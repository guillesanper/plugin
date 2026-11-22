// SPDX-License-Identifier: MIT
#include "esceq/dsp/phase_equalizer.h"

#include "esceq/dsp/partitioned_convolver.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace esceq::dsp
{

namespace
{

/// Lo que de una banda cuenta para el FIR: la parte estática.
bool sameStaticPart(const BandSettings& a, const BandSettings& b) noexcept
{
    return a.enabled == b.enabled && a.filter == b.filter && a.channel == b.channel;
}

/// Tramo más largo que procesa de una vez la dinámica espectral (sus búferes).
constexpr int kSpectralChunk = 512;

float finiteOrZero(float sample) noexcept
{
    return std::isfinite(sample) ? sample : 0.0f;
}

/// Retardo de exactamente `delay` muestras.
class DelayLine
{
public:
    explicit DelayLine(int delay) : buffer_(static_cast<std::size_t>(std::max(delay, 0)), 0.0f) {}

    void reset() noexcept { std::fill(buffer_.begin(), buffer_.end(), 0.0f); }

    /// `in` y `out` pueden ser la misma memoria.
    void process(const float* in, float* out, int length) noexcept
    {
        if (buffer_.empty())
        {
            std::copy_n(in, length, out);
            return;
        }
        const int size = static_cast<int>(buffer_.size());
        for (int i = 0; i < length; ++i)
        {
            const float sample = in[i];
            out[i] = buffer_[static_cast<std::size_t>(position_)];
            buffer_[static_cast<std::size_t>(position_)] = sample;
            position_ = position_ + 1 == size ? 0 : position_ + 1;
        }
    }

private:
    std::vector<float> buffer_;
    int position_ = 0;
};

} // namespace

/// Un FIR listo para la convolución, con lo necesario para saber a qué
/// ajustes y a qué modo corresponde.
struct PhaseEqualizer::Kernel
{
    Kernel(const LinearPhaseFir& design, PhaseMode phaseMode, double rate, std::uint64_t gen)
        : fir(design.size.blockSize, design.ll, design.lr, design.rl, design.rr), mode(phaseMode), sampleRate(rate),
          size(design.size), generation(gen), identity(design.identity)
    {
    }

    FirKernel fir;
    PhaseMode mode;
    double sampleRate;
    LinearPhaseSize size;
    std::uint64_t generation;
    bool identity;
};

/// Todo lo que suena en un modo de fase lineal, salvo la dinámica: la
/// convolución, los retardos de la señal seca y del sidechain, y su memoria.
struct PhaseEqualizer::LinearPath
{
    LinearPath(PhaseMode phaseMode, double rate, std::uint64_t pathEpoch)
        : mode(phaseMode), sampleRate(rate), size(linearPhaseSize(phaseMode, rate)), epoch(pathEpoch),
          convolver(size.blockSize, size.partitions()), dry{DelayLine(size.latency()), DelayLine(size.latency())},
          side{DelayLine(size.latency()), DelayLine(size.latency())}
    {
        for (auto* group : {&input, &filtered, &delayed, &sidechain})
            for (auto& buffer : *group)
                buffer.assign(static_cast<std::size_t>(size.blockSize), 0.0f);
    }

    void reset() noexcept
    {
        convolver.reset();
        for (auto& line : dry)
            line.reset();
        for (auto& line : side)
            line.reset();
        outputIdentity = kernel == nullptr || kernel->identity;
    }

    PhaseMode mode;
    double sampleRate;
    LinearPhaseSize size;
    std::uint64_t epoch;
    PartitionedConvolver convolver;
    std::unique_ptr<Kernel> kernel; ///< El que calcula los bloques.
    std::unique_ptr<Kernel> next;   ///< Recogido, para el próximo bloque.
    /// El bloque que está saliendo es el de un núcleo identidad, sin fundido:
    /// la salida puede tomarse de la señal seca retrasada, bit a bit.
    bool outputIdentity = true;
    bool outputFading = false; ///< El bloque que está saliendo es un fundido entre núcleos.
    float wet = 1.0f;          ///< Mezcla del bypass.
    std::array<DelayLine, kMaxChannels> dry;
    std::array<DelayLine, kMaxChannels> side;
    std::array<std::vector<float>, kMaxChannels> input, filtered, delayed, sidechain;
};

/// La dinámica espectral, tras el ecualizador: el motor, la señal seca
/// retrasada como él (para el bypass) y la entrada del ecualizador en IIR
/// (lo que escuchan sus detectores).
struct PhaseEqualizer::SpectralPath
{
    SpectralPath() : dry{DelayLine(0), DelayLine(0)} {}

    void prepare(double sampleRate)
    {
        engine.prepare(sampleRate);
        for (auto& line : dry)
            line = DelayLine(engine.latency());
        for (auto* group : {&detector, &delayed})
            for (auto& buffer : *group)
                buffer.assign(static_cast<std::size_t>(kSpectralChunk), 0.0f);
    }

    void reset(bool bypassed) noexcept
    {
        engine.reset();
        for (auto& line : dry)
            line.reset();
        wet = bypassed ? 0.0f : 1.0f;
    }

    SpectralDynamics engine;
    std::array<DelayLine, kMaxChannels> dry;
    std::array<std::vector<float>, kMaxChannels> detector, delayed;
    float wet = 1.0f; ///< Mezcla del bypass.
};

/// La saturación, al final: el motor y la señal seca retrasada como él en
/// fase lineal (para el bypass; en IIR no tiene latencia).
struct PhaseEqualizer::SaturationPath
{
    SaturationPath() : dry{DelayLine(0), DelayLine(0)} {}

    void prepare(double sampleRate, OversamplingFilter filter)
    {
        engine.setFilter(filter);
        engine.prepare(sampleRate);
        for (auto& line : dry)
            line = DelayLine(Saturation::latencySamples(OversamplingFilter::LinearPhase, sampleRate));
        for (auto& buffer : delayed)
            buffer.assign(static_cast<std::size_t>(kSaturationChunk), 0.0f);
    }

    void reset(bool bypassed) noexcept
    {
        engine.reset();
        for (auto& line : dry)
            line.reset();
        wet = bypassed ? 0.0f : 1.0f;
    }

    /// Tramo más largo de la señal seca retrasada.
    static constexpr int kSaturationChunk = 512;

    Saturation engine;
    std::array<DelayLine, kMaxChannels> dry;
    std::array<std::vector<float>, kMaxChannels> delayed;
    float wet = 1.0f; ///< Mezcla del bypass.
};

PhaseEqualizer::PhaseEqualizer()
    : spectral_(std::make_unique<SpectralPath>()), saturation_(std::make_unique<SaturationPath>())
{
    spectral_->prepare(sampleRate_);
    saturation_->prepare(sampleRate_, OversamplingFilter::Iir);
    publishRequest();
}

PhaseEqualizer::~PhaseEqualizer() = default;

void PhaseEqualizer::prepare(double sampleRate, int numChannels)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
    fadeLength_ = std::max(1, static_cast<int>(std::lround(kTransitionSeconds * sampleRate_)));
    iir_.prepare(sampleRate_);
    spectral_->prepare(sampleRate_);
    saturation_->prepare(sampleRate_, OversamplingFilter::Iir);

    // Lo de antes era para otra frecuencia de muestreo. No es tiempo real:
    // se puede destruir aquí.
    path_.reset();
    incoming_.reset();
    activeMode_ = PhaseMode::Iir;
    activeSpectral_ = false;
    for (int index = 0; index < kMaxBands; ++index)
        routeBand(index);
    // En IIR, la saturación no cambia la latencia: suena ya, sin fundido.
    activeSaturation_ = requestedSaturation_;
    saturation_->engine.setMode(saturationSettings_.mode);
    resetSaturation();
    latency_.store(0, std::memory_order_relaxed);
    configureIir();
    stage_ = Stage::Steady;
    transitionGain_ = 1.0f;
    fresh_ = true;

    ++epoch_;
    ++generation_;
    publishRequest();
}

