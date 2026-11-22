// SPDX-License-Identifier: MIT
#include "esceq/dsp/saturation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>

namespace esceq::dsp {

namespace {

// Coeficientes de los filtros de media banda (docs/dsp/saturacion.md).
//
// IIR: H(z) = (A0(z²) + z⁻¹·A1(z²)) / 2, con A0 y A1 cascadas de secciones
// pasa todo (a + z⁻²) / (1 + a·z⁻²): los coeficientes pares van a A0 y los
// impares a A1. Diseño elíptico de Valenzuela y Constantinides (1983), con la
// banda de transición centrada en un cuarto de la frecuencia de dentro: la
// primera etapa con una semianchura de 0,045 (plana hasta 0,41·fs, −103 dB
// desde 0,59·fs) y las demás con 0,1025 (plana hasta 0,59·fs, −105 dB desde 1,41·fs).
constexpr std::array<float, Oversampler::kIirFirstCoefficients> kIirFirst = {
    0.038059051f, 0.14173922f, 0.28551681f, 0.44157277f, 0.58993798f, 0.72178786f, 0.83782998f, 0.94551747f};
constexpr std::array<float, Oversampler::kIirCoefficients> kIir = {0.038501175f, 0.14517984f, 0.29891647f,
                                                                   0.47805835f,  0.67083688f, 0.88133950f};

// Fase lineal: FIR de media banda de 4K − 1 coeficientes (Parks-McClellan con
// las mismas bandas): el central vale 1/2, los de distancia par al central,
// 0, y aquí están los de distancia 1, 3, 5... (simétricos).
constexpr std::array<float, Oversampler::kFirFirstHalfLength> kFirFirst = {
    0.31716672f,     -0.10271805f,    0.058163988f,     -0.03806658f,      0.026317596f,   -0.01854829f,
    0.013082691f,    -0.0091287137f,  0.0062483876f,    -0.0041653577f,    0.0026855727f,  -0.001661907f,
    0.00097805014f,  -0.00054073573f, 0.00027593341f,   -0.00012628433f,   4.9128074e-05f, -1.4681638e-05f};
constexpr std::array<float, Oversampler::kFirHalfLength> kFir = {
    0.31265039f, -0.090185256f, 0.040275958f, -0.018169589f, 0.0073879128f, -0.0025001638f, 0.00063192594f,
    -9.2613656e-05f};

/// Coeficientes IIR de una etapa.
std::span<const float> iirCoefficients(int stage) noexcept
{
    return stage == 0 ? std::span<const float>(kIirFirst) : std::span<const float>(kIir);
}

/// Los 2K coeficientes de la rama par de un FIR de media banda, en el orden
/// de la memoria (la muestra más antigua primero): las distancias 2K − 1, ...,
/// 3, 1 al central y luego 1, 3, ..., 2K − 1.
template <std::size_t K>
constexpr std::array<float, 2 * K> evenTaps(const std::array<float, K>& half) noexcept
{
    std::array<float, 2 * K> taps{};
    for (std::size_t m = 0; m < K; ++m)
    {
        taps[m] = half[K - 1 - m];
        taps[K + m] = half[m];
    }
    return taps;
}

constexpr auto kFirFirstTaps = evenTaps(kFirFirst);
constexpr auto kFirTaps = evenTaps(kFir);

std::span<const float> firTaps(int stage) noexcept
{
    return stage == 0 ? std::span<const float>(kFirFirstTaps) : std::span<const float>(kFirTaps);
}

/// Una rama IIR: las secciones con los coeficientes first, first + 2... `state`
/// guarda la entrada anterior de la rama y la salida anterior de cada sección
/// (la salida de una es la entrada de la siguiente).
template <std::size_t N>
inline float allpassBranch(std::span<const float> coefficients, int first, std::array<float, N>& state,
                           float input) noexcept
{
    std::size_t k = 0;
    for (std::size_t c = static_cast<std::size_t>(first); c < coefficients.size(); c += 2, ++k)
    {
        const float output = coefficients[c] * (input - state[k + 1]) + state[k];
        state[k] = input;
        input = output;
    }
    state[k] = input;
    return input;
}

/// tanh en float, sin ramas (para que el compilador la vectorice) y el
/// mismo resultado en todos los sistemas: impar exacta y error de 1,1e-7 como
/// mucho. Por encima de 0,25, (1 − e)/(1 + e) con e = e^(−2|x|) = 2^k·2^f
/// (|f| ≤ 1/2, Taylor de grado 7); por debajo, la serie de Taylor hasta x⁹.
inline float fastTanh(float x) noexcept
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(x);
    float a = std::bit_cast<float>(bits & 0x7fffffffu);
    a = a < 9.0f ? a : 9.0f; // tanh(9) es 1 en float.
    const float t = -2.88539008f * a; // −2·log2(e)·a
    const float shifted = t + 12582912.0f; // 1,5·2^23: redondea t al entero más cercano
    const float kf = shifted - 12582912.0f;
    const std::int32_t k = std::bit_cast<std::int32_t>(shifted) - 0x4B400000;
    const float y = (t - kf) * 0.693147181f;
    const float p =
        1.0f + y * (1.0f + y * (0.5f + y * (1.0f / 6.0f + y * (1.0f / 24.0f + y * (1.0f / 120.0f +
                                                                                 y * (1.0f / 720.0f + y / 5040.0f))))));
    const float e = p * std::bit_cast<float>(static_cast<std::uint32_t>(k + 127) << 23);
    const float large = (1.0f - e) / (1.0f + e);
    const float a2 = a * a;
    const float small =
        a * (1.0f + a2 * (-1.0f / 3.0f + a2 * (2.0f / 15.0f + a2 * (-17.0f / 315.0f + a2 * (62.0f / 2835.0f)))));
    const float r = a < 0.25f ? small : large;
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(r) | (bits & 0x80000000u));
}

