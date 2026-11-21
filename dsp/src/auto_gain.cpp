// SPDX-License-Identifier: MIT
#include "esceq/dsp/auto_gain.h"

#include "esceq/dsp/filter_design.h"
#include "esceq/dsp/linear_phase.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <numbers>

namespace esceq::dsp {

namespace {

/// Rejilla (docs/dsp/compensacion-de-ganancia.md): puntos por octava en toda
/// la banda, y alrededor de cada banda, a ±kRefineSpan anchos de banda de su
/// frecuencia, kRefinePerBandwidth puntos por ancho de banda. El ancho de
/// banda se toma como 1/Q octavas.
constexpr double kBasePointsPerOctave = 24.0;
constexpr double kRefineSpan = 4.0;
constexpr double kRefinePerBandwidth = 16.0;
/// En los shelves y el tilt, el resalte de un Q alto queda a ±(ganancia/40)
/// décadas de la frecuencia: la zona refinada se ensancha eso.
constexpr double kShelfOctavesPerDb = std::numbers::log2e * std::numbers::ln10 / 40.0;

/// Potencia de un biquad b/a (con a0 = 1) en z⁻¹ = `z1`. En complejos: la
/// forma con cos ω y cos 2ω pierde precisión en graves, donde el numerador
/// del paso alto casi se anula.
double biquadPower(const double (&b)[3], const double (&a)[3], std::complex<double> z1) noexcept
{
    const std::complex<double> z2 = z1 * z1;
    return std::norm((b[0] + b[1] * z1 + b[2] * z2) / (a[0] + a[1] * z1 + a[2] * z2));
}

/// Bandas que cambian la magnitud. Los ajustes llegan saneados.
bool contributes(const BandSettings& band) noexcept
{
    if (!band.enabled || band.filter.type == FilterType::AllPass)
        return false;
    return !filterUsesGain(band.filter.type) || band.filter.gainDb != 0.0;
}

/// En mono, el único canal es a la vez el izquierdo y la suma (como en el IIR).
bool processesMono(ChannelMode mode) noexcept
{
    return mode != ChannelMode::Right && mode != ChannelMode::Side;
}

bool processesLeft(ChannelMode mode) noexcept
{
    return mode == ChannelMode::Stereo || mode == ChannelMode::Left;
}

bool processesRight(ChannelMode mode) noexcept
{
    return mode == ChannelMode::Stereo || mode == ChannelMode::Right;
}

struct Curve
{
    std::array<BandDesign, kMaxBands> designs{};
    std::array<ChannelMode, kMaxBands> channels{};
    std::array<double, kMaxBands> lowOctave{}; ///< Zona refinada de cada banda, en log2(Hz).
    std::array<double, kMaxBands> highOctave{};
    std::array<double, kMaxBands> spacing{}; ///< Separación entre puntos que pide, en octavas.
    int count = 0;
    double sampleRate = 48000.0;
    bool mono = false;
    bool mixesChannels = false; ///< Alguna banda en Mid o Side.

    /// Potencia de la curva en `frequencyHz` (ver autoGainDb()).
    double power(double frequencyHz) const noexcept
    {
        const double tangent = responseTangent(frequencyHz, sampleRate);
        if (mono || !mixesChannels)
        {
            double left = 1.0, right = 1.0;
            for (int i = 0; i < count; ++i)
            {
                const auto index = static_cast<std::size_t>(i);
                const double p = std::norm(responseAtTangent(designs[index], tangent));
                const ChannelMode mode = channels[index];
                if (mono ? processesMono(mode) : processesLeft(mode))
                    left *= p;
                if (!mono && processesRight(mode))
                    right *= p;
            }
            return mono ? left : 0.5 * (left + right);
        }
        // Con Mid o Side, la matriz de 2×2 de cada banda con su magnitud (la
        // misma composición que la fase lineal), y la media de la potencia de
        // los dos canales de salida para entradas incorreladas.
        ChannelMatrix total;
        for (int i = 0; i < count; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            const double h = std::abs(responseAtTangent(designs[index], tangent));
            total = channelMatrix(channels[index], h) * total;
        }
        return 0.5 * (total.ll * total.ll + total.lr * total.lr + total.rl * total.rl + total.rr * total.rr);
    }

    /// Subdivisiones del tramo [from, to] (en log2(Hz)) que piden las bandas.
    int divisions(double from, double to) const noexcept
    {
        double step = to - from;
        for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i)
            if (to >= lowOctave[i] && from <= highOctave[i])
                step = std::min(step, spacing[i]);
        return std::max(1, static_cast<int>(std::ceil((to - from) / step - 1.0e-9)));
    }
};

/// La curva de las bandas que cuentan, con la zona refinada de cada una.
Curve buildCurve(std::span<const BandSettings> bands, double sampleRate, int numChannels) noexcept
{
    Curve curve;
    curve.sampleRate = sampleRate;
    curve.mono = numChannels < 2;
    for (const auto& raw : bands.first(std::min<std::size_t>(bands.size(), kMaxBands)))
    {
        const BandSettings band = sanitizeBand(raw);
        if (!contributes(band))
            continue;
        const auto index = static_cast<std::size_t>(curve.count++);
        curve.designs[index] = designBand(band.filter, sampleRate);
        curve.channels[index] = band.channel;
        curve.mixesChannels =
            curve.mixesChannels || band.channel == ChannelMode::Mid || band.channel == ChannelMode::Side;

        const double bandwidth = 1.0 / band.filter.q;
        double reach = kRefineSpan * bandwidth;
        const FilterType type = band.filter.type;
        if (type == FilterType::LowShelf || type == FilterType::HighShelf || type == FilterType::Tilt)
            reach += std::abs(band.filter.gainDb) * kShelfOctavesPerDb;
        const double centre = std::log2(std::min(band.filter.frequencyHz, kMaxFrequencyRatio * sampleRate));
        curve.lowOctave[index] = centre - reach;
        curve.highOctave[index] = centre + reach;
        curve.spacing[index] = bandwidth / kRefinePerBandwidth;
    }
    return curve;
}

} // namespace