void PhaseEqualizer::publishRequest() noexcept
{
    DesignRequest& request = requests_.writeSlot();
    request.generation = generation_;
    request.epoch = epoch_;
    request.mode = requestedMode_;
    request.sampleRate = sampleRate_;
    request.numChannels = numChannels_;
    request.bands = bands_;
    requests_.publish();
}

void PhaseEqualizer::setBand(int index, const BandSettings& settings) noexcept ESCEQ_NONBLOCKING
{
    if (index < 0 || index >= kMaxBands)
        return;
    BandSettings& band = bands_[static_cast<std::size_t>(index)];
    const BandSettings next = sanitizeBand(settings);
    if (next == band)
        return;
    const bool firChanged = !sameStaticPart(band, next);
    band = next;
    routeBand(index);
    requestedSpectral_ =
        std::any_of(bands_.begin(), bands_.end(), [](const BandSettings& b) { return b.dynamics.spectral; });
    if (firChanged)
    {
        ++generation_;
        publishRequest();
    }
}

void PhaseEqualizer::routeBand(int index) noexcept
{
    // Sin dinámica espectral, el Equalizer recibe la banda tal cual (con el
    // campo spectral, que no mira, a false) y el motor, la banda sin dinámica.
    const BandSettings& band = bands_[static_cast<std::size_t>(index)];
    BandSettings normal = band;
    normal.dynamics.spectral = false;
    BandSettings masked = normal;
    if (activeSpectral_ && band.dynamics.spectral)
        normal.dynamics.enabled = false;
    else
        masked.dynamics.enabled = false;
    iir_.setBand(index, normal);
    spectral_->engine.setBand(index, masked);
}

