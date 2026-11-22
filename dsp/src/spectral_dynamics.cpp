// SPDX-License-Identifier: MIT
#include "esceq/dsp/spectral_dynamics.h"

#include "esceq/dsp/dynamics.h"
#include "esceq/dsp/filter_design.h"
#include "esceq/dsp/linear_phase.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace esceq::dsp {

namespace {

constexpr double kPi = std::numbers::pi;

/// Energía de un seno en los bins de su lóbulo principal con la ventana de
/// Hann, relativa a la del bin central cuando cae justo en él: N·Σw²/(Σw)² = 1,5.
constexpr double kHannLobeEnergy = 1.5;

/// Coeficiente por trama de una constante de tiempo, con un salto como mínimo.
float frameCoefficient(double ms, double hopMs) noexcept
{
    const double tau = std::isfinite(ms) ? std::max(ms, hopMs) : hopMs;
    return static_cast<float>(std::exp(-hopMs / tau));
}

/// 20·log10(x) para un x normal y positivo, sin llamar a la biblioteca y sin
/// ramas: es lo que más se repite (un bin de cada banda en cada trama), y así
/// se vectoriza. Para 0 da un número finito, que no se usa. Con x = m·2^e y m
/// en [√½, √2), ln(m) = 2·atanh(t), t = (m − 1)/(m + 1), con |t| < 0,172 y
/// cuatro términos de la serie: error por debajo de 1e-4 dB (medido, 3e-5 de
/// 10^-8 a 100, sobre todo el redondeo del float), y el mismo
/// resultado en todos los sistemas.
float decibels(float x) noexcept
{
    const auto bits = std::bit_cast<std::uint32_t>(x);
    const auto exponent = static_cast<float>(static_cast<std::int32_t>(bits >> 23) - 127); // x > 0: sin signo
    const float mantissa = std::bit_cast<float>((bits & 0x7FFFFFu) | 0x3F800000u);       // [1, 2)
    const bool high = mantissa > 1.41421356f;
    const float m = mantissa * (high ? 0.5f : 1.0f);
    const float e = exponent + (high ? 1.0f : 0.0f);
    const float t = (m - 1.0f) / (m + 1.0f);
    const float t2 = t * t;
    const float ln = 2.0f * t * (1.0f + t2 * (1.0f / 3.0f + t2 * (1.0f / 5.0f + t2 * (1.0f / 7.0f))));
    constexpr float kDbPerNeper = 8.68588963806503655f; // 20/ln 10
    constexpr float kLn2 = 0.693147180559945309f;
    return kDbPerNeper * (ln + e * kLn2);
}

/// Lo que decide la forma del detector: la ganancia de la banda no influye.
FilterParams weightKey(FilterParams params) noexcept
{
    params.gainDb = 0.0;
    return params;
}

} // namespace

int SpectralDynamics::fftSize(double sampleRate) noexcept
{
    return kBaseSize * sampleRateFactor(sampleRate);
}

SpectralDynamics::SpectralDynamics()
{
    prepare(48000.0);
}

SpectralDynamics::~SpectralDynamics() = default;