/// sums[i] = Σ taps[m]·samples[i + m], con m creciente: el mismo orden de
/// sumas en cada salida, y el bucle de dentro sobre i, que el compilador vectoriza.
inline void convolve(std::span<const float> taps, const float* samples, float* sums, int numOutputs) noexcept
{
    std::fill_n(sums, numOutputs, 0.0f);
    for (std::size_t m = 0; m < taps.size(); ++m)
    {
        const float tap = taps[m];
        const float* from = samples + m;
        for (int i = 0; i < numOutputs; ++i)
            sums[i] += tap * from[i];
    }
}

} // namespace

// --- Oversampler ---

int Oversampler::latencySamples(OversamplingFilter filter, int stages) noexcept
{
    if (filter != OversamplingFilter::LinearPhase)
        return 0;
    stages = std::clamp(stages, 1, kMaxStages);
    const int factor = 1 << stages;
    return (firDelay(stages) + factor - 1) / factor;
}

int Oversampler::firDelay(int stages) noexcept
{
    // Cada etapa k (0, la primera) retrasa al subir y al bajar 2K − 1
    // muestras a la frecuencia de su salida, 2^(k+1)·fs.
    const int factor = 1 << stages;
    int delay = 0;
    for (int k = 0; k < stages; ++k)
        delay += 2 * (static_cast<int>(firTaps(k).size()) - 1) * (factor >> (k + 1));
    return delay;
}

int Oversampler::padLength() const noexcept
{
    return filter_ == OversamplingFilter::LinearPhase ? latency() * factor() - firDelay(stages_) : 0;
}

void Oversampler::setup(OversamplingFilter filter, int stages) noexcept ESCEQ_NONBLOCKING
{
    filter_ = filter;
    stages_ = std::clamp(stages, 1, kMaxStages);
    reset();
}

void Oversampler::reset() noexcept ESCEQ_NONBLOCKING
{
    channels_ = {};
}

bool Oversampler::isFinite() const noexcept
{
    const auto finite = [](const auto& values) {
        return std::ranges::all_of(values, [](float v) { return std::isfinite(v); });
    };
    for (const Channel& channel : channels_)
    {
        for (int k = 0; k < stages_; ++k)
        {
            const auto s = static_cast<std::size_t>(k);
            if (!finite(channel.iirUp[s].even) || !finite(channel.iirUp[s].odd) ||
                !finite(channel.iirDown[s].even) || !finite(channel.iirDown[s].odd) ||
                !std::isfinite(channel.iirDown[s].oddOutput) || !finite(channel.fir[s].up) ||
                !finite(channel.fir[s].downEven) || !finite(channel.fir[s].downOdd))
                return false;
        }
        if (!finite(channel.pad))
            return false;
    }
    return true;
}