double PhaseEqualizer::dynamicGainDb(int index) const noexcept
{
    if (activeSpectral_ && spectral_->engine.isActive(index))
        return spectral_->engine.gainDb(index);
    return iir_.dynamicGainDb(index);
}

const SpectralDynamics& PhaseEqualizer::spectralDynamics() const noexcept
{
    return spectral_->engine;
}

void PhaseEqualizer::setBypassed(bool bypassed) noexcept ESCEQ_NONBLOCKING
{
    bypassed_ = bypassed;
    if (!isLinearPhase(activeMode_))
        iir_.setBypassed(bypassed);
}

void PhaseEqualizer::setPhaseMode(PhaseMode mode) noexcept ESCEQ_NONBLOCKING
{
    if (mode == requestedMode_)
        return;
    requestedMode_ = mode;
    ++epoch_;
    ++generation_;
    publishRequest();
}

void PhaseEqualizer::setSaturation(const SaturationSettings& settings) noexcept ESCEQ_NONBLOCKING
{
    SaturationSettings next = settings;
    if (next.mode > SaturationMode::Warm)
        next.mode = SaturationMode::Off;
    Saturation& engine = saturation_->engine;
    // El drive y la mezcla, siempre: con Off no suenan y saltan sin rampa.
    engine.setDriveDb(next.driveDb);
    engine.setMix(next.mix);
    if (std::isfinite(next.driveDb))
        next.driveDb = std::clamp(next.driveDb, 0.0, Saturation::kMaxDriveDb);
    else
        next.driveDb = saturationSettings_.driveDb;
    if (std::isfinite(next.mix))
        next.mix = std::clamp(next.mix, 0.0, 1.0);
    else
        next.mix = saturationSettings_.mix;
    saturationSettings_ = next;
    requestedSaturation_ = next.mode != SaturationMode::Off;
    applySaturationMode();
}

void PhaseEqualizer::applySaturationMode() noexcept
{
    // En fase lineal, encender o apagar cambia la latencia: se hace en el
    // silencio del cambio (switchMode()). Mientras, el motor sigue como estaba.
    if (requestedSaturation_ != activeSaturation_ && isLinearPhase(activeMode_))
        return;
    Saturation& engine = saturation_->engine;
    const bool wasRunning = engine.isActive();
    activeSaturation_ = requestedSaturation_;
    engine.setMode(saturationSettings_.mode);
    if (!wasRunning && engine.isActive())
    {
        // Empieza de cero, con su fundido (Saturation), y con el bypass donde toca.
        for (auto& line : saturation_->dry)
            line.reset();
        saturation_->wet = bypassed_ ? 0.0f : 1.0f;
    }
}

void PhaseEqualizer::resetSaturation() noexcept
{
    saturation_->reset(bypassed_);
}

int PhaseEqualizer::saturationLatency(PhaseMode mode, double sampleRate) noexcept
{
    return Saturation::latencySamples(isLinearPhase(mode) ? OversamplingFilter::LinearPhase : OversamplingFilter::Iir,
                                      sampleRate);
}