void SpectralDynamics::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
    const int size = fftSize(sampleRate_);
    if (size != size_)
    {
        size_ = size;
        bins_ = size / 2 + 1;
        int order = 0;
        while ((1 << order) < size)
            ++order;
        fft_ = std::make_unique<Fft>(order);
    }
    const auto n = static_cast<std::size_t>(size_);
    const auto bins = static_cast<std::size_t>(bins_);
    ringSize_ = 2 * size_;
    const auto ring = static_cast<std::size_t>(ringSize_);

    // Ventanas periódicas: con un salto de N/4, la suma de los cuadrados de la
    // raíz de Hann solapada es 2 en todas las muestras.
    analysisWindow_.assign(n, 0.0f);
    detectorWindow_.assign(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n));
        detectorWindow_[i] = static_cast<float>(hann);
        analysisWindow_[i] = static_cast<float>(std::sqrt(hann));
    }

    // Detector de cada bin: de f·2^(−1/12) a f·2^(1/12), y un bin a cada lado como mínimo.
    detectorLow_.assign(bins, 0);
    detectorHigh_.assign(bins, 0);
    const double halfWidth = std::exp2(0.5 * kDetectorOctaves);
    for (int k = 0; k < bins_; ++k)
    {
        const double bin = static_cast<double>(k);
        const int low = std::min(k - 1, static_cast<int>(std::floor(bin / halfWidth + 0.5)));
        const int high = std::max(k + 1, static_cast<int>(std::floor(bin * halfWidth + 0.5)));
        detectorLow_[static_cast<std::size_t>(k)] = std::max(low, 0);
        detectorHigh_[static_cast<std::size_t>(k)] = std::min(high, bins_ - 1);
    }

    // Tiempos automáticos de la frecuencia de cada bin (el bin 0 usa la del bin 1).
    const double hopMs = 1000.0 * hopSize() / sampleRate_;
    tangent_.assign(bins, 0.0);
    autoAttack_.assign(bins, 0.0f);
    autoRelease_.assign(bins, 0.0f);
    autoSlow_.assign(bins, 0.0f);
    for (int k = 0; k < bins_; ++k)
    {
        const auto i = static_cast<std::size_t>(k);
        tangent_[i] = responseTangent(binFrequencyHz(k), sampleRate_);
        const EnvelopeTimes times = autoEnvelopeTimes(binFrequencyHz(std::max(k, 1)));
        autoAttack_[i] = frameCoefficient(times.attackMs, hopMs);
        autoRelease_[i] = frameCoefficient(times.releaseMs, hopMs);
        autoSlow_[i] = frameCoefficient(times.slowReleaseMs, hopMs);
    }

    for (auto& channel : input_)
        channel.assign(ring, 0.0f);
    for (auto& source : sources_)
        for (auto& channel : source)
            channel.assign(ring, 0.0f);
    for (auto& channel : output_)
        channel.assign(ring, 0.0f);

    spectrum_.assign(n, Complex{});
    detector_.assign(n, Complex{});
    prefix_.assign(bins + 1, 0.0);
    for (auto& power : power_)
        power.assign(bins, 0.0f);
    for (auto& source : levels_)
        for (auto& level : source)
            level.assign(bins, 0.0f);
    for (auto& mask : masks_)
        mask.assign(bins, 0.0f);
    effective_.assign(bins, 0.0f);

    for (auto& band : bands_)
    {
        band.envelope.assign(bins, 0.0f);
        band.slowEnvelope.assign(bins, 0.0f);
        band.gains.assign(bins, 0.0f);
        band.weight.assign(bins, 0.0f);
        band.weightValid = false;
        updateBandConstants(band.live);
    }
    reset();
}

void SpectralDynamics::setBand(int index, const BandSettings& settings) noexcept ESCEQ_NONBLOCKING
{
    if (index < 0 || index >= kMaxBands)
        return;
    BandConstants& live = bands_[static_cast<std::size_t>(index)].live;
    const BandSettings next = sanitizeBand(settings);
    if (next == live.settings)
        return;
    live.settings = next;
    // La trama en curso sigue con los de antes (frame): la siguiente los toma.
    updateBandConstants(live);
}

void SpectralDynamics::updateBandConstants(BandConstants& constants) const noexcept
{
    const BandSettings& s = constants.settings;
    constants.active = s.enabled && s.dynamics.enabled && supportsDynamics(s.filter.type) && s.dynamics.rangeDb != 0.0;
    const double hopMs = 1000.0 * hopSize() / sampleRate_;
    constants.attack = frameCoefficient(s.dynamics.attackMs, hopMs);
    constants.release = frameCoefficient(s.dynamics.releaseMs, hopMs);
    const double halfKnee = 0.5 * std::max(s.dynamics.kneeDb, 0.0);
    constants.kneeStart = static_cast<float>(std::pow(10.0, (s.dynamics.thresholdDb - halfKnee) / 20.0));
}