template <int NumChannels>
void Oversampler::iirUp(int stage, const float* const* input, float* const* output, int numSamples) noexcept
{
    // Los canales en el mismo bucle: sus recurrencias se solapan.
    const std::span<const float> c = iirCoefficients(stage);
    const auto s = static_cast<std::size_t>(stage);
    for (int i = 0; i < numSamples; ++i)
        for (int ch = 0; ch < NumChannels; ++ch)
        {
            IirStage& state = channels_[static_cast<std::size_t>(ch)].iirUp[s];
            output[ch][2 * i] = allpassBranch(c, 0, state.even, input[ch][i]);
            output[ch][2 * i + 1] = allpassBranch(c, 1, state.odd, input[ch][i]);
        }
}

template <int NumChannels>
void Oversampler::iirDown(int stage, const float* const* input, float* const* output, int numSamples) noexcept
{
    const std::span<const float> c = iirCoefficients(stage);
    const auto s = static_cast<std::size_t>(stage);
    for (int i = 0; i < numSamples; ++i)
        for (int ch = 0; ch < NumChannels; ++ch)
        {
            IirStage& state = channels_[static_cast<std::size_t>(ch)].iirDown[s];
            const float even = allpassBranch(c, 0, state.even, input[ch][2 * i]);
            output[ch][i] = 0.5f * (even + state.oddOutput);
            state.oddOutput = allpassBranch(c, 1, state.odd, input[ch][2 * i + 1]);
        }
}

void Oversampler::firUp(int channel, int stage, const float* input, float* output, int numSamples) noexcept
{
    // Ceros intercalados y el FIR, con ganancia 2: la salida impar es el
    // coeficiente central (la entrada retrasada K − 1) y la par, los demás.
    const std::span<const float> taps = firTaps(stage);
    const int history = static_cast<int>(taps.size()) - 1;
    const int half = static_cast<int>(taps.size()) / 2;
    auto& memory = channels_[static_cast<std::size_t>(channel)].fir[static_cast<std::size_t>(stage)].up;
    std::copy_n(memory.begin(), history, work_.begin());
    std::copy_n(input, numSamples, work_.begin() + history);
    convolve(taps, work_.data(), sums_.data(), numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        output[2 * i] = 2.0f * sums_[static_cast<std::size_t>(i)];
        output[2 * i + 1] = work_[static_cast<std::size_t>(i + half)];
    }
    std::copy_n(work_.begin() + numSamples, history, memory.begin());
}

void Oversampler::firDown(int channel, int stage, const float* input, float* output, int numSamples) noexcept
{
    // Las muestras pares van por los coeficientes de distancia impar al
    // central, y las impares por el central (retrasadas K).
    const std::span<const float> taps = firTaps(stage);
    const int history = static_cast<int>(taps.size()) - 1;
    const int half = static_cast<int>(taps.size()) / 2;
    FirStage& fir = channels_[static_cast<std::size_t>(channel)].fir[static_cast<std::size_t>(stage)];
    std::copy_n(fir.downEven.begin(), history, work_.begin());
    std::copy_n(fir.downOdd.begin(), half, workOdd_.begin());
    for (int i = 0; i < numSamples; ++i)
    {
        work_[static_cast<std::size_t>(history + i)] = input[2 * i];
        workOdd_[static_cast<std::size_t>(half + i)] = input[2 * i + 1];
    }
    convolve(taps, work_.data(), sums_.data(), numSamples);
    for (int i = 0; i < numSamples; ++i)
        output[i] = sums_[static_cast<std::size_t>(i)] + 0.5f * workOdd_[static_cast<std::size_t>(i)];
    std::copy_n(work_.begin() + numSamples, history, fir.downEven.begin());
    std::copy_n(workOdd_.begin() + numSamples, half, fir.downOdd.begin());
}