const BandSettings& PhaseEqualizer::target(int index) const noexcept
{
    return bands_[static_cast<std::size_t>(std::clamp(index, 0, kMaxBands - 1))];
}

void PhaseEqualizer::configureIir() noexcept
{
    const bool linear = isLinearPhase(activeMode_);
    iir_.setDynamicsOnly(linear);
    iir_.setBypassed(linear ? false : bypassed_);
    iir_.reset();
}

void PhaseEqualizer::reset() noexcept ESCEQ_NONBLOCKING
{
    // Una transición a medias se completa si el modo nuevo ya está listo.
    if (stage_ == Stage::FadingOut && targetReady())
        switchMode();
    stage_ = Stage::Steady;
    transitionGain_ = 1.0f;
    iir_.reset();
    if (path_ != nullptr)
    {
        path_->reset();
        path_->wet = bypassed_ ? 0.0f : 1.0f;
    }
    resetSpectral();
    resetSaturation();
    fresh_ = true;
}

void PhaseEqualizer::resetSpectral() noexcept
{
    // Sin dinámica espectral, su memoria no se usa.
    if (activeSpectral_)
        spectral_->reset(bypassed_);
}

bool PhaseEqualizer::switchPending() const noexcept
{
    return activeMode_ != requestedMode_ || activeSpectral_ != requestedSpectral_ ||
           activeSaturation_ != requestedSaturation_;
}

bool PhaseEqualizer::targetReady() const noexcept
{
    if (activeMode_ == requestedMode_)
        return true; // solo cambia la dinámica espectral o la saturación
    if (isLinearPhase(requestedMode_))
        return incoming_ != nullptr;
    // A IIR: hay que poder devolver el camino lineal.
    return path_ == nullptr || paths_.freeRetireSlots() >= 1;
}

void PhaseEqualizer::switchMode() noexcept
{
    if (activeMode_ != requestedMode_)
    {
        paths_.retire(std::move(path_));
        path_ = std::move(incoming_);
        activeMode_ = path_ != nullptr ? path_->mode : PhaseMode::Iir;
        configureIir();
        if (path_ != nullptr)
        {
            path_->reset();
            path_->wet = bypassed_ ? 0.0f : 1.0f;
        }
    }
    if (activeSpectral_ != requestedSpectral_)
    {
        // La dinámica de las bandas espectrales pasa del Equalizer a la máscara, o al revés.
        activeSpectral_ = requestedSpectral_;
        for (int index = 0; index < kMaxBands; ++index)
            routeBand(index);
    }
    // La saturación, con los filtros del modo que suena y en el modo pedido.
    Saturation& saturation = saturation_->engine;
    saturation.setFilter(isLinearPhase(activeMode_) ? OversamplingFilter::LinearPhase : OversamplingFilter::Iir);
    activeSaturation_ = requestedSaturation_;
    saturation.setMode(saturationSettings_.mode);
    // Lo que tuviera dentro saldría tras el silencio: se empieza de cero (la
    // saturación, también sin el fundido: entra y sale en el silencio).
    resetSpectral();
    resetSaturation();
    const int linear = path_ != nullptr ? path_->size.latency() : 0;
    const int spectral = activeSpectral_ ? spectral_->engine.latency() : 0;
    const int saturating = activeSaturation_ ? saturation.latency() : 0;
    latency_.store(linear + spectral + saturating, std::memory_order_relaxed);
}

void PhaseEqualizer::discardPath(std::unique_ptr<LinearPath> path) noexcept
{
    // Si se diseñó para lo que se pide ahora (se ha pedido el modo que ya
    // suena, por ejemplo), su FIR es el más reciente y se iría con él: se
    // pide otro para el camino que suena.
    if (path->epoch == epoch_)
    {
        ++generation_;
        publishRequest();
    }
    paths_.retire(std::move(path));
}

void PhaseEqualizer::takePaths() noexcept
{
    // Hace falta un camino nuevo si el modo pedido es lineal y no es el que suena.
    const bool wanted = isLinearPhase(requestedMode_) && (activeMode_ != requestedMode_ || path_ == nullptr);
    if (incoming_ != nullptr && (!wanted || incoming_->epoch != epoch_))
        discardPath(std::move(incoming_));

    if (auto path = paths_.take())
    {
        if (wanted && incoming_ == nullptr && path->epoch == epoch_)
            incoming_ = std::move(path);
        else
            discardPath(std::move(path));
    }
}

