// SPDX-License-Identifier: MIT
//
// Algoritmos de filtro (FilterAlgorithm): Classic sigue siendo exactamente el
// de la 0.1, y Matched se acerca al prototipo analógico sin perder ninguna de
// las garantías de los filtros. Ver docs/dsp/filtros.md y
// docs/decisiones/0013-correccion-en-agudos.md.
#include "esceq/dsp/band_filter.h"
#include "esceq/dsp/filter_design.h"

#include "filter_test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;
using namespace esceq::test;

namespace
{

// ---------------------------------------------------------------------------
// Copia literal del diseño de la 0.1 (dsp/src/filter_design.cpp en v0.1.0).
// Es la referencia del contrato de sonido: NO se modifica. Si el diseño
// Classic deja de coincidir con ella bit a bit, los proyectos guardados con
// la 0.1 dejan de sonar igual.
namespace v01
{

constexpr double kPi = std::numbers::pi;

SectionCoeffs secondOrder(double g, double k, double m0, double m1, double m2)
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

SectionCoeffs firstOrder(double g, double m0, double m2)
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

double butterworthQ(int order, int pair)
{
    return 1.0 / (2.0 * std::sin((2.0 * pair - 1.0) * kPi / (2.0 * order)));
}

void designCut(BandDesign& design, bool lowCut, double g, double q, int slope)
{
    const int order = sanitizeSlope(slope) / 6;
    const int pairs = order / 2;
    if (order % 2 == 1)
        design.sections[static_cast<std::size_t>(design.numSections++)] =
            lowCut ? firstOrder(g, 1.0, -1.0) : firstOrder(g, 0.0, 1.0);
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

BandDesign designBand(const FilterParams& params, double sampleRate)
{
    const double frequency = std::clamp(params.frequencyHz, kMinFrequencyHz, kMaxFrequencyRatio * sampleRate);
    const double g = std::tan(kPi * frequency / sampleRate);
    const double q = std::clamp(params.q, kMinQ, kMaxQ);
    const double gainDb = std::clamp(params.gainDb, -kMaxGainDb, kMaxGainDb);
    const double k = 1.0 / q;

    BandDesign design;
    auto single = [&design](const SectionCoeffs& c)
    {
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
        const double a = std::pow(10.0, gainDb / 20.0);
        single(firstOrder(g * a, a, 1.0 / a - a));
        break;
    }
    }
    return design;
}

} // namespace v01
// ---------------------------------------------------------------------------

bool identical(const BandDesign& a, const BandDesign& b)
{
    if (a.numSections != b.numSections)
        return false;
    for (int i = 0; i < a.numSections; ++i)
    {
        const auto& x = a.sections[static_cast<std::size_t>(i)];
        const auto& y = b.sections[static_cast<std::size_t>(i)];
        if (x.order != y.order || x.g != y.g || x.k != y.k || x.a1 != y.a1 || x.a2 != y.a2 || x.a3 != y.a3 ||
            x.m0 != y.m0 || x.m1 != y.m1 || x.m2 != y.m2)
            return false;
    }
    return true;
}

/// Frecuencias de `from` a `to` (sin incluirla), cada una `ratio` veces la anterior.
std::vector<double> logSweep(double from, double to, double ratio)
{
    std::vector<double> out;
    for (int i = 0;; ++i)
    {
        const double f = from * std::pow(ratio, i);
        if (f >= to)
            return out;
        out.push_back(f);
    }
}

FilterParams matched(FilterType type, double frequency, double gain, double q, int slope = 12)
{
    return {type, frequency, gain, q, slope, FilterAlgorithm::Matched};
}

/// Peor diferencia en dB entre Matched y el prototipo analógico, de 20 Hz a
/// `top` (sin pasar de 0,4999·fs). Las dos por debajo de −40 dB cuentan como
/// iguales: ahí el corte ya ha hecho su trabajo.
double worstAnalogErrorDb(const FilterParams& p, double sampleRate, double top = 20000.0)
{
    const auto design = designBand(p, sampleRate);
    double worst = 0.0;
    for (double f : logSweep(20.0, std::min(top, 0.4999 * sampleRate), 1.01))
    {
        const double analog = analogMagnitudeDb(p, f);
        const double digital = magnitudeDb(design, f, sampleRate);
        if (analog < -40.0 && digital < -40.0)
            continue;
        worst = std::max(worst, std::abs(analog - digital));
    }
    return worst;
}

const double kSampleRates[] = {44100.0, 48000.0, 96000.0, 192000.0};

} // namespace