void Oversampler::upsample(const float* const* input, float* const* output, int numChannels,
                           int numSamples) noexcept ESCEQ_NONBLOCKING
{
    numChannels = std::clamp(numChannels, 0, kMaxChannels);
    numSamples = std::clamp(numSamples, 0, kMaxBlock);
    // Las etapas se alternan entre `output` y scratch_, de forma que la última escriba en `output`.
    std::array<const float*, kMaxChannels> from{};
    std::array<float*, kMaxChannels> to{};
    for (int ch = 0; ch < numChannels; ++ch)
        from[static_cast<std::size_t>(ch)] = input[ch];
    int length = numSamples;
    for (int k = 0; k < stages_; ++k)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            to[static_cast<std::size_t>(ch)] =
                (stages_ - 1 - k) % 2 == 0 ? output[ch] : scratch_[static_cast<std::size_t>(ch)].data();
        if (filter_ == OversamplingFilter::Iir)
        {
            if (numChannels == 2)
                iirUp<2>(k, from.data(), to.data(), length);
            else if (numChannels == 1)
                iirUp<1>(k, from.data(), to.data(), length);
        }
        else
            for (int ch = 0; ch < numChannels; ++ch)
                firUp(ch, k, from[static_cast<std::size_t>(ch)], to[static_cast<std::size_t>(ch)], length);
        for (int ch = 0; ch < numChannels; ++ch)
            from[static_cast<std::size_t>(ch)] = to[static_cast<std::size_t>(ch)];
        length *= 2;
    }
}

void Oversampler::downsample(float* const* input, float* const* output, int numChannels,
                             int numSamples) noexcept ESCEQ_NONBLOCKING
{
    numChannels = std::clamp(numChannels, 0, kMaxChannels);
    numSamples = std::clamp(numSamples, 0, kMaxBlock);
    int length = numSamples << stages_;
    const int pad = padLength();
    for (int ch = 0; ch < numChannels && pad > 0; ++ch)
    {
        // Retardo de `pad` muestras a la frecuencia de dentro, en el sitio.
        Channel& state = channels_[static_cast<std::size_t>(ch)];
        float* samples = input[ch];
        for (int i = 0; i < length; ++i)
        {
            const auto p = static_cast<std::size_t>(state.padPosition);
            const float delayed = state.pad[p];
            state.pad[p] = samples[i];
            samples[i] = delayed;
            state.padPosition = state.padPosition + 1 == pad ? 0 : state.padPosition + 1;
        }
    }
    // Cada etapa escribe en el sitio (la muestra i sale de la 2i y la 2i + 1), y la última, en `output`.
    for (int k = stages_ - 1; k >= 0; --k)
    {
        length /= 2;
        float* const* to = k == 0 ? output : input;
        if (filter_ == OversamplingFilter::Iir)
        {
            if (numChannels == 2)
                iirDown<2>(k, input, to, length);
            else if (numChannels == 1)
                iirDown<1>(k, input, to, length);
        }
        else
            for (int ch = 0; ch < numChannels; ++ch)
                firDown(ch, k, input[ch], to[ch], length);
    }
}

// --- Saturation ---

void Saturation::Ramp::moveTo(double v, int samples) noexcept
{
    target = v;
    if (v == value)
    {
        remaining = 0;
        step = 0.0;
        return;
    }
    remaining = std::max(1, samples);
    step = (target - value) / remaining;
}

int Saturation::stagesFor(double sampleRate) noexcept
{
    const int factor = sampleRateFactor(sampleRate);
    return factor >= 4 ? 1 : factor == 2 ? 2 : 3;
}

double Saturation::compensationGain(SaturationMode mode, double driveDb) noexcept ESCEQ_NONBLOCKING
{
    if (mode == SaturationMode::Off)
        return 1.0;
    const double drive = std::pow(10.0, std::clamp(driveDb, 0.0, kMaxDriveDb) / 20.0);
    const double bias = mode == SaturationMode::Warm ? kWarmBias : 0.0;
    const double biasTanh = std::tanh(bias);
    const double amplitude = std::pow(10.0, kCalibrationDbfs / 20.0);
    double sum = 0.0;
    double sumSquares = 0.0;
    for (int k = 0; k < kCalibrationPoints; ++k)
    {
        const double phase = 2.0 * std::numbers::pi * (k + 0.5) / kCalibrationPoints;
        const double y = std::tanh(drive * amplitude * std::sin(phase) + bias) - biasTanh;
        sum += y;
        sumSquares += y * y;
    }
    const double mean = sum / kCalibrationPoints;
    const double rms = std::sqrt(std::max(0.0, sumSquares / kCalibrationPoints - mean * mean));
    return rms / (amplitude / std::numbers::sqrt2);
}

Saturation::Saturation() noexcept
{
    oversampler_.setup(OversamplingFilter::Iir, stagesFor(sampleRate_));
    mix_.jump(1.0);
    prepare(sampleRate_);
}