void PhaseEqualizer::takeKernels() noexcept
{
    // Un núcleo nuevo espera al bloque siguiente: no se recoge otro mientras.
    if (path_ != nullptr && path_->next != nullptr)
        return;

    auto kernel = kernels_.take();
    if (kernel == nullptr)
        return;

    auto fits = [&kernel](const LinearPath* path)
    {
        return path != nullptr && kernel->mode == path->mode && kernel->sampleRate == path->sampleRate &&
               kernel->size == path->size && kernel->generation > path->kernel->generation;
    };

    if (fits(incoming_.get()))
    {
        // Aún no suena: se sustituye sin fundido.
        kernels_.retire(std::move(incoming_->kernel));
        incoming_->kernel = std::move(kernel);
        incoming_->outputIdentity = incoming_->kernel->identity;
    }
    else if (fits(path_.get()))
    {
        if (fresh_)
        {
            kernels_.retire(std::move(path_->kernel));
            path_->kernel = std::move(kernel);
            path_->outputIdentity = path_->kernel->identity;
        }
        else
            path_->next = std::move(kernel);
    }
    else
        kernels_.retire(std::move(kernel));
}

void PhaseEqualizer::update() noexcept ESCEQ_NONBLOCKING
{
    takePaths();
    takeKernels();
    // Sin audio de por medio, el modo pedido entra sin transición.
    if (fresh_ && switchPending() && targetReady())
        switchMode();
}

void PhaseEqualizer::beginTransitionIfNeeded() noexcept
{
    // Avanza de etapa mientras la actual haya terminado: una etapa de
    // duración 0 (esperar la latencia de IIR, por ejemplo) no ocupa ningún tramo.
    for (;;)
    {
        switch (stage_)
        {
        case Stage::Steady:
            if (!switchPending() || !targetReady())
                return;
            stage_ = Stage::FadingOut;
            stageRemaining_ = fadeLength_;
            return;
        case Stage::FadingOut:
            if (!switchPending())
            {
                // Se ha vuelto a pedir lo que suena: se deshace la bajada.
                stage_ = Stage::FadingIn;
                stageRemaining_ =
                    static_cast<int>(std::lround((1.0f - transitionGain_) * static_cast<float>(fadeLength_)));
                continue;
            }
            if (stageRemaining_ > 0 || !targetReady())
                return; // bajando, o en silencio esperando al modo nuevo
            switchMode();
            stage_ = Stage::Priming;
            stageRemaining_ = latency_.load(std::memory_order_relaxed);
            continue;
        case Stage::Priming:
            if (stageRemaining_ > 0)
                return;
            stage_ = Stage::FadingIn;
            stageRemaining_ = fadeLength_;
            continue;
        case Stage::FadingIn:
            if (stageRemaining_ > 0)
                return;
            stage_ = Stage::Steady;
            transitionGain_ = 1.0f;
            continue; // puede haberse pedido otro modo mientras
        }
        return;
    }
}

int PhaseEqualizer::segmentLength(int available) const noexcept
{
    int length = available;
    if (stage_ != Stage::Steady && stageRemaining_ > 0)
        length = std::min(length, stageRemaining_);
    if (path_ != nullptr)
        length = std::min(length, path_->convolver.samplesUntilBlock());
    else if (activeSpectral_)
        length = std::min(length, kSpectralChunk); // la copia de la entrada para los detectores
    return length;
}

