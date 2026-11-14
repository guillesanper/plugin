// SPDX-License-Identifier: MIT
#include "esceq/dsp/linear_phase.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <optional>

namespace esceq::dsp
{

namespace
{

/// Bandas que cambian la señal en el FIR. Los ajustes llegan saneados.
bool contributes(const BandSettings& band) noexcept
{
    return band.enabled;
}

/// En mono, el único canal es a la vez el izquierdo y la suma (como en el IIR).
bool processesMono(ChannelMode mode) noexcept
{
    return mode != ChannelMode::Right && mode != ChannelMode::Side;
}

double bandMagnitude(const BandDesign& design, double tangent) noexcept
{
    return std::abs(responseAtTangent(design, tangent));
}

/// Función de Bessel modificada de primera especie y orden 0 (serie de potencias).
double besselI0(double x) noexcept
{
    const double quarter = 0.25 * x * x;
    double term = 1.0, sum = 1.0;
    for (int k = 1; k < 200 && term > 1.0e-17 * sum; ++k)
    {
        term *= quarter / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
    }
    return sum;
}

int factorFor(double sampleRate) noexcept
{
    if (!(sampleRate > 0.0) || sampleRate <= 50000.0)
        return 1;
    if (sampleRate <= 100000.0)
        return 2;
    if (sampleRate <= 200000.0)
        return 4;
    return 8;
}

} // namespace

LinearPhaseSize linearPhaseSize(PhaseMode mode, double sampleRate) noexcept
{
    const int factor = factorFor(sampleRate);
    switch (mode)
    {
    case PhaseMode::Iir:
        return {};
    case PhaseMode::LinearLow:
        return {2048 * factor, 256 * factor};
    case PhaseMode::LinearMedium:
        return {8192 * factor, 512 * factor};
    case PhaseMode::LinearHigh:
        return {32768 * factor, 1024 * factor};
    }
    return {};
}

ChannelMatrix channelMatrix(ChannelMode mode, double h) noexcept
{
    switch (mode)
    {
    case ChannelMode::Stereo:
        return {h, 0.0, 0.0, h};
    case ChannelMode::Left:
        return {h, 0.0, 0.0, 1.0};
    case ChannelMode::Right:
        return {1.0, 0.0, 0.0, h};
    case ChannelMode::Mid:
    {
        // d = (h − 1)·(L + R)/2;  L += d;  R += d
        const double half = 0.5 * (h - 1.0);
        return {1.0 + half, half, half, 1.0 + half};
    }
    case ChannelMode::Side:
    {
        // d = (h − 1)·(L − R)/2;  L += d;  R −= d
        const double half = 0.5 * (h - 1.0);
        return {1.0 + half, -half, -half, 1.0 + half};
    }
    }
    return {};
}

ChannelMatrix operator*(const ChannelMatrix& b, const ChannelMatrix& a) noexcept
{
    return {b.ll * a.ll + b.lr * a.rl, b.ll * a.lr + b.lr * a.rr, b.rl * a.ll + b.rr * a.rl, b.rl * a.lr + b.rr * a.rr};
}

ChannelMatrix LinearPhaseDesigner::targetResponse(std::span<const BandSettings> bands, double frequencyHz,
                                                  double sampleRate, int numChannels)
{
    const double tangent = responseTangent(frequencyHz, sampleRate);
    ChannelMatrix total;
    double mono = 1.0;
    for (const auto& band : bands.first(std::min<std::size_t>(bands.size(), kMaxBands)))
    {
        if (!contributes(band))
            continue;
        const double h = bandMagnitude(designBand(band.filter, sampleRate), tangent);
        total = channelMatrix(band.channel, h) * total;
        if (processesMono(band.channel))
            mono *= h;
    }
    if (numChannels < 2)
        return {mono, 0.0, 0.0, mono};
    return total;
}

struct LinearPhaseDesigner::Cache
{
    struct Band
    {
        std::optional<FilterParams> params; ///< Ajustes para los que vale `magnitude`.
        std::vector<double> magnitude;      ///< Un valor por punto de la rejilla.
    };