TEST_CASE("Classic: identico bit a bit al diseno de la 0.1")
{
    // Todos los tipos, dentro y fuera de rango, en las frecuencias de muestreo
    // habituales y en una rara. Con == en cada coeficiente, no con tolerancia.
    const double frequencies[] = {-5.0, 1.0, 20.0, 63.0, 1000.0, 9000.0, 15000.0, 19999.0, 23000.0, 1.0e7};
    const double gains[] = {-100.0, -30.0, -6.5, 0.0, 0.25, 12.0, 30.0, 100.0};
    const double qs[] = {0.0, 0.1, 0.7071, 1.0, 4.5, 30.0, 100.0};
    const int slopes[] = {6, 12, 18, 30, 48, 96, 200};
    int compared = 0;
    for (auto type : kAllTypes)
        for (double sampleRate : {22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
            for (double f : frequencies)
                for (double gain : gains)
                    for (double q : qs)
                        for (int slope : slopes)
                        {
                            const FilterParams p{type, f, gain, q, slope};
                            REQUIRE(p.algorithm == FilterAlgorithm::Classic);
                            INFO(typeName(type) << " " << f << " Hz, " << gain << " dB, Q " << q << ", " << slope
                                                << " dB/oct, " << sampleRate << " Hz");
                            REQUIRE(identical(designBand(p, sampleRate), v01::designBand(p, sampleRate)));
                            ++compared;
                        }
    CHECK(compared == 9 * 6 * 10 * 8 * 7 * 7);
}

TEST_CASE("Diseno con la frecuencia preparada: el mismo bit a bit con los dos algoritmos")
{
    for (auto algorithm : {FilterAlgorithm::Classic, FilterAlgorithm::Matched})
        for (auto type : kAllTypes)
            for (double f : {30.0, 2500.0, 18000.0})
                for (double gain : {-12.0, 0.0, 7.0})
                {
                    const FilterParams p{type, f, gain, 3.0, 30, algorithm};
                    REQUIRE(identical(designBand(p, 44100.0), designBandPrepared(p, prepareFrequency(f, 44100.0))));
                }
}

TEST_CASE("Matched: magnitud del prototipo analogico en continua y en el centro")
{
    for (double sampleRate : kSampleRates)
        for (double f0 : {100.0, 1000.0, 10000.0, 18000.0})
            for (double q : {0.3, 1.0, 8.0})
                for (double gain : {-18.0, -3.0, 6.0, 24.0})
                {
                    INFO(sampleRate << " Hz, f0 " << f0 << ", Q " << q << ", " << gain << " dB");
                    const auto bell = designBand(matched(FilterType::Bell, f0, gain, q), sampleRate);
                    CHECK_THAT(magnitudeDb(bell, f0, sampleRate), WithinAbs(gain, 1e-6));
                    CHECK_THAT(magnitudeDb(bell, 0.0, sampleRate), WithinAbs(0.0, 1e-9));

                    const auto low = designBand(matched(FilterType::LowShelf, f0, gain, q), sampleRate);
                    CHECK_THAT(magnitudeDb(low, 0.0, sampleRate), WithinAbs(gain, 1e-9));
                    const auto high = designBand(matched(FilterType::HighShelf, f0, gain, q), sampleRate);
                    CHECK_THAT(magnitudeDb(high, 0.0, sampleRate), WithinAbs(0.0, 1e-9));

                    const auto tilt = designBand(matched(FilterType::Tilt, f0, gain, q), sampleRate);
                    CHECK_THAT(magnitudeDb(tilt, 0.0, sampleRate), WithinAbs(-gain, 1e-9));
                    CHECK_THAT(magnitudeDb(tilt, f0, sampleRate), WithinAbs(0.0, 1e-6)); // el pivote
                }
}

TEST_CASE("Matched: notch y paso alto con los ceros exactos")
{
    for (double sampleRate : kSampleRates)
        for (double f0 : {100.0, 1000.0, 10000.0, 18000.0})
            for (double q : {0.3, 1.0, 8.0})
            {
                INFO(sampleRate << " Hz, f0 " << f0 << ", Q " << q);
                const auto notch = designBand(matched(FilterType::Notch, f0, 0.0, q), sampleRate);
                CHECK(magnitudeDb(notch, f0, sampleRate) < -200.0);
                CHECK_THAT(magnitudeDb(notch, 0.0, sampleRate), WithinAbs(0.0, 1e-9));
                for (int slope : {12, 48})
                {
                    const auto cut = designBand(matched(FilterType::LowCut, f0, 0.0, q, slope), sampleRate);
                    CHECK(magnitudeDb(cut, 0.0, sampleRate) < -200.0);
                }
            }
}

TEST_CASE("Matched: cerca de Nyquist se acerca al analogico mucho mas que Classic")
{
    // El criterio de la ADR 0013, en el rango de uso habitual: campanas,
    // shelves, notch, paso banda y cortes de 1 a 16 kHz, Q de 0,3 a 8,
    // ±18 dB. Cifras medidas en docs/dsp/filtros.md.
    struct Limit
    {
        FilterType type;
        double at48k; ///< Peor error de Matched a 44,1 y 48 kHz.
        double at96k; ///< A 96 y 192 kHz.
    };
    // Lo medido, con un 20 % de margen.
    const Limit limits[] = {
        {FilterType::Bell, 2.1, 0.45},  {FilterType::LowShelf, 3.8, 0.4},  {FilterType::HighShelf, 3.8, 0.4},
        {FilterType::Notch, 7.0, 0.4},  {FilterType::BandPass, 3.2, 0.55}, {FilterType::HighCut, 4.5, 0.4},
        {FilterType::LowCut, 2.0, 0.2}, {FilterType::Tilt, 2.2, 0.45},
    };
    for (const auto& limit : limits)
        for (double sampleRate : kSampleRates)
        {
            double worstMatched = 0.0, worstClassic = 0.0;
            for (double f0 : {1000.0, 4000.0, 8000.0, 12000.0, 16000.0})
                for (double q : {0.3, 0.7071, 2.0, 8.0})
                    for (double gain : {-18.0, -6.0, 6.0, 18.0})
                        for (int slope : {12, 24, 48})
                        {
                            FilterParams p = matched(limit.type, f0, gain, q, slope);
                            worstMatched = std::max(worstMatched, worstAnalogErrorDb(p, sampleRate));
                            p.algorithm = FilterAlgorithm::Classic;
                            worstClassic = std::max(worstClassic, worstAnalogErrorDb(p, sampleRate));
                        }
            INFO(typeName(limit.type) << " a " << sampleRate << " Hz: Matched " << worstMatched << " dB, Classic "
                                      << worstClassic << " dB");
            CHECK(worstMatched < (sampleRate < 90000.0 ? limit.at48k : limit.at96k));
            CHECK(worstMatched < worstClassic);
        }
}

TEST_CASE("Matched: lejos de Nyquist es el prototipo analogico y casi Classic")
{
    // Por debajo de fs/24 la bilineal apenas deforma: Matched sigue al
    // analógico casi exactamente, y donde la banda suena (por encima de
    // −12 dB) es prácticamente el mismo filtro que Classic. Cambiar de uno a
    // otro no mueve los graves.
    for (auto type : kAllTypes)
        for (double q : {0.3, 0.7071, 4.0})
            for (double gain : {-12.0, 9.0})
                for (double f0 : {30.0, 200.0, 800.0})
                {
                    const FilterParams classic{type, f0, gain, q, 24};
                    FilterParams p = classic;
                    p.algorithm = FilterAlgorithm::Matched;
                    const auto a = designBand(classic, 48000.0);
                    const auto b = designBand(p, 48000.0);
                    for (double f : logSweep(10.0, 2000.0, 1.05))
                    {
                        INFO(typeName(type) << " f0 " << f0 << " Q " << q << " " << gain << " dB, en " << f << " Hz");
                        const double analog = analogMagnitudeDb(p, f);
                        if (analog > -60.0)
                            CHECK_THAT(magnitudeDb(b, f, 48000.0), WithinAbs(analog, 0.02));
                        if (magnitudeDb(a, f, 48000.0) > -12.0)
                            CHECK_THAT(magnitudeDb(b, f, 48000.0), WithinAbs(magnitudeDb(a, f, 48000.0), 0.05));
                    }
                }
}

TEST_CASE("Matched: realce y atenuacion simetricos se cancelan exactamente")
{
    for (auto type : {FilterType::Bell, FilterType::LowShelf, FilterType::HighShelf, FilterType::Tilt})
        for (double sampleRate : {44100.0, 96000.0})
            for (double f0 : {60.0, 5000.0, 17000.0})
                for (double q : {0.2, 0.7071, 3.0, 30.0})
                    for (double gain : {1.0, 12.0, 30.0})
                    {
                        const auto up = designBand(matched(type, f0, gain, q), sampleRate);
                        const auto down = designBand(matched(type, f0, -gain, q), sampleRate);
                        for (double f : {20.0, 1000.0, f0, 15000.0, 21000.0})
                        {
                            INFO(typeName(type) << " f0 " << f0 << " Q " << q << " " << gain << " dB, en " << f);
                            const double t = responseTangent(f, sampleRate);
                            const auto product = responseAtTangent(up, t) * responseAtTangent(down, t);
                            CHECK_THAT(std::abs(product - 1.0), WithinAbs(0.0, 1e-9));
                        }
                    }
}

TEST_CASE("Matched: una campana a 0 dB deja pasar el audio bit a bit")
{
    BandFilter filter;
    filter.setDesign(designBand(matched(FilterType::Bell, 15000.0, 0.0, 2.0), 44100.0));
    std::vector<float> signal(512);
    for (std::size_t i = 0; i < signal.size(); ++i)
        signal[i] = std::sin(0.37f * static_cast<float>(i)) * 0.8f;
    auto processed = signal;
    float* channel = processed.data();
    filter.process(&channel, 1, static_cast<int>(processed.size()));
    CHECK(processed == signal);
}

TEST_CASE("Matched: el audio procesado tiene la respuesta calculada")
{
    // La curva sigue siendo lo que suena: response() vale para cualquier SVF.
    for (auto type : kAllTypes)
        for (double sampleRate : {44100.0, 96000.0})
            for (double f0 : {200.0, 14000.0})
            {
                const auto design = designBand(matched(type, f0, -7.0, 2.5, 36), sampleRate);
                const auto impulse = impulseResponse(design, 1 << 15);
                for (double probe : {100.0, 1000.0, f0, 18000.0})
                {
                    const double expected = magnitudeDb(design, probe, sampleRate);
                    if (expected < -60.0)
                        continue;
                    INFO(typeName(type) << " f0 " << f0 << " en " << probe << " Hz");
                    CHECK_THAT(measuredMagnitudeDb(impulse, probe, sampleRate), WithinAbs(expected, 0.01));
                }
            }
}

TEST_CASE("Matched: parametros extremos dan coeficientes finitos y estables")
{
    for (auto type : kAllTypes)
        for (double sampleRate : {8000.0, 44100.0, 192000.0, 768000.0})
            for (double f : {-100.0, 0.0, 1.0, 20.0, 20000.0, 1.0e9})
                for (double gain : {-1000.0, -60.0, -30.0, 0.0, 1.0e-12, 30.0, 60.0, 1000.0})
                    for (double q : {-1.0, 0.0, 0.025, 0.7071, 0.70711, 40.0, 1.0e6})
                        for (int slope : {6, 12, 90})
                        {
                            const auto design = designBand(matched(type, f, gain, q, slope), sampleRate);
                            REQUIRE(design.numSections >= 1);
                            REQUIRE(design.numSections <= kMaxSections);
                            if (filterUsesGain(type)) // cabe con su inversa (fase lineal con dinámica)
                                REQUIRE(design.numSections <= kMaxSections / 2);
                            for (int i = 0; i < design.numSections; ++i)
                            {
                                const auto& c = design.sections[static_cast<std::size_t>(i)];
                                INFO(typeName(type)
                                     << " " << f << " Hz, " << gain << " dB, Q " << q << ", " << sampleRate);
                                REQUIRE(std::isfinite(c.a1 + c.a2 + c.a3 + c.m0 + c.m1 + c.m2));
                                REQUIRE(c.g > 0.0); // polos dentro del círculo unidad
                                REQUIRE((c.order == 1 || c.k > 0.0));
                            }
                            REQUIRE(std::isfinite(magnitudeDb(design, 1000.0, sampleRate)));
                        }
}

TEST_CASE("Matched: la respuesta cambia sin saltos al mover frecuencia, Q y ganancia")
{
    // Al arrastrar una banda, o con la dinámica cambiando la ganancia en cada
    // muestra, no hay umbrales que hagan saltar la respuesta: ni al cruzar
    // Q = 0,7071 en los shelves (de una a dos secciones), ni cuando sus ceros
    // se acercan a Nyquist, ni al pasar la ganancia por 0 dB.
    const double sampleRate = 44100.0;
    auto worstStep = [&](auto paramsAt, int steps)
    {
        double worst = 0.0;
        BandDesign previous = designBand(paramsAt(0.0), sampleRate);
        for (int i = 1; i <= steps; ++i)
        {
            const BandDesign next = designBand(paramsAt(static_cast<double>(i) / steps), sampleRate);
            for (double f : logSweep(20.0, 22000.0, 1.03))
            {
                const double t = responseTangent(f, sampleRate);
                worst = std::max(worst, std::abs(magnitudeDbAtTangent(next, t) - magnitudeDbAtTangent(previous, t)));
            }
            previous = next;
        }
        return worst;
    };
    for (auto type : {FilterType::LowShelf, FilterType::HighShelf})
        for (double gain : {-24.0, 24.0})
        {
            INFO(typeName(type) << " " << gain << " dB");
            // Q de 0,6 a 0,85 en 2000 pasos, a 5 kHz.
            CHECK(worstStep([&](double x) { return matched(type, 5000.0, gain, 0.6 + 0.25 * x); }, 2000) < 0.01);
            // Frecuencia de 6 a 20 kHz con Q 8: los ceros cruzan la zona de 0,9π a 0,98π.
            CHECK(worstStep([&](double x) { return matched(type, 6000.0 * std::pow(20000.0 / 6000.0, x), gain, 8.0); },
                            4000) < 0.25);
        }
    for (auto type : {FilterType::Bell, FilterType::LowShelf, FilterType::HighShelf, FilterType::Tilt})
    {
        INFO(typeName(type));
        CHECK(worstStep([&](double x) { return matched(type, 12000.0, -0.5 + x, 4.0); }, 1000) < 0.01);
    }
}
