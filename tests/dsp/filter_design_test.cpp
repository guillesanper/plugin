// SPDX-License-Identifier: MIT
//
// Comprueba el diseño de filtros contra valores de libro, independientes
// de la implementación. La coincidencia con el audio realmente procesado
// se comprueba en band_filter_test.cpp.
#include "esceq/dsp/filter_design.h"

#include "filter_test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <numbers>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;

namespace {

constexpr double kFs = 96000.0;

double db(const FilterParams& params, double frequency, double sampleRate = kFs)
{
    return magnitudeDb(designBand(params, sampleRate), frequency, sampleRate);
}

/// Campana del "Audio EQ Cookbook" de R. Bristow-Johnson, evaluada en z.
/// Fórmula independiente de la del SVF: sirve de referencia cruzada.
double cookbookBellDb(double f0, double gainDb, double q, double frequency, double sampleRate)
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * std::numbers::pi * f0 / sampleRate;
    const double alpha = std::sin(w0) / (2.0 * q);

    const double b0 = 1.0 + alpha * a, b1 = -2.0 * std::cos(w0), b2 = 1.0 - alpha * a;
    const double a0 = 1.0 + alpha / a, a1 = -2.0 * std::cos(w0), a2 = 1.0 - alpha / a;

    const auto z1 = std::polar(1.0, -2.0 * std::numbers::pi * frequency / sampleRate);
    const auto h = (b0 + b1 * z1 + b2 * z1 * z1) / (a0 + a1 * z1 + a2 * z1 * z1);
    return 20.0 * std::log10(std::abs(h));
}

} // namespace

TEST_CASE("Bell: la ganancia en el centro es la pedida y lejos es 0 dB")
{
    for (double gain : {-18.0, -6.0, 0.0, 3.0, 12.0})
    {
        for (double q : {0.3, 1.0, 8.0})
        {
            const FilterParams p{FilterType::Bell, 1000.0, gain, q};
            INFO("gain " << gain << " q " << q);
            CHECK_THAT(db(p, 1000.0), WithinAbs(gain, 1e-9));
            CHECK_THAT(db(p, 1.0), WithinAbs(0.0, 0.01));
            CHECK_THAT(db(p, 47000.0), WithinAbs(0.0, 0.01));
        }
    }
}

TEST_CASE("Bell: realce y atenuación son simétricos")
{
    for (double f : {200.0, 900.0, 1100.0, 5000.0})
    {
        const double boost = db({FilterType::Bell, 1000.0, 9.0, 2.0}, f);
        const double cut = db({FilterType::Bell, 1000.0, -9.0, 2.0}, f);
        CHECK_THAT(boost + cut, WithinAbs(0.0, 1e-9));
    }
}

TEST_CASE("Bell: coincide con la fórmula del Audio EQ Cookbook")
{
    for (double sampleRate : {44100.0, 48000.0, 96000.0})
        for (double f0 : {60.0, 1000.0, 8000.0})
            for (double gain : {-12.0, 6.0})
                for (double q : {0.5, 4.0})
                    for (double f : {30.0, 250.0, 1000.0, 4000.0, 15000.0})
                    {
                        INFO("fs " << sampleRate << " f0 " << f0 << " gain " << gain << " q " << q << " f " << f);
                        CHECK_THAT(db({FilterType::Bell, f0, gain, q}, f, sampleRate),
                                   WithinAbs(cookbookBellDb(f0, gain, q, f, sampleRate), 1e-6));
                    }
}

TEST_CASE("Shelves: ganancia completa en su lado, mitad en la frecuencia, 0 dB en el otro")
{
    for (double gain : {-12.0, 6.0})
    {
        const FilterParams low{FilterType::LowShelf, 1000.0, gain, kButterworthQ};
        CHECK_THAT(db(low, 1.0), WithinAbs(gain, 0.01));
        CHECK_THAT(db(low, 1000.0), WithinAbs(gain / 2.0, 1e-9));
        CHECK_THAT(db(low, 47000.0), WithinAbs(0.0, 0.01));

        const FilterParams high{FilterType::HighShelf, 1000.0, gain, kButterworthQ};
        CHECK_THAT(db(high, 1.0), WithinAbs(0.0, 0.01));
        CHECK_THAT(db(high, 1000.0), WithinAbs(gain / 2.0, 1e-9));
        CHECK_THAT(db(high, 47000.0), WithinAbs(gain, 0.01));
    }
}

TEST_CASE("Cortes: Butterworth, -3,01 dB en la frecuencia para cualquier pendiente")
{
    for (int slope = 6; slope <= 96; slope += 6)
    {
        INFO("pendiente " << slope);
        CHECK(designBand({FilterType::HighCut, 1000.0, 0.0, kButterworthQ, slope}, kFs).numSections == (slope / 6 + 1) / 2);

        const FilterParams high{FilterType::HighCut, 1000.0, 0.0, kButterworthQ, slope};
        CHECK_THAT(db(high, 1000.0), WithinAbs(-3.0103, 1e-3));
        CHECK_THAT(db(high, 1.0), WithinAbs(0.0, 1e-3));

        const FilterParams low{FilterType::LowCut, 1000.0, 0.0, kButterworthQ, slope};
        CHECK_THAT(db(low, 1000.0), WithinAbs(-3.0103, 1e-3));
        CHECK_THAT(db(low, 40000.0), WithinAbs(0.0, 1e-3));
    }
}