void Saturation::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 && std::isfinite(sampleRate) ? sampleRate : 48000.0;
    rampSamples_ = std::max(1, static_cast<int>(std::lround(kRampSeconds * sampleRate_)));
    if (oversampler_.stages() != stagesFor(sampleRate_))
        oversampler_.setup(oversampler_.filter(), stagesFor(sampleRate_));
    dcCoefficient_ =
        std::exp(-2.0 * std::numbers::pi * kDcBlockerHz / (oversampler_.factor() * sampleRate_));
    drive_.jump(driveDb_);
    bias_.jump(curve_ == SaturationMode::Warm ? kWarmBias : 0.0);
    updateCompensation();
    reset();
}

void Saturation::clearMemory() noexcept
{
    oversampler_.reset();
    dcBlockers_ = {};
}

void Saturation::reset() noexcept ESCEQ_NONBLOCKING
{
    clearMemory();
    drive_.jump(drive_.target);
    compensation_.jump(compensation_.target);
    mix_.jump(mix_.target);
    bias_.jump(bias_.target);
    fade_.jump(mode_ == SaturationMode::Off ? 0.0 : 1.0);
}

void Saturation::updateCompensation() noexcept
{
    const double target = -20.0 * std::log10(compensationGain(curve_, driveDb_));
    if (isActive())
        compensation_.moveTo(target, rampSamples_);
    else
        compensation_.jump(target);
}

void Saturation::setMode(SaturationMode mode) noexcept ESCEQ_NONBLOCKING
{
    if (mode == mode_ || mode > SaturationMode::Warm)
        return;
    const bool wasActive = isActive();
    mode_ = mode;
    if (mode == SaturationMode::Off)
    {
        fade_.moveTo(0.0, rampSamples_);
        return;
    }
    curve_ = mode;
    const double bias = mode == SaturationMode::Warm ? kWarmBias : 0.0;
    if (!wasActive)
    {
        // Empieza de cero, con la curva nueva sin rampa, y sube.
        clearMemory();
        bias_.jump(bias);
        drive_.jump(drive_.target);
        mix_.jump(mix_.target);
        compensation_.jump(-20.0 * std::log10(compensationGain(curve_, driveDb_)));
    }
    else
    {
        bias_.moveTo(bias, rampSamples_);
        updateCompensation();
    }
    fade_.moveTo(1.0, rampSamples_);
}

void Saturation::setDriveDb(double driveDb) noexcept ESCEQ_NONBLOCKING
{
    if (!std::isfinite(driveDb))
        return;
    driveDb = std::clamp(driveDb, 0.0, kMaxDriveDb);
    if (driveDb == driveDb_)
        return;
    driveDb_ = driveDb;
    if (isActive())
        drive_.moveTo(driveDb, rampSamples_);
    else
        drive_.jump(driveDb);
    updateCompensation();
}

void Saturation::setMix(double mix) noexcept ESCEQ_NONBLOCKING
{
    if (!std::isfinite(mix))
        return;
    mix = std::clamp(mix, 0.0, 1.0);
    if (mix == mix_.target)
        return;
    if (isActive())
        mix_.moveTo(mix, rampSamples_);
    else
        mix_.jump(mix);
}

void Saturation::setFilter(OversamplingFilter filter) noexcept ESCEQ_NONBLOCKING
{
    if (filter == oversampler_.filter() || filter > OversamplingFilter::LinearPhase)
        return;
    oversampler_.setup(filter, stagesFor(sampleRate_));
    dcBlockers_ = {};
}

void Saturation::process(float* const* channels, int numChannels, int numSamples) noexcept ESCEQ_NONBLOCKING
{
    numChannels = std::min(numChannels, kMaxChannels);
    for (int offset = 0; offset < numSamples && isActive(); offset += Oversampler::kMaxBlock)
        processChunk(channels, numChannels, offset, std::min(Oversampler::kMaxBlock, numSamples - offset));
}