void SpectralDynamics::reset() noexcept ESCEQ_NONBLOCKING
{
    for (auto& channel : input_)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& source : sources_)
        for (auto& channel : source)
            std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& channel : output_)
        std::fill(channel.begin(), channel.end(), 0.0f);
    sourceChannels_ = {};
    position_ = 0;
    hopPhase_ = 0;
    frames_ = 0;
    steps_ = 0;
    frameStepCount_ = 0;
    frameStepsDone_ = 0;
    for (auto& band : bands_)
    {
        band.running = false;
        band.gainDb = 0.0;
        std::fill(band.gains.begin(), band.gains.end(), 0.0f);
    }
}

bool SpectralDynamics::isActive(int index) const noexcept
{
    return index >= 0 && index < kMaxBands && bands_[static_cast<std::size_t>(index)].live.active;
}

bool SpectralDynamics::anyActive() const noexcept
{
    return std::any_of(bands_.begin(), bands_.end(), [](const Band& band) { return band.live.active; });
}

double SpectralDynamics::gainDb(int index) const noexcept
{
    return isActive(index) ? bands_[static_cast<std::size_t>(index)].gainDb : 0.0;
}

std::span<const float> SpectralDynamics::binGainsDb(int index) const noexcept
{
    if (!isActive(index))
        return {};
    return bands_[static_cast<std::size_t>(index)].gains;
}

int SpectralDynamics::applyDomain(ChannelMode mode, int numChannels) noexcept
{
    if (numChannels < 2)
    {
        // Mono: Stereo, Left y Mid procesan el único canal; Right y Side, nada.
        return mode == ChannelMode::Right || mode == ChannelMode::Side ? kNumDomains : kLeft;
    }
    switch (mode)
    {
    case ChannelMode::Stereo: return kLeft; // y kRight: ver runBand()
    case ChannelMode::Left: return kLeft;
    case ChannelMode::Right: return kRight;
    case ChannelMode::Mid: return kMid;
    case ChannelMode::Side: return kSide;
    }
    return kNumDomains;
}

int SpectralDynamics::levelFor(ChannelMode mode, int numSourceChannels) noexcept
{
    if (numSourceChannels < 2)
        return kLeft; // una fuente mono se escucha tal cual
    switch (mode)
    {
    case ChannelMode::Stereo: return kStereo;
    case ChannelMode::Left: return kLeft;
    case ChannelMode::Right: return kRight;
    case ChannelMode::Mid: return kMid;
    case ChannelMode::Side: return kSide;
    }
    return kStereo;
}

void SpectralDynamics::process(float* const* channels, int numChannels, int numSamples, const float* const* sidechain,
                               int numSidechainChannels, const float* const* detectorInput) noexcept ESCEQ_NONBLOCKING
{
    if (size_ == 0 || numSamples <= 0)
        return;
    numChannels = std::clamp(numChannels, 0, kMaxChannels);
    if (sidechain == nullptr)
        numSidechainChannels = 0;
    numSidechainChannels = std::clamp(numSidechainChannels, 0, kMaxChannels);

    const int hop = hopSize();
    const int latency = this->latency();
    int offset = 0;
    while (offset < numSamples)
    {
        // Hasta la trama siguiente, o el final del anillo (al escribir o al
        // leer la salida), o el del bloque.
        const int reading = (position_ + ringSize_ - latency) % ringSize_;
        const int length =
            std::min({hop - hopPhase_, ringSize_ - position_, ringSize_ - reading, numSamples - offset});
        pushSamples(channels, numChannels, offset, length, sidechain, numSidechainChannels, detectorInput);
        offset += length;
        position_ = (position_ + length) % ringSize_;
        hopPhase_ += length;

        // La trama anterior, tantos pasos como la parte del salto que ha
        // llegado: toda al completarlo, antes de que haga falta su salida.
        if (frameStepsDone_ < frameStepCount_)
            advanceFrame((frameStepCount_ * hopPhase_ + hop - 1) / hop);
        if (hopPhase_ == hop)
        {
            hopPhase_ = 0;
            startFrame(numChannels);
        }
    }
}