TEST_CASE("Cortes: la pendiente medida es la pedida")
{
    // Lejos del corte y de Nyquist, una octava atenúa 20·log10(2) = 6,02 dB
    // por cada orden del filtro ("6 dB/oct" es el nombre redondeado).
    for (int slope = 6; slope <= 96; slope += 6)
    {
        INFO("pendiente " << slope);
        const double perOctave = (slope / 6) * 20.0 * std::log10(2.0);
        const FilterParams high{FilterType::HighCut, 100.0, 0.0, kButterworthQ, slope};
        CHECK_THAT(db(high, 400.0) - db(high, 800.0), WithinAbs(perOctave, 0.25));

        const FilterParams low{FilterType::LowCut, 8000.0, 0.0, kButterworthQ, slope};
        CHECK_THAT(db(low, 2000.0) - db(low, 1000.0), WithinAbs(perOctave, 0.25));
    }
}

TEST_CASE("Cortes: el Q controla la resonancia en la frecuencia de corte")
{
    // Orden 2: la ganancia en el corte es exactamente Q.
    CHECK_THAT(db({FilterType::HighCut, 1000.0, 0.0, 2.0, 12}, 1000.0), WithinAbs(20.0 * std::log10(2.0), 1e-9));
    CHECK_THAT(db({FilterType::LowCut, 1000.0, 0.0, 4.0, 12}, 1000.0), WithinAbs(20.0 * std::log10(4.0), 1e-9));

    // Órdenes mayores: más Q, más nivel en el corte; a 6 dB/oct no influye.
    CHECK(db({FilterType::HighCut, 1000.0, 0.0, 3.0, 48}, 1000.0) > db({FilterType::HighCut, 1000.0, 0.0, 1.0, 48}, 1000.0));
    CHECK(db({FilterType::HighCut, 1000.0, 0.0, 3.0, 6}, 1000.0) == db({FilterType::HighCut, 1000.0, 0.0, 1.0, 6}, 1000.0));
}

TEST_CASE("Cortes: pendientes fuera de rango o no múltiplos de 6 se ajustan")
{
    auto sections = [](int slope) { return designBand({FilterType::LowCut, 1000.0, 0.0, kButterworthQ, slope}, kFs).numSections; };
    CHECK(sections(-5) == 1);
    CHECK(sections(0) == 1);
    CHECK(sections(13) == 1);  // 12 dB/oct
    CHECK(sections(1000) == 8); // 96 dB/oct
}

TEST_CASE("Notch: anula el centro y respeta el resto")
{
    const FilterParams p{FilterType::Notch, 1000.0, 0.0, 4.0};
    CHECK(db(p, 1000.0) < -200.0);
    CHECK_THAT(db(p, 50.0), WithinAbs(0.0, 0.01));
    CHECK_THAT(db(p, 20000.0), WithinAbs(0.0, 0.01));
}

TEST_CASE("BandPass: 0 dB en el centro, atenúa a los lados")
{
    const FilterParams p{FilterType::BandPass, 1000.0, 0.0, 2.0};
    CHECK_THAT(db(p, 1000.0), WithinAbs(0.0, 1e-9));
    CHECK(db(p, 100.0) < -20.0);
    CHECK(db(p, 10000.0) < -20.0);
}

TEST_CASE("AllPass: magnitud plana y fase invertida en el centro")
{
    const auto design = designBand({FilterType::AllPass, 1000.0, 0.0, 1.5}, kFs);
    for (double f : {20.0, 300.0, 1000.0, 5000.0, 30000.0})
        CHECK_THAT(magnitudeDb(design, f, kFs), WithinAbs(0.0, 1e-9));

    const auto centre = response(design, 1000.0, kFs);
    CHECK_THAT(centre.real(), WithinAbs(-1.0, 1e-9));
    CHECK_THAT(centre.imag(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Tilt: -ganancia en graves, +ganancia en agudos, 0 dB en el pivote")
{
    for (double gain : {-6.0, 4.0})
    {
        const FilterParams p{FilterType::Tilt, 1000.0, gain};
        CHECK_THAT(db(p, 0.01), WithinAbs(-gain, 0.01));
        CHECK_THAT(db(p, 1000.0), WithinAbs(0.0, 1e-9));
        CHECK_THAT(db(p, 47990.0), WithinAbs(gain, 0.01));
    }
}

TEST_CASE("Diseño: parámetros extremos o absurdos dan coeficientes finitos y estables")
{
    const double frequencies[] = {-100.0, 0.0, 1.0, 20.0, 20000.0, 1.0e9};
    const double gains[] = {-1000.0, -30.0, 0.0, 30.0, 1000.0};
    const double qs[] = {-1.0, 0.0, 0.025, 40.0, 1.0e6};

    for (auto type : esceq::test::kAllTypes)
        for (double sampleRate : {44100.0, 192000.0})
            for (double f : frequencies)
                for (double gain : gains)
                    for (double q : qs)
                    {
                        const auto design = designBand({type, f, gain, q, 96}, sampleRate);
                        REQUIRE(design.numSections >= 1);
                        REQUIRE(design.numSections <= kMaxSections);
                        for (int i = 0; i < design.numSections; ++i)
                        {
                            const auto& c = design.sections[static_cast<std::size_t>(i)];
                            REQUIRE(std::isfinite(c.a1 + c.a2 + c.a3 + c.m0 + c.m1 + c.m2));
                            REQUIRE(c.g > 0.0);  // polos dentro del círculo unidad
                            REQUIRE(c.k >= 0.0);
                        }
                        REQUIRE(std::isfinite(magnitudeDb(design, 1000.0, sampleRate)));
                    }
}
