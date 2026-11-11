// SPDX-License-Identifier: MIT
#include "esceq/dsp/filter_design.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace esceq::dsp {

namespace {

constexpr double kPi = std::numbers::pi;

SectionCoeffs secondOrder(double g, double k, double m0, double m1, double m2) noexcept
{
    SectionCoeffs c;
    c.order = 2;
    c.g = g;
    c.k = k;
    c.a1 = 1.0 / (1.0 + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    c.m0 = m0;
    c.m1 = m1;
    c.m2 = m2;
    return c;
}

SectionCoeffs firstOrder(double g, double m0, double m2) noexcept
{
    SectionCoeffs c;
    c.order = 1;
    c.g = g;
    c.k = 0.0;
    c.a1 = g / (1.0 + g);
    c.m0 = m0;
    c.m2 = m2;
    return c;
}

/// Q de la pareja de polos número `pair` (1..N/2) de un Butterworth de orden N.
double butterworthQ(int order, int pair) noexcept
{
    return 1.0 / (2.0 * std::sin((2.0 * pair - 1.0) * kPi / (2.0 * order)));
}

int sanitizeSlope(int slope) noexcept
{
    const int clamped = std::clamp(slope, kMinSlopeDbPerOct, kMaxSlopeDbPerOct);
    return ((clamped + 3) / 6) * 6;
}

void designCut(BandDesign& design, bool lowCut, double g, double q, int slope) noexcept
{
    const int order = sanitizeSlope(slope) / 6;
    const int pairs = order / 2;

    if (order % 2 == 1)
        design.sections[static_cast<std::size_t>(design.numSections++)] =
            lowCut ? firstOrder(g, 1.0, -1.0) : firstOrder(g, 0.0, 1.0);

    // De menor a mayor Q. El Q del usuario escala solo la sección más
    // resonante (pair == 1): con Q = 0,7071 el filtro es Butterworth puro.
    for (int pair = pairs; pair >= 1; --pair)
    {
        double sectionQ = butterworthQ(order, pair);
        if (pair == 1)
            sectionQ *= q / kButterworthQ;
        const double k = 1.0 / sectionQ;

        design.sections[static_cast<std::size_t>(design.numSections++)] =
            lowCut ? secondOrder(g, k, 1.0, -k, -1.0) : secondOrder(g, k, 0.0, 0.0, 1.0);
    }
}

std::complex<double> sectionResponse(const SectionCoeffs& c, double tanHalfOmega) noexcept
{
    // El SVF trapezoidal equivale a la transformada bilineal del prototipo
    // analógico normalizado, evaluado en s = j·tan(ω/2)/g.
    const std::complex<double> s{0.0, tanHalfOmega / c.g};

    if (c.order == 1)
        return c.m0 + c.m2 / (s + 1.0);

    return c.m0 + (c.m1 * s + c.m2) / (s * s + c.k * s + 1.0);
}

} // namespace

BandDesign designBand(const FilterParams& params, double sampleRate) noexcept
{
    const double frequency = std::clamp(params.frequencyHz, kMinFrequencyHz, kMaxFrequencyRatio * sampleRate);
    const double q = std::clamp(params.q, kMinQ, kMaxQ);
    const double gainDb = std::clamp(params.gainDb, -kMaxGainDb, kMaxGainDb);

    const double g = std::tan(kPi * frequency / sampleRate);
    const double k = 1.0 / q;

    BandDesign design;
    auto single = [&design](const SectionCoeffs& c) {
        design.sections[0] = c;
        design.numSections = 1;
    };

    switch (params.type)
    {
    case FilterType::Bell:
    {
        const double a = std::pow(10.0, gainDb / 40.0);
        const double kBell = 1.0 / (q * a);
        single(secondOrder(g, kBell, 1.0, kBell * (a * a - 1.0), 0.0));
        break;
    }
    case FilterType::LowShelf:
    {
        const double a = std::pow(10.0, gainDb / 40.0);
        single(secondOrder(g / std::sqrt(a), k, 1.0, k * (a - 1.0), a * a - 1.0));
        break;
    }
    case FilterType::HighShelf:
    {
        const double a = std::pow(10.0, gainDb / 40.0);
        single(secondOrder(g * std::sqrt(a), k, a * a, k * (1.0 - a) * a, 1.0 - a * a));
        break;
    }
    case FilterType::LowCut:
        designCut(design, true, g, q, params.slopeDbPerOct);
        break;
    case FilterType::HighCut:
        designCut(design, false, g, q, params.slopeDbPerOct);
        break;
    case FilterType::Notch:
        single(secondOrder(g, k, 1.0, -k, 0.0));
        break;
    case FilterType::BandPass:
        single(secondOrder(g, k, 0.0, k, 0.0));
        break;
    case FilterType::AllPass:
        single(secondOrder(g, k, 1.0, -2.0 * k, 0.0));
        break;
    case FilterType::Tilt:
    {
        // H(s) = (A·s + 1)/(s + A): 1/A en continua, A en agudos, 1 en el pivote.
        const double a = std::pow(10.0, gainDb / 20.0);
        single(firstOrder(g * a, a, 1.0 / a - a));
        break;
    }
    }

    return design;
}

std::complex<double> response(const BandDesign& design, double frequencyHz, double sampleRate) noexcept
{
    const double frequency = std::clamp(frequencyHz, 0.0, 0.4999 * sampleRate);
    const double tanHalfOmega = std::tan(kPi * frequency / sampleRate);

    std::complex<double> h{1.0, 0.0};
    for (int i = 0; i < design.numSections; ++i)
        h *= sectionResponse(design.sections[static_cast<std::size_t>(i)], tanHalfOmega);
    return h;
}

double magnitudeDb(const BandDesign& design, double frequencyHz, double sampleRate) noexcept
{
    const double magnitude = std::abs(response(design, frequencyHz, sampleRate));
    return 20.0 * std::log10(std::max(magnitude, 1.0e-15));
}

} // namespace esceq::dsp