void SpectralDynamics::pushSamples(float* const* channels, int numChannels, int offset, int length,
                                   const float* const* sidechain, int numSidechainChannels,
                                   const float* const* detectorInput) noexcept
{
    const auto at = static_cast<std::size_t>(position_);
    const auto read = static_cast<std::size_t>((position_ + ringSize_ - latency()) % ringSize_);
    const auto count = static_cast<std::size_t>(length);
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        const auto c = static_cast<std::size_t>(ch);
        float* const input = input_[c].data() + at;
        float* const internal = sources_[kInternal][c].data() + at;
        float* const external = sources_[kExternal][c].data() + at;
        if (ch < numChannels)
        {
            // Primero la entrada, que la salida la sobrescribe.
            const float* in = channels[ch] + offset;
            std::copy_n(in, count, input);
            std::copy_n(detectorInput != nullptr ? detectorInput[ch] + offset : in, count, internal);
            float* const out = channels[ch] + offset;
            std::copy_n(output_[c].data() + read, count, out);
        }
        else
        {
            std::fill_n(input, count, 0.0f);
            std::fill_n(internal, count, 0.0f);
        }
        if (ch < numSidechainChannels)
            std::copy_n(sidechain[ch] + offset, count, external);
        else
            std::fill_n(external, count, 0.0f);
        std::fill_n(output_[c].data() + read, count, 0.0f);
    }
    sourceChannels_[kInternal] = std::max(numChannels, 1);
    sourceChannels_[kExternal] = numSidechainChannels;
}

void SpectralDynamics::startFrame(int numChannels) noexcept
{
    ++frames_;
    // La trama son las size_ últimas muestras, que no se sobrescriben en el
    // salto que tarda en calcularse.
    frameStart_ = (position_ + ringSize_ - size_) % ringSize_;
    frameChannels_ = numChannels;
    frameSourceChannels_ = sourceChannels_;
    frameStepCount_ = 0;
    frameStepsDone_ = 0;

    // Los ajustes con los que se calcula, y qué hace falta escuchar.
    frameNeeded_ = {};
    frameAny_ = false;
    frameSteps_[static_cast<std::size_t>(frameStepCount_++)] = kSignalStep;
    std::array<int, kMaxBands> running{};
    int numRunning = 0;
    for (int b = 0; b < kMaxBands; ++b)
    {
        Band& band = bands_[static_cast<std::size_t>(b)];
        band.frame = band.live;
        if (!band.frame.active)
        {
            if (band.running || band.gainDb != 0.0)
            {
                band.running = false;
                band.gainDb = 0.0;
                std::fill(band.gains.begin(), band.gains.end(), 0.0f);
            }
            continue;
        }
        running[static_cast<std::size_t>(numRunning++)] = b;
        if (applyDomain(band.frame.settings.channel, numChannels) == kNumDomains)
            continue;
        const Source source = band.frame.settings.dynamics.external ? kExternal : kInternal;
        const int level = levelFor(band.frame.settings.channel, frameSourceChannels_[source]);
        frameNeeded_[source][static_cast<std::size_t>(level)] = true;
        frameAny_ = true;
    }
    if (frameAny_)
    {
        for (int source = 0; source < kNumSources; ++source)
        {
            const auto& need = frameNeeded_[static_cast<std::size_t>(source)];
            if (std::any_of(need.begin(), need.end(), [](bool b) { return b; }))
                frameSteps_[static_cast<std::size_t>(frameStepCount_++)] = kFirstSourceStep - source;
        }
        for (int i = 0; i < numRunning; ++i)
            frameSteps_[static_cast<std::size_t>(frameStepCount_++)] = running[static_cast<std::size_t>(i)];
        for (int domain = 0; domain < kNumDomains; ++domain)
            frameSteps_[static_cast<std::size_t>(frameStepCount_++)] = kFirstMaskStep - domain;
    }
    frameSteps_[static_cast<std::size_t>(frameStepCount_++)] = kSynthesisStep;
}

