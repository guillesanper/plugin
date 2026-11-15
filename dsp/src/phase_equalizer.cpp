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

PhaseEqualizer::PhaseEqualizer()
{
    publishRequest();
}

PhaseEqualizer::~PhaseEqualizer() = default;

void PhaseEqualizer::prepare(double sampleRate, int numChannels)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    numChannels_ = std::clamp(numChannels, 1, kMaxChannels);
    fadeLength_ = std::max(1, static_cast<int>(std::lround(kTransitionSeconds * sampleRate_)));
    iir_.prepare(sampleRate_);

    // Lo de antes era para otra frecuencia de muestreo. No es tiempo real:
    // se puede destruir aquí.
    path_.reset();
    incoming_.reset();
    activeMode_ = PhaseMode::Iir;
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
    iir_.setBand(index, next);
    if (firChanged)
    {
        ++generation_;
        publishRequest();
    }
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
    fresh_ = true;
}

bool PhaseEqualizer::targetReady() const noexcept
{
    if (isLinearPhase(requestedMode_))
        return incoming_ != nullptr;
    // A IIR: hay que poder devolver el camino lineal.
    return path_ == nullptr || paths_.freeRetireSlots() >= 1;
}

void PhaseEqualizer::switchMode() noexcept
{
    paths_.retire(std::move(path_));
    path_ = std::move(incoming_);
    activeMode_ = path_ != nullptr ? path_->mode : PhaseMode::Iir;
    latency_.store(path_ != nullptr ? path_->size.latency() : 0, std::memory_order_relaxed);
    configureIir();
    if (path_ != nullptr)
    {
        path_->reset();
        path_->wet = bypassed_ ? 0.0f : 1.0f;
    }
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
    if (fresh_ && activeMode_ != requestedMode_ && targetReady())
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
            if (activeMode_ == requestedMode_ || !targetReady())
                return;
            stage_ = Stage::FadingOut;
            stageRemaining_ = fadeLength_;
            return;
        case Stage::FadingOut:
            if (activeMode_ == requestedMode_)
            {
                // Se ha vuelto a pedir el modo que suena: se deshace la bajada.
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

    // Lo normal en IIR: el IIR, sin nada más.
    if (stage_ == Stage::Steady && path_ == nullptr)
    {
        iir_.process(channels, usedChannels, numSamples, sidechain, numSidechainChannels);
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

        if (path_ != nullptr)
            renderLinear(io.data(), usedChannels, length, scChannels > 0 ? sc.data() : nullptr, scChannels);
        else
            iir_.process(io.data(), usedChannels, length, scChannels > 0 ? sc.data() : nullptr, scChannels);

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

bool PhaseEqualizer::isSettled() const noexcept
{
    if (stage_ != Stage::Steady || activeMode_ != requestedMode_ || !iir_.isSettled())
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