void PhaseEqualizer::applyTransition(float* const* io, int numChannels, int length) noexcept
{
    const float step = 1.0f / static_cast<float>(fadeLength_);
    switch (stage_)
    {
    case Stage::Steady:
        return;
    case Stage::Priming:
        for (int ch = 0; ch < numChannels; ++ch)
            std::fill_n(io[ch], length, 0.0f);
        break;
    case Stage::FadingOut:
    case Stage::FadingIn:
    {
        const float direction = stage_ == Stage::FadingIn ? step : -step;
        for (int i = 0; i < length; ++i)
        {
            transitionGain_ = std::clamp(transitionGain_ + direction, 0.0f, 1.0f);
            for (int ch = 0; ch < numChannels; ++ch)
                io[ch][i] *= transitionGain_;
        }
        if (stage_ == Stage::FadingOut && stageRemaining_ == 0)
            for (int ch = 0; ch < numChannels; ++ch)
                std::fill_n(io[ch], length, 0.0f); // esperando a que el modo nuevo esté listo
        break;
    }
    }
    stageRemaining_ = std::max(0, stageRemaining_ - length);
}

void PhaseEqualizer::process(float* const* channels, int numChannels, int numSamples, const float* const* sidechain,
                             int numSidechainChannels) noexcept ESCEQ_NONBLOCKING
{
    const int usedChannels = std::min(numChannels, kMaxChannels);
    if (numSamples <= 0 || usedChannels <= 0)
        return;

    if (usedChannels != numChannels_)
    {
        // El FIR depende de si la señal es mono o estéreo.
        numChannels_ = usedChannels;
        ++generation_;
        publishRequest();
    }

    update();
    fresh_ = false;
    beginTransitionIfNeeded();

    // Lo normal en IIR: el IIR y, si no está en Off, la saturación.
    if (stage_ == Stage::Steady && path_ == nullptr && !activeSpectral_)
    {
        iir_.process(channels, usedChannels, numSamples, sidechain, numSidechainChannels);
        renderSaturation(channels, usedChannels, numSamples);
        return;
    }

    std::array<float*, kMaxChannels> io{};
    std::array<const float*, kMaxChannels> sc{};
    const int scChannels = sidechain != nullptr ? std::clamp(numSidechainChannels, 0, kMaxChannels) : 0;
    for (int position = 0; position < numSamples;)
    {
        const int length = segmentLength(numSamples - position);
        for (int ch = 0; ch < usedChannels; ++ch)
            io[static_cast<std::size_t>(ch)] = channels[ch] + position;
        for (int ch = 0; ch < scChannels; ++ch)
            sc[static_cast<std::size_t>(ch)] = sidechain[ch] + position;

        const float* const* side = scChannels > 0 ? sc.data() : nullptr;
        // Lo que escuchan los detectores de la dinámica espectral: la entrada
        // del ecualizador y el sidechain, retrasados como el audio.
        std::array<const float*, kMaxChannels> detector{};
        std::array<const float*, kMaxChannels> detectorSide{};
        if (path_ != nullptr)
        {
            renderLinear(io.data(), usedChannels, length, side, scChannels);
            for (std::size_t c = 0; c < kMaxChannels; ++c)
            {
                detector[c] = path_->delayed[c].data();
                detectorSide[c] = path_->sidechain[c].data();
            }
            side = scChannels > 0 ? detectorSide.data() : nullptr;
        }
        else
        {
            if (activeSpectral_)
                for (int ch = 0; ch < usedChannels; ++ch)
                {
                    const auto c = static_cast<std::size_t>(ch);
                    std::copy_n(io[c], length, spectral_->detector[c].data());
                    detector[c] = spectral_->detector[c].data();
                }
            iir_.process(io.data(), usedChannels, length, side, scChannels);
        }
        if (activeSpectral_)
            renderSpectral(io.data(), usedChannels, length, side, scChannels, detector.data());
        renderSaturation(io.data(), usedChannels, length);

        applyTransition(io.data(), usedChannels, length);
        position += length;
        if (stage_ != Stage::Steady)
            beginTransitionIfNeeded();
    }
}