void SpectralDynamics::advanceFrame(int target) noexcept
{
    target = std::min(target, frameStepCount_);
    while (frameStepsDone_ < target)
    {
        runStep(frameSteps_[static_cast<std::size_t>(frameStepsDone_++)]);
        ++steps_;
    }
}

void SpectralDynamics::runStep(int step) noexcept
{
    if (step == kSignalStep)
        analyseSignal();
    else if (!frameFinite_)
        return; // esta trama no suena; las que no contengan el no finito, sí
    else if (step == kSynthesisStep)
        synthesise();
    else if (step <= kFirstMaskStep)
        convertMask(kFirstMaskStep - step);
    else if (step <= kFirstSourceStep)
    {
        const auto source = static_cast<Source>(kFirstSourceStep - step);
        analyseSource(source, frameSourceChannels_[source], frameNeeded_[source]);
    }
    else
        runBand(bands_[static_cast<std::size_t>(step)], frameChannels_);
}

void SpectralDynamics::analyseSignal() noexcept
{
    const auto n = static_cast<std::size_t>(size_);
    const auto start = static_cast<std::size_t>(frameStart_);
    const auto wrap = static_cast<std::size_t>(ringSize_ - 1);

    // La señal: L en la parte real y R en la imaginaria, con la raíz de Hann.
    {
        const float* const left = input_[0].data();
        const float* const right = input_[1].data();
        float check = 0.0f;
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t j = (start + i) & wrap;
            const float w = analysisWindow_[i];
            spectrum_[i] = Complex{left[j] * w, right[j] * w};
            check += std::abs(left[j]) + std::abs(right[j]);
        }
        frameFinite_ = std::isfinite(check);
    }
    if (!frameFinite_)
        return;
    fft_->forward(spectrum_.data());

    maskUsed_ = {};
    if (frameAny_)
    {
        sourceReset_ = {};
        for (auto& mask : masks_)
            std::fill(mask.begin(), mask.end(), 0.0f);
    }
}

void SpectralDynamics::synthesise() noexcept
{
    applyMasks(frameChannels_);

    fft_->inverse(spectrum_.data());

    // Síntesis con la misma ventana: la suma solapada de w² es 2, y la inversa multiplica por N.
    const auto n = static_cast<std::size_t>(size_);
    const auto start = static_cast<std::size_t>(frameStart_);
    const auto wrap = static_cast<std::size_t>(ringSize_ - 1);
    const float scale = 1.0f / (2.0f * static_cast<float>(n));
    float* const left = output_[0].data();
    float* const right = output_[1].data();
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t j = (start + i) & wrap;
        const float w = analysisWindow_[i] * scale;
        left[j] += spectrum_[i].real() * w;
        right[j] += spectrum_[i].imag() * w;
    }
}