    double sampleRate = 0.0;
    int gridSize = 0;
    std::vector<double> tangents;
    std::array<Band, kMaxBands> bands;
    std::optional<FftDouble> fft;
    std::vector<std::complex<double>> buffer;
    int windowLength = 0;
    std::vector<double> window; ///< Kaiser, de 0 a windowLength/2 (simétrica).

    void prepareGrid(double rate, int size)
    {
        if (rate == sampleRate && size == gridSize)
            return;
        sampleRate = rate;
        gridSize = size;
        const int points = size / 2 + 1;
        tangents.resize(static_cast<std::size_t>(points));
        for (int k = 0; k < points; ++k)
            tangents[static_cast<std::size_t>(k)] = responseTangent(rate * k / size, rate);
        for (auto& band : bands)
            band.params.reset();
        fft.emplace(std::countr_zero(static_cast<unsigned>(size)));
        buffer.assign(static_cast<std::size_t>(size), {});
    }

    const std::vector<double>& magnitude(int index, const FilterParams& params)
    {
        Band& band = bands[static_cast<std::size_t>(index)];
        if (band.params && *band.params == params)
            return band.magnitude;
        const BandDesign design = designBand(params, sampleRate);
        band.magnitude.resize(tangents.size());
        for (std::size_t k = 0; k < tangents.size(); ++k)
            band.magnitude[k] = bandMagnitude(design, tangents[k]);
        band.params = params;
        return band.magnitude;
    }

    void prepareWindow(int length)
    {
        if (length == windowLength)
            return;
        windowLength = length;
        const int half = length / 2;
        window.resize(static_cast<std::size_t>(half));
        const double norm = 1.0 / besselI0(kKaiserBeta);
        for (int n = 0; n < half; ++n)
        {
            const double x = static_cast<double>(n) / half;
            window[static_cast<std::size_t>(n)] = besselI0(kKaiserBeta * std::sqrt(1.0 - x * x)) * norm;
        }
    }