double kWeightingPower(double frequencyHz) noexcept ESCEQ_NONBLOCKING
{
    // ITU-R BS.1770-5, tablas 1 y 2: coeficientes a 48 kHz.
    static constexpr double kShelfB[3] = {1.53512485958697, -2.69169618940638, 1.19839281085285};
    static constexpr double kShelfA[3] = {1.0, -1.69065929318241, 0.73248077421585};
    static constexpr double kHighPassB[3] = {1.0, -2.0, 1.0};
    static constexpr double kHighPassA[3] = {1.0, -1.99004745483398, 0.99007225036621};
    const auto z1 = std::polar(1.0, -2.0 * std::numbers::pi * frequencyHz / 48000.0);
    return biquadPower(kShelfB, kShelfA, z1) * biquadPower(kHighPassB, kHighPassA, z1);
}

double autoGainDb(std::span<const BandSettings> bands, double sampleRate, int numChannels) noexcept
{
    if (!(sampleRate > 0.0))
        return 0.0;

    const Curve curve = buildCurve(bands, sampleRate, numChannels);
    if (curve.count == 0)
        return 0.0; // curva plana: exactamente 0, sin errores de redondeo

    // Regla del trapecio en log2(f), donde el ruido rosa tiene la misma
    // potencia por octava: el peso de cada punto es solo |K|².
    const double low = std::log2(kAutoGainLowHz);
    const double high = std::log2(std::min(kAutoGainHighHz, kMaxFrequencyRatio * sampleRate));
    if (!(high > low))
        return 0.0;
    const int intervals = static_cast<int>(std::ceil((high - low) * kBasePointsPerOctave));
    const double base = (high - low) / static_cast<double>(intervals);

    double weighted = 0.0, total = 0.0;
    double previousWeight = kWeightingPower(kAutoGainLowHz);
    double previousPower = curve.power(kAutoGainLowHz);
    for (int i = 0; i < intervals; ++i)
    {
        const double from = low + base * static_cast<double>(i);
        const double to = i + 1 == intervals ? high : from + base;
        const int steps = curve.divisions(from, to);
        const double step = (to - from) / static_cast<double>(steps);
        for (int j = 1; j <= steps; ++j)
        {
            const double frequency = std::exp2(j == steps ? to : from + step * static_cast<double>(j));
            const double weight = kWeightingPower(frequency);
            const double power = curve.power(frequency);
            weighted += 0.5 * step * (previousWeight * previousPower + weight * power);
            total += 0.5 * step * (previousWeight + weight);
            previousWeight = weight;
            previousPower = power;
        }
    }

    if (!(weighted > 0.0))
        return kAutoGainLimitDb; // la curva lo quita todo
    const double gain = -10.0 * std::log10(weighted / total);
    return std::isfinite(gain) ? std::clamp(gain, -kAutoGainLimitDb, kAutoGainLimitDb) : kAutoGainLimitDb;
}

void staticCurveDb(std::span<const BandSettings> bands, double sampleRate, int numChannels,
                   std::span<const double> frequenciesHz, std::span<double> outDb) noexcept
{
    const std::size_t n = std::min(frequenciesHz.size(), outDb.size());
    if (!(sampleRate > 0.0))
    {
        std::fill_n(outDb.begin(), n, 0.0);
        return;
    }
    const Curve curve = buildCurve(bands, sampleRate, numChannels);
    for (std::size_t i = 0; i < n; ++i)
        outDb[i] = curve.count == 0 ? 0.0 : 10.0 * std::log10(std::max(curve.power(frequenciesHz[i]), 1.0e-30));
}

void AutoGain::request(std::span<const BandSettings> bands, double sampleRate,
                       int numChannels) noexcept ESCEQ_NONBLOCKING
{
    Request& slot = requests_.writeSlot();
    const auto count = std::min<std::size_t>(bands.size(), kMaxBands);
    std::copy_n(bands.begin(), count, slot.bands.begin());
    slot.numBands = static_cast<int>(count);
    slot.sampleRate = sampleRate;
    slot.numChannels = numChannels;
    slot.sequence = nextSequence_++;
    if (nextSequence_ == 0)
        nextSequence_ = 1; // 0 es "nada calculado"
    requests_.publish();
}

void AutoGain::restart() noexcept ESCEQ_NONBLOCKING
{
    validFrom_ = nextSequence_;
}

std::optional<float> AutoGain::latestDb() const noexcept ESCEQ_NONBLOCKING
{
    const std::uint64_t packed = result_.load(std::memory_order_acquire);
    const auto sequence = static_cast<std::uint32_t>(packed >> 32);
    // Comparación con vuelta: vale mientras entre una petición y la otra no
    // haya 2^31 peticiones (con una por bloque, meses).
    if (sequence == 0 || static_cast<std::int32_t>(sequence - validFrom_) < 0)
        return std::nullopt;
    return std::bit_cast<float>(static_cast<std::uint32_t>(packed & 0xffffffffu));
}

bool AutoGain::service() noexcept
{
    const Request* request = requests_.read();
    if (request == nullptr)
        return false;
    const auto gain =
        static_cast<float>(autoGainDb(std::span(request->bands).first(static_cast<std::size_t>(request->numBands)),
                                      request->sampleRate, request->numChannels));
    result_.store((static_cast<std::uint64_t>(request->sequence) << 32) | std::bit_cast<std::uint32_t>(gain),
                  std::memory_order_release);
    return true;
}

} // namespace esceq::dsp