void SpectralDynamics::analyseSource(Source source, int numSourceChannels,
                                     const std::array<bool, kNumLevels>& needed) noexcept
{
    const auto n = static_cast<std::size_t>(size_);
    const auto start = static_cast<std::size_t>(frameStart_);
    const auto wrap = static_cast<std::size_t>(ringSize_ - 1);
    const auto& rings = sources_[source];
    auto& levels = levels_[source];

    // Las dos señales de la fuente en una FFT compleja, con la ventana de Hann.
    float check = 0.0f;
    for (std::size_t i = 0; i < n; ++i)
    {
        const std::size_t j = (start + i) & wrap;
        const float w = detectorWindow_[i];
        const float a = rings[0][j];
        const float b = numSourceChannels > 1 ? rings[1][j] : 0.0f;
        detector_[i] = Complex{a * w, b * w};
        check += std::abs(a) + std::abs(b);
    }
    if (!std::isfinite(check))
    {
        // El detector no llega a la salida: sin esto, un NaN del sidechain
        // dejaría la banda parada. Se escucha silencio y se reinicia.
        sourceReset_[source] = true;
        for (auto& level : levels)
            std::fill(level.begin(), level.end(), 0.0f);
        return;
    }
    fft_->forward(detector_.data());

    const bool stereo = numSourceChannels > 1;
    const bool wantLeft = needed[kLeft] || needed[kStereo];
    const bool wantRight = stereo && (needed[kRight] || needed[kStereo]);
    const bool wantMid = stereo && needed[kMid];
    const bool wantSide = stereo && needed[kSide];
    for (int k = 0; k < bins_; ++k)
    {
        const auto i = static_cast<std::size_t>(k);
        const Complex z = detector_[i];
        const Complex mirror = std::conj(detector_[(n - i) & (n - 1)]);
        // A = (Z[k] + conj Z[N−k])/2, B = −i·(Z[k] − conj Z[N−k])/2.
        const Complex a = 0.5f * (z + mirror);
        const Complex d = z - mirror;
        const Complex b{0.5f * d.imag(), -0.5f * d.real()};
        if (wantLeft)
            power_[kLeft][i] = std::norm(a);
        if (wantRight)
            power_[kRight][i] = std::norm(b);
        if (wantMid)
            power_[kMid][i] = std::norm(0.5f * (a + b));
        if (wantSide)
            power_[kSide][i] = std::norm(0.5f * (a - b));
    }

    // Nivel de cada bin: la energía de su 1/6 de octava, calibrada para que un
    // seno lea su pico: |X| = A·N/4 en el centro y 1,5 veces eso en energía.
    const double calibration = 16.0 / (static_cast<double>(n) * static_cast<double>(n) * kHannLobeEnergy);
    const auto smooth = [&](int which) {
        const auto& power = power_[static_cast<std::size_t>(which)];
        auto& level = levels[static_cast<std::size_t>(which)];
        prefix_[0] = 0.0;
        for (std::size_t k = 0; k < static_cast<std::size_t>(bins_); ++k)
            prefix_[k + 1] = prefix_[k] + static_cast<double>(power[k]);
        for (std::size_t k = 0; k < static_cast<std::size_t>(bins_); ++k)
        {
            const auto low = static_cast<std::size_t>(detectorLow_[k]);
            const auto high = static_cast<std::size_t>(detectorHigh_[k]);
            const double energy = std::max(prefix_[high + 1] - prefix_[low], 0.0);
            level[k] = static_cast<float>(std::sqrt(energy * calibration));
        }
    };
    if (wantLeft)
        smooth(kLeft);
    if (wantRight)
        smooth(kRight);
    if (wantMid)
        smooth(kMid);
    if (wantSide)
        smooth(kSide);
    if (needed[kStereo] && stereo)
    {
        auto& out = levels[kStereo];
        for (std::size_t k = 0; k < static_cast<std::size_t>(bins_); ++k)
            out[k] = std::max(levels[kLeft][k], levels[kRight][k]);
    }
}

void SpectralDynamics::updateWeight(Band& band) noexcept
{
    const FilterParams key = weightKey(band.frame.settings.filter);
    if (band.weightValid && key == band.weightFor)
        return;
    band.weightFor = key;
    band.weightValid = true;

    const BandDesign design = designDetector(band.frame.settings.filter, sampleRate_);
    if (design.numSections == 0)
    {
        std::fill(band.weight.begin(), band.weight.end(), 1.0f); // tilt: todo el espectro
        return;
    }
    double peak = 0.0;
    for (std::size_t k = 0; k < static_cast<std::size_t>(bins_); ++k)
    {
        const double magnitude = std::abs(responseAtTangent(design, tangent_[k]));
        band.weight[k] = static_cast<float>(magnitude);
        peak = std::max(peak, magnitude);
    }
    const float normalise = peak > 0.0 ? static_cast<float>(1.0 / peak) : 0.0f;
    for (float& w : band.weight)
        w *= normalise;
}