void PhaseEqualizer::renderLinear(float* const* io, int numChannels, int length, const float* const* sidechain,
                                  int numSidechainChannels) noexcept
{
    LinearPath& path = *path_;

    std::array<float*, kMaxChannels> filtered{};
    std::array<const float*, kMaxChannels> delayed{};
    std::array<const float*, kMaxChannels> sidechainDelayed{};
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        const auto c = static_cast<std::size_t>(ch);
        // Mono: el canal que falta se rellena con el único, sin efecto en el resultado.
        float* const in = path.input[c].data();
        const float* source = io[std::min(ch, numChannels - 1)];
        for (int i = 0; i < length; ++i)
            in[i] = finiteOrZero(source[i]);
        path.dry[c].process(in, path.delayed[c].data(), length);
        delayed[c] = path.delayed[c].data();
        filtered[c] = path.filtered[c].data();

        // El sidechain se retrasa igual que el audio, también sin conectar (con
        // silencio), para que al conectarlo no suene lo de antes.
        float* const side = path.sidechain[c].data();
        if (ch < numSidechainChannels)
            for (int i = 0; i < length; ++i)
                side[i] = finiteOrZero(sidechain[ch][i]);
        else
            std::fill_n(side, length, 0.0f);
        path.side[c].process(side, side, length);
        sidechainDelayed[c] = side;
    }

    // El bloque que sale en este tramo se calculó en el bloque anterior; si
    // este tramo completa un bloque, el siguiente se calcula ya con el núcleo
    // nuevo, fundiendo desde el anterior.
    const bool completesBlock = length == path.convolver.samplesUntilBlock();
    const bool installing = completesBlock && path.next != nullptr;
    const Kernel& compute = installing ? *path.next : *path.kernel;
    const FirKernel* fadeFrom = installing ? &path.kernel->fir : nullptr;
    path.convolver.process(path.input[0].data(), path.input[1].data(), filtered[0], filtered[1], length, compute.fir,
                           fadeFrom);

    if (path.outputIdentity && !path.outputFading)
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy_n(delayed[static_cast<std::size_t>(ch)], length, filtered[static_cast<std::size_t>(ch)]);

    if (completesBlock)
    {
        path.outputFading = installing;
        path.outputIdentity = compute.identity;
        if (installing)
        {
            kernels_.retire(std::move(path.kernel));
            path.kernel = std::move(path.next);
        }
    }

    // La dinámica, sobre la señal ya ecualizada, con los detectores
    // escuchando la entrada del ecualizador retrasada como el audio.
    iir_.process(filtered.data(), numChannels, length, numSidechainChannels > 0 ? sidechainDelayed.data() : nullptr,
                 numSidechainChannels, delayed.data());

    // Bypass: fundido con la señal seca retrasada, para que la latencia no cambie.
    const float wetTarget = bypassed_ ? 0.0f : 1.0f;
    const float step = 1.0f / static_cast<float>(fadeLength_);
    for (int i = 0; i < length; ++i)
    {
        if (path.wet != wetTarget)
            path.wet =
                wetTarget > path.wet ? std::min(wetTarget, path.wet + step) : std::max(wetTarget, path.wet - step);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto c = static_cast<std::size_t>(ch);
            const float dry = delayed[c][i];
            const float wet = filtered[c][i];
            io[ch][i] = path.wet == 1.0f ? wet : path.wet == 0.0f ? dry : dry + path.wet * (wet - dry);
        }
    }
}

void PhaseEqualizer::renderSpectral(float* const* io, int numChannels, int length, const float* const* sidechain,
                                    int numSidechainChannels, const float* const* detectorInput) noexcept
{
    SpectralPath& stage = *spectral_;
    const float wetTarget = bypassed_ ? 0.0f : 1.0f;
    const float step = 1.0f / static_cast<float>(fadeLength_);
    std::array<float*, kMaxChannels> out{};
    std::array<const float*, kMaxChannels> side{};
    std::array<const float*, kMaxChannels> detector{};
    for (int offset = 0; offset < length;)
    {
        const int count = std::min(length - offset, kSpectralChunk);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto c = static_cast<std::size_t>(ch);
            out[c] = io[ch] + offset;
            detector[c] = detectorInput[ch] + offset;
            // La señal seca, retrasada como la máscara, para el bypass.
            stage.dry[c].process(out[c], stage.delayed[c].data(), count);
        }
        for (int ch = 0; ch < numSidechainChannels; ++ch)
            side[static_cast<std::size_t>(ch)] = sidechain[ch] + offset;
        stage.engine.process(out.data(), numChannels, count, sidechain != nullptr ? side.data() : nullptr,
                             numSidechainChannels, detector.data());

        // Bypass: fundido con la señal seca retrasada, para que la latencia no cambie.
        if (stage.wet != wetTarget || wetTarget == 0.0f)
            for (int i = 0; i < count; ++i)
            {
                if (stage.wet != wetTarget)
                    stage.wet = wetTarget > stage.wet ? std::min(wetTarget, stage.wet + step)
                                                      : std::max(wetTarget, stage.wet - step);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto c = static_cast<std::size_t>(ch);
                    const float dry = stage.delayed[c][static_cast<std::size_t>(i)];
                    const float wet = out[c][i];
                    out[c][i] = stage.wet == 1.0f ? wet : stage.wet == 0.0f ? dry : dry + stage.wet * (wet - dry);
                }
            }
        offset += count;
    }
}