    /// Dos espectros reales y pares (`first` y `second`, gridSize/2 + 1
    /// puntos) a coeficientes de fase lineal de `length` puntos, con una
    /// sola FFT inversa: la de first + i·second.
    void toTaps(const std::vector<double>& first, const std::vector<double>* second, int length,
                std::vector<double>& outFirst, std::vector<double>* outSecond)
    {
        const int size = gridSize;
        for (int k = 0; k <= size / 2; ++k)
        {
            const std::complex<double> value{first[static_cast<std::size_t>(k)],
                                             second != nullptr ? (*second)[static_cast<std::size_t>(k)] : 0.0};
            buffer[static_cast<std::size_t>(k)] = value;
            if (k > 0 && k < size / 2)
                buffer[static_cast<std::size_t>(size - k)] = value;
        }
        fft->inverse(buffer.data());

        const int centre = length / 2;
        const double scale = 1.0 / static_cast<double>(size);
        outFirst.assign(static_cast<std::size_t>(length), 0.0);
        if (outSecond != nullptr)
            outSecond->assign(static_cast<std::size_t>(length), 0.0);
        for (int n = 0; n < centre; ++n)
        {
            // Un espectro real y par da un impulso real y par; la media de n y
            // −n quita el redondeo para que la simetría sea exacta.
            const auto& positive = buffer[static_cast<std::size_t>(n)];
            const auto& negative = buffer[static_cast<std::size_t>((size - n) % size)];
            const double w = window[static_cast<std::size_t>(n)] * scale;
            const double a = 0.5 * (positive.real() + negative.real()) * w;
            outFirst[static_cast<std::size_t>(centre + n)] = a;
            outFirst[static_cast<std::size_t>(centre - n)] = a;
            if (outSecond != nullptr)
            {
                const double b = 0.5 * (positive.imag() + negative.imag()) * w;
                (*outSecond)[static_cast<std::size_t>(centre + n)] = b;
                (*outSecond)[static_cast<std::size_t>(centre - n)] = b;
            }
        }
        // outFirst[0] (n = −centre) queda a cero: la ventana tiene length − 1 puntos.
    }
};

LinearPhaseDesigner::LinearPhaseDesigner() : cache_(std::make_unique<Cache>()) {}

LinearPhaseDesigner::~LinearPhaseDesigner() = default;

LinearPhaseFir LinearPhaseDesigner::design(std::span<const BandSettings> bands, PhaseMode mode, double sampleRate,
                                           int numChannels)
{
    LinearPhaseFir fir;
    fir.size = linearPhaseSize(mode, sampleRate);
    fir.numChannels = numChannels < 2 ? 1 : 2;
    const int length = fir.size.firLength;
    if (length == 0 || !(sampleRate > 0.0))
        return fir;

    Cache& cache = *cache_;
    cache.prepareGrid(sampleRate, 2 * length);
    cache.prepareWindow(length);
    const auto points = static_cast<std::size_t>(cache.gridSize / 2 + 1);
    bands = bands.first(std::min<std::size_t>(bands.size(), kMaxBands));

    // Qué hace falta calcular: en estéreo, si todas las bandas son Stereo, los
    // dos canales reciben lo mismo y no se mezclan.
    bool sameChannels = true;
    bool mixesChannels = false;
    for (const auto& band : bands)
    {
        if (!contributes(band) || (fir.numChannels == 1 && !processesMono(band.channel)))
            continue;
        sameChannels = sameChannels && (fir.numChannels == 1 || band.channel == ChannelMode::Stereo);
        mixesChannels = mixesChannels || (fir.numChannels == 2 &&
                                          (band.channel == ChannelMode::Mid || band.channel == ChannelMode::Side));
    }

    std::vector<double> ll(points, 1.0), lr, rl, rr;
    if (!sameChannels)
        rr.assign(points, 1.0);
    if (mixesChannels)
    {
        lr.assign(points, 0.0);
        rl.assign(points, 0.0);
    }

    for (std::size_t index = 0; index < bands.size(); ++index)
    {
        const BandSettings& band = bands[index];
        if (!contributes(band) || (fir.numChannels == 1 && !processesMono(band.channel)))
            continue;
        const auto& h = cache.magnitude(static_cast<int>(index), band.filter);
        if (sameChannels)
        {
            for (std::size_t k = 0; k < points; ++k)
                ll[k] *= h[k];
        }
        else if (!mixesChannels)
        {
            // Diagonal: Left, Right y Stereo.
            const bool left = band.channel != ChannelMode::Right;
            const bool right = band.channel != ChannelMode::Left;
            for (std::size_t k = 0; k < points; ++k)
            {
                if (left)
                    ll[k] *= h[k];
                if (right)
                    rr[k] *= h[k];
            }
        }
        else
        {
            for (std::size_t k = 0; k < points; ++k)
            {
                const ChannelMatrix total =
                    channelMatrix(band.channel, h[k]) * ChannelMatrix{ll[k], lr[k], rl[k], rr[k]};
                ll[k] = total.ll;
                lr[k] = total.lr;
                rl[k] = total.rl;
                rr[k] = total.rr;
            }
        }
    }

    constexpr double kIdentityTolerance = 1.0e-9;
    auto near = [](const std::vector<double>& values, double target)
    {
        return std::all_of(values.begin(), values.end(),
                           [target](double v) { return std::abs(v - target) <= kIdentityTolerance; });
    };
    fir.identity = near(ll, 1.0) && (rr.empty() || near(rr, 1.0)) && (lr.empty() || near(lr, 0.0)) &&
                   (rl.empty() || near(rl, 0.0));

    cache.toTaps(ll, rr.empty() ? nullptr : &rr, length, fir.ll, rr.empty() ? nullptr : &fir.rr);
    if (mixesChannels)
        cache.toTaps(lr, &rl, length, fir.lr, &fir.rl);
    return fir;
}

} // namespace esceq::dsp