void SpectralDynamics::runBand(Band& band, int numChannels) noexcept
{
    const BandConstants& constants = band.frame;
    const int domain = applyDomain(constants.settings.channel, numChannels);
    if (domain == kNumDomains)
    {
        band.gainDb = 0.0;
        std::fill(band.gains.begin(), band.gains.end(), 0.0f);
        return;
    }
    updateWeight(band);

    const DynamicSettings& dyn = constants.settings.dynamics;
    const Source source = dyn.external ? kExternal : kInternal;
    const int which = levelFor(constants.settings.channel, frameSourceChannels_[source]);
    const float* const level = levels_[source][static_cast<std::size_t>(which)].data();
    if (!band.running || sourceReset_[source])
    {
        std::fill(band.envelope.begin(), band.envelope.end(), 0.0f);
        std::fill(band.slowEnvelope.begin(), band.slowEnvelope.end(), 0.0f);
        band.running = true;
    }

    // Tres bucles sin ramas, que el compilador vectoriza: la envolvente, la
    // curva de ganancia y lo que se suma a la máscara.
    const auto bins = static_cast<std::size_t>(bins_);
    float* const envelope = band.envelope.data();
    float* const effective = effective_.data();
    if (dyn.autoTiming)
    {
        float* const slow = band.slowEnvelope.data();
        const float* const autoAttack = autoAttack_.data();
        const float* const autoRelease = autoRelease_.data();
        const float* const autoSlow = autoSlow_.data();
        for (std::size_t k = 0; k < bins; ++k)
        {
            const float in = level[k];
            const float previous = envelope[k];
            const float attack = autoAttack[k];
            const float release = autoRelease[k];
            const float env = in + (in > previous ? attack : release) * (previous - in);
            const float held = env + autoSlow[k] * (slow[k] - env);
            envelope[k] = env;
            slow[k] = held;
            effective[k] = env > held ? env : held;
        }
    }
    else
    {
        const float attack = constants.attack;
        const float release = constants.release;
        for (std::size_t k = 0; k < bins; ++k)
        {
            const float in = level[k];
            const float env = in + (in > envelope[k] ? attack : release) * (envelope[k] - in);
            envelope[k] = env;
            effective[k] = env;
        }
    }

    // En reposo (nada llega a la rodilla), la banda no hace nada: lo habitual
    // con el umbral bien puesto, y así cuesta solo la envolvente.
    float loudest = 0.0f;
    for (std::size_t k = 0; k < bins; ++k)
        loudest = effective[k] > loudest ? effective[k] : loudest;
    float* const gains = band.gains.data();
    if (!(loudest > constants.kneeStart))
    {
        std::fill_n(gains, bins, 0.0f);
        band.gainDb = 0.0;
        return;
    }

    // La curva de dynamicGainDb(), en float: 0 hasta el principio de la
    // rodilla y, desde ahí, el exceso redondeado por la rodilla hasta el rango.
    const auto threshold = static_cast<float>(dyn.thresholdDb);
    const auto knee = static_cast<float>(dyn.kneeDb);
    const float halfKnee = 0.5f * knee;
    const float inverseTwoKnees = knee > 0.0f ? 1.0f / (2.0f * knee) : 0.0f;
    const auto limit = static_cast<float>(std::abs(dyn.rangeDb));
    const float sign = dyn.rangeDb < 0.0 ? -1.0f : 1.0f;
    const float kneeStart = constants.kneeStart;
    const float* const weight = band.weight.data();
    for (std::size_t k = 0; k < bins; ++k)
    {
        const float env = effective[k];
        const float over = decibels(env) - threshold;
        const float rounded = (over + halfKnee) * (over + halfKnee) * inverseTwoKnees;
        const float positive = over > 0.0f ? over : 0.0f;
        const float amount = std::abs(over) < halfKnee ? rounded : positive;
        const float gain = sign * (amount < limit ? amount : limit) * weight[k];
        gains[k] = env > kneeStart ? gain : 0.0f;
    }

    float* const mask = masks_[static_cast<std::size_t>(domain)].data();
    for (std::size_t k = 0; k < bins; ++k)
        mask[k] += gains[k];
    if (constants.settings.channel == ChannelMode::Stereo && numChannels > 1)
    {
        float* const maskRight = masks_[kRight].data();
        for (std::size_t k = 0; k < bins; ++k)
            maskRight[k] += gains[k];
        maskUsed_[kRight] = true;
    }
    maskUsed_[static_cast<std::size_t>(domain)] = true;

    float extreme = 0.0f;
    for (std::size_t k = 0; k < bins; ++k)
        if (std::abs(gains[k]) > std::abs(extreme))
            extreme = gains[k];
    band.gainDb = extreme;
}