void PhaseEqualizer::renderSaturation(float* const* io, int numChannels, int length) noexcept
{
    SaturationPath& stage = *saturation_;
    if (!stage.engine.isActive())
        return; // Off: bit a bit lo de antes
    const bool delayedDry = stage.engine.filter() == OversamplingFilter::LinearPhase;
    const float wetTarget = bypassed_ ? 0.0f : 1.0f;
    const float step = 1.0f / static_cast<float>(fadeLength_);
    std::array<float*, kMaxChannels> out{};
    for (int offset = 0; offset < length;)
    {
        const int count = std::min(length - offset, SaturationPath::kSaturationChunk);
        // La señal seca para el bypass: retrasada como la saturación en fase
        // lineal (la línea se alimenta siempre); en IIR, la entrada, solo si hace falta.
        const bool mixing = stage.wet != wetTarget || wetTarget == 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto c = static_cast<std::size_t>(ch);
            out[c] = io[ch] + offset;
            if (delayedDry)
                stage.dry[c].process(out[c], stage.delayed[c].data(), count);
            else if (mixing)
                std::copy_n(out[c], count, stage.delayed[c].data());
        }
        stage.engine.process(out.data(), numChannels, count);

        // Bypass: fundido con la señal seca retrasada, para que la latencia no cambie.
        if (mixing)
            for (int i = 0; i < count; ++i)
            {
                if (stage.wet != wetTarget)
                    stage.wet = wetTarget > stage.wet ? std::min(wetTarget, stage.wet + step)
                                                      : std::max(wetTarget, stage.wet - step);
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto c = static_cast<std::size_t>(ch);
                    const float dry = stage.delayed[c][static_cast<std::size_t>(i)];
                    const float wet = out[c][i];
                    out[c][i] = stage.wet == 1.0f ? wet : stage.wet == 0.0f ? dry : dry + stage.wet * (wet - dry);
                }
            }
        offset += count;
    }
}

bool PhaseEqualizer::isSettled() const noexcept
{
    if (stage_ != Stage::Steady || switchPending() || !iir_.isSettled())
        return false;
    if (activeSpectral_ && spectral_->wet != (bypassed_ ? 0.0f : 1.0f))
        return false;
    if (saturation_->engine.isActive() && saturation_->wet != (bypassed_ ? 0.0f : 1.0f))
        return false;
    if (path_ == nullptr)
        return true;
    return path_->next == nullptr && !path_->outputFading && path_->kernel->generation == generation_ &&
           path_->wet == (bypassed_ ? 0.0f : 1.0f);
}

bool PhaseEqualizer::serviceDesigns()
{
    paths_.collect();
    kernels_.collect();
    if (const DesignRequest* request = requests_.read())
        latest_ = *request;
    if (!isLinearPhase(latest_.mode))
        return false;

    auto makeKernel = [this]
    {
        const auto fir = designer_.design(latest_.bands, latest_.mode, latest_.sampleRate, latest_.numChannels);
        return std::make_unique<Kernel>(fir, latest_.mode, latest_.sampleRate, latest_.generation);
    };

    if (latest_.epoch != builtEpoch_)
    {
        auto path = std::make_unique<LinearPath>(latest_.mode, latest_.sampleRate, latest_.epoch);
        path->kernel = makeKernel();
        path->outputIdentity = path->kernel->identity;
        paths_.offer(std::move(path));
        builtEpoch_ = latest_.epoch;
        builtGeneration_ = latest_.generation;
        return true;
    }
    if (latest_.generation != builtGeneration_)
    {
        kernels_.offer(makeKernel());
        builtGeneration_ = latest_.generation;
        return true;
    }
    return false;
}

} // namespace esceq::dsp
