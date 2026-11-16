// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/band_filter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace esceq::test {

/// Respuesta al impulso de una banda, procesada en bloques de `blockSize`.
inline std::vector<float> impulseResponse(const dsp::BandDesign& design, int numSamples, int blockSize = 512)
{
    dsp::BandFilter filter;
    filter.setDesign(design);

    std::vector<float> signal(static_cast<std::size_t>(numSamples), 0.0f);
    signal[0] = 1.0f;

    for (int pos = 0; pos < numSamples; pos += blockSize)
    {
        float* channel = signal.data() + pos;
        filter.process(&channel, 1, std::min(blockSize, numSamples - pos));
    }
    return signal;
}

/// Magnitud en dB de la transformada de Fourier de `signal` en una frecuencia.
/// Es la respuesta MEDIDA del filtro si `signal` es su respuesta al impulso.
inline double measuredMagnitudeDb(const std::vector<float>& signal, double frequencyHz, double sampleRate)
{
    // Aritmética real explícita: mucho más rápida que std::complex sin optimizar.
    const double omega = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
    const double stepRe = std::cos(omega), stepIm = -std::sin(omega);
    double phRe = 1.0, phIm = 0.0, sumRe = 0.0, sumIm = 0.0;

    for (const float sample : signal)
    {
        sumRe += static_cast<double>(sample) * phRe;
        sumIm += static_cast<double>(sample) * phIm;
        const double nextRe = phRe * stepRe - phIm * stepIm;
        phIm = phRe * stepIm + phIm * stepRe;
        phRe = nextRe;
    }
    return 20.0 * std::log10(std::max(std::hypot(sumRe, sumIm), 1.0e-15));
}

inline const char* typeName(dsp::FilterType type)
{
    switch (type)
    {
    case dsp::FilterType::Bell: return "Bell";
    case dsp::FilterType::LowShelf: return "LowShelf";
    case dsp::FilterType::HighShelf: return "HighShelf";
    case dsp::FilterType::LowCut: return "LowCut";
    case dsp::FilterType::HighCut: return "HighCut";
    case dsp::FilterType::Notch: return "Notch";
    case dsp::FilterType::BandPass: return "BandPass";
    case dsp::FilterType::AllPass: return "AllPass";
    case dsp::FilterType::Tilt: return "Tilt";
    }
    return "?";
}

inline constexpr dsp::FilterType kAllTypes[] = {
    dsp::FilterType::Bell,    dsp::FilterType::LowShelf, dsp::FilterType::HighShelf,
    dsp::FilterType::LowCut,  dsp::FilterType::HighCut,  dsp::FilterType::Notch,
    dsp::FilterType::BandPass, dsp::FilterType::AllPass, dsp::FilterType::Tilt,
};

/// Respuesta del prototipo analógico de una banda en `frequencyHz`, sin
/// transformada bilineal ni ningún otro paso a digital: lo que el algoritmo
/// Matched intenta igualar. Escrito a partir de las fórmulas del Audio EQ
/// Cookbook y de la tabla de docs/dsp/filtros.md, sin usar el código de dsp/.
/// Los parámetros deben estar ya en rango.
inline std::complex<double> analogResponse(const dsp::FilterParams& p, double frequencyHz)
{
    using C = std::complex<double>;
    const C s{0.0, frequencyHz / p.frequencyHz};
    const double a = std::pow(10.0, p.gainDb / 40.0);
    const double q = p.q;
    switch (p.type)
    {
    case dsp::FilterType::Bell:
        return (s * s + s * (a / q) + 1.0) / (s * s + s / (a * q) + 1.0);
    case dsp::FilterType::LowShelf:
        return a * (s * s + s * (std::sqrt(a) / q) + a) / (a * s * s + s * (std::sqrt(a) / q) + 1.0);
    case dsp::FilterType::HighShelf:
        return a * (a * s * s + s * (std::sqrt(a) / q) + 1.0) / (s * s + s * (std::sqrt(a) / q) + a);
    case dsp::FilterType::Notch:
        return (s * s + 1.0) / (s * s + s / q + 1.0);
    case dsp::FilterType::BandPass:
        return (s / q) / (s * s + s / q + 1.0);
    case dsp::FilterType::AllPass:
        return (s * s - s / q + 1.0) / (s * s + s / q + 1.0);
    case dsp::FilterType::Tilt:
    {
        const double big = a * a;
        return (big * s + 1.0) / (s + big);
    }
    case dsp::FilterType::LowCut:
    case dsp::FilterType::HighCut:
    {
        // Butterworth de orden N; el Q del usuario escala el polo más resonante.
        const bool low = p.type == dsp::FilterType::LowCut;
        const int order = p.slopeDbPerOct / 6;
        C h{1.0, 0.0};
        if (order % 2 == 1)
            h *= low ? s / (s + 1.0) : 1.0 / (s + 1.0);
        for (int pair = 1; pair <= order / 2; ++pair)
        {
            double sectionQ = 1.0 / (2.0 * std::sin((2.0 * pair - 1.0) * std::numbers::pi / (2.0 * order)));
            if (pair == 1)
                sectionQ *= q / dsp::kButterworthQ;
            h *= (low ? s * s : C{1.0, 0.0}) / (s * s + s / sectionQ + 1.0);
        }
        return h;
    }
    }
    return {1.0, 0.0};
}

inline double analogMagnitudeDb(const dsp::FilterParams& p, double frequencyHz)
{
    return 20.0 * std::log10(std::max(std::abs(analogResponse(p, frequencyHz)), 1.0e-15));
}

} // namespace esceq::test