void SpectralDynamics::convertMask(int domain) noexcept
{
    // Un paso por dominio: es lo que más cuesta de la síntesis (un pow por bin).
    if (!maskUsed_[static_cast<std::size_t>(domain)])
        return;
    for (float& g : masks_[static_cast<std::size_t>(domain)])
        g = g == 0.0f ? 1.0f : static_cast<float>(std::pow(10.0, static_cast<double>(g) / 20.0));
}

void SpectralDynamics::applyMasks(int numChannels) noexcept
{
    // Las máscaras ya son lineales (convertMask()).
    const bool lr = maskUsed_[kLeft] || maskUsed_[kRight];
    const bool ms = maskUsed_[kMid] || maskUsed_[kSide];
    if (!lr && !ms)
        return;

    const auto n = static_cast<std::size_t>(size_);
    const auto half = n / 2;
    const float* const gl = masks_[kLeft].data();
    const float* const gr = masks_[kRight].data();
    const float* const gm = masks_[kMid].data();
    const float* const gs = masks_[kSide].data();

    // Mono, o solo bandas Stereo: los dos canales reciben la misma ganancia
    // real y simétrica, que se aplica sin separarlos.
    const bool same = numChannels < 2 || (!ms && maskUsed_[kLeft] && maskUsed_[kRight] &&
                                          std::equal(masks_[kLeft].begin(), masks_[kLeft].end(),
                                                     masks_[kRight].begin()));
    if (same)
    {
        spectrum_[0] *= gl[0];
        for (std::size_t k = 1; k < half; ++k)
        {
            spectrum_[k] *= gl[k];
            spectrum_[n - k] *= gl[k];
        }
        spectrum_[half] *= gl[half];
        return;
    }

    for (std::size_t k = 0; k <= half; ++k)
    {
        const Complex z = spectrum_[k];
        const Complex mirror = std::conj(spectrum_[(n - k) & (n - 1)]);
        const Complex d = z - mirror;
        Complex left = 0.5f * (z + mirror);
        Complex right{0.5f * d.imag(), -0.5f * d.real()};
        if (lr)
        {
            left *= maskUsed_[kLeft] ? gl[k] : 1.0f;
            right *= maskUsed_[kRight] ? gr[k] : 1.0f;
        }
        if (ms)
        {
            const Complex mid = 0.5f * (left + right) * (maskUsed_[kMid] ? gm[k] : 1.0f);
            const Complex side = 0.5f * (left - right) * (maskUsed_[kSide] ? gs[k] : 1.0f);
            left = mid + side;
            right = mid - side;
        }
        // Y[k] = L + i·R; Y[N−k] = conj(L) + i·conj(R).
        spectrum_[k] = Complex{left.real() - right.imag(), left.imag() + right.real()};
        if (k != 0 && k != half)
        {
            const Complex l = std::conj(left);
            const Complex r = std::conj(right);
            spectrum_[n - k] = Complex{l.real() - r.imag(), l.imag() + r.real()};
        }
    }
}

} // namespace esceq::dsp