void Saturation::processChunk(float* const* channels, int numChannels, int offset, int length) noexcept
{
    // Los ajustes de cada muestra de fuera, iguales en sus muestras de dentro.
    bool fading = false;
    int active = length; // Muestras hasta que termina el fundido hacia Off (el resto, la entrada tal cual).
    for (int i = 0; i < length; ++i)
    {
        Frame& frame = frames_[static_cast<std::size_t>(i)];
        if (bias_.value != cache_.bias)
        {
            cache_.bias = bias_.value;
            cache_.biasTanh = static_cast<float>(std::tanh(cache_.bias));
        }
        if (drive_.value != cache_.driveDb)
        {
            cache_.driveDb = drive_.value;
            cache_.drive = static_cast<float>(std::pow(10.0, cache_.driveDb / 20.0));
        }
        if (compensation_.value != cache_.compensationDb)
        {
            cache_.compensationDb = compensation_.value;
            cache_.compensation = static_cast<float>(std::pow(10.0, cache_.compensationDb / 20.0));
        }
        frame = {cache_.drive,
                 cache_.compensation,
                 static_cast<float>(mix_.value),
                 static_cast<float>(bias_.value),
                 cache_.biasTanh,
                 static_cast<float>(fade_.value)};
        fading = fading || fade_.value < 1.0;
        drive_.advance();
        compensation_.advance();
        mix_.advance();
        bias_.advance();
        fade_.advance();
        if (mode_ == SaturationMode::Off && frame.fade <= 0.0f && active == length)
            active = i;
    }
    if (active == 0)
    {
        clearMemory();
        return;
    }

    const int factor = oversampler_.factor();
    const int inner = active * factor;
    std::array<float*, kMaxChannels> io{};
    std::array<float*, kMaxChannels> up{};
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto c = static_cast<std::size_t>(ch);
        io[c] = channels[ch] + offset;
        up[c] = oversampled_[c].data();
        std::copy_n(io[c], active, dry_[c].begin());
    }
    oversampler_.upsample(io.data(), up.data(), numChannels, active);

    // La curva (sin dependencias entre muestras: el compilador la vectoriza).
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto c = static_cast<std::size_t>(ch);
        const float* x = up[c];
        float* wet = wet_[c].data();
        for (int i = 0; i < active; ++i)
        {
            const Frame& frame = frames_[static_cast<std::size_t>(i)];
            for (int j = i * factor; j < (i + 1) * factor; ++j)
                wet[j] = frame.compensation * (fastTanh(frame.drive * x[j] + frame.bias) - frame.biasTanh);
        }
    }
    // El bloqueador de continua, en double, con los canales en el mismo bucle.
    const double r = dcCoefficient_;
    bool finite = true;
    if (numChannels == 2)
    {
        DcBlocker& a = dcBlockers_[0];
        DcBlocker& b = dcBlockers_[1];
        float* wa = wet_[0].data();
        float* wb = wet_[1].data();
        for (int n = 0; n < inner; ++n)
        {
            const double xa = wa[n];
            const double xb = wb[n];
            a.output = xa - a.input + r * a.output;
            b.output = xb - b.input + r * b.output;
            a.input = xa;
            b.input = xb;
            wa[n] = static_cast<float>(a.output);
            wb[n] = static_cast<float>(b.output);
        }
        finite = std::isfinite(a.output) && std::isfinite(b.output);
    }
    else if (numChannels == 1)
    {
        DcBlocker& a = dcBlockers_[0];
        float* wa = wet_[0].data();
        for (int n = 0; n < inner; ++n)
        {
            const double xa = wa[n];
            a.output = xa - a.input + r * a.output;
            a.input = xa;
            wa[n] = static_cast<float>(a.output);
        }
        finite = std::isfinite(a.output);
    }
    // La mezcla con la seca sobremuestreada.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto c = static_cast<std::size_t>(ch);
        float* x = up[c];
        const float* wet = wet_[c].data();
        for (int i = 0; i < active; ++i)
        {
            const float mix = frames_[static_cast<std::size_t>(i)].mix;
            for (int j = i * factor; j < (i + 1) * factor; ++j)
                x[j] += mix * (wet[j] - x[j]);
        }
    }
    oversampler_.downsample(up.data(), io.data(), numChannels, active);
    if (fading)
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto c = static_cast<std::size_t>(ch);
            for (int i = 0; i < active; ++i)
            {
                const auto k = static_cast<std::size_t>(i);
                const float fade = frames_[k].fade;
                if (fade < 1.0f)
                    io[c][i] = dry_[c][k] + fade * (io[c][i] - dry_[c][k]);
            }
        }
    if (!finite || !oversampler_.isFinite())
    {
        for (int ch = 0; ch < numChannels; ++ch)
            std::fill_n(channels[ch] + offset, length, 0.0f);
        clearMemory();
        return;
    }
    if (active < length)
        clearMemory(); // El fundido hacia Off ha terminado: el resto del tramo es la entrada tal cual.
}

} // namespace esceq::dsp
