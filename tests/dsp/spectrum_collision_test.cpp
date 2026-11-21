// SPDX-License-Identifier: MIT
#include "esceq/dsp/spectrum_analyzer.h"
#include "esceq/dsp/spectrum_collision.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;

namespace
{

std::vector<double> logGrid(int count, double lo = 20.0, double hi = 20000.0)
{
    std::vector<double> f(static_cast<std::size_t>(count));
    for (std::size_t i = 0; i < f.size(); ++i)
        f[i] = lo * std::pow(hi / lo, static_cast<double>(i) / static_cast<double>(count - 1));
    return f;
}

/// Espectro plano a `floor`, con `level` entre `lo` y `hi` Hz.
std::vector<float> plateau(const std::vector<double>& f, double lo, double hi, float level, float floor = -120.0f)
{
    std::vector<float> out(f.size(), floor);
    for (std::size_t i = 0; i < f.size(); ++i)
        if (f[i] >= lo && f[i] <= hi)
            out[i] = level;
    return out;
}

std::vector<float> analyzeSines(const std::vector<double>& grid, std::initializer_list<double> freqs)
{
    constexpr double fs = 48000.0;
    SpectrumAnalyzer analyzer(13);
    analyzer.setSampleRate(fs);
    std::vector<float> signal(static_cast<std::size_t>(analyzer.fftSize()));
    for (std::size_t n = 0; n < signal.size(); ++n)
        for (double f : freqs)
            signal[n] += static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(n) / fs));
    analyzer.push(signal.data(), static_cast<int>(signal.size()));
    std::vector<float> levels(grid.size());
    analyzer.compute(grid, levels);
    return levels;
}

float maxIn(const std::vector<double>& f, const std::vector<float>& v, double lo, double hi)
{
    float m = 0.0f;
    for (std::size_t i = 0; i < f.size(); ++i)
        if (f[i] >= lo && f[i] <= hi)
            m = std::max(m, v[i]);
    return m;
}

} // namespace

TEST_CASE("Colisiones: suavizado por octavas")
{
    const auto f = logGrid(97); // 10 puntos por octava, aproximadamente
    std::vector<float> out(f.size());

    SECTION("anchura cero copia")
    {
        const auto in = plateau(f, 100.0, 200.0, -10.0f);
        smoothOctaves(f, in, 0.0, out);
        CHECK(out == in);
    }

    SECTION("un espectro plano no cambia")
    {
        const std::vector<float> in(f.size(), -24.0f);
        smoothOctaves(f, in, 1.0 / 3.0, out);
        for (float v : out)
            CHECK_THAT(v, WithinAbs(-24.0, 1e-3));
    }

    SECTION("un pico aislado se reparte en potencia entre los puntos de la ventana")
    {
        std::vector<float> in(f.size(), -300.0f);
        const std::size_t k = 48;
        in[k] = 0.0f;
        smoothOctaves(f, in, 1.0, out);
        // Dentro de ±1/2 octava caben unos 10 puntos: la media es ~ -10 dB.
        int inWindow = 0;
        for (double x : f)
            if (std::abs(std::log2(x / f[k])) <= 0.5 + 1e-12)
                ++inWindow;
        CHECK_THAT(out[k], WithinAbs(-10.0 * std::log10(inWindow), 1e-3));
        // Lejos del pico no llega nada.
        CHECK(out[0] < -200.0f);
        CHECK(out.back() < -200.0f);
    }

    SECTION("los extremos promedian solo lo que tienen")
    {
        std::vector<float> in(f.size(), -60.0f);
        in.front() = -60.0f;
        smoothOctaves(f, in, 2.0, out);
        CHECK_THAT(out.front(), WithinAbs(-60.0, 1e-3));
        CHECK_THAT(out.back(), WithinAbs(-60.0, 1e-3));
    }

    SECTION("tamaños vacíos o distintos no se salen de los límites")
    {
        smoothOctaves({}, {}, 1.0, {});
        std::vector<float> small(3, -1.0f);
        std::vector<float> outSmall(3, 0.0f);
        smoothOctaves(f, small, 1.0, outSmall);
        CHECK(outSmall[0] == -1.0f);
        const std::vector<double> one{1000.0};
        const std::vector<float> oneLevel{-5.0f};
        std::vector<float> oneOut(1);
        smoothOctaves(one, oneLevel, 1.0, oneOut);
        CHECK(oneOut[0] == -5.0f);
    }
}

TEST_CASE("Colisiones: suavizado con un ancho minimo en Hz")
{
    // Ancho de la ventana: el de las octavas mientras mide más que el mínimo.
    CHECK(smoothWindowOctaves(1000.0, 1.0 / 3.0) == 2.0 * std::log2(std::exp2(1.0 / 6.0)));
    CHECK_THAT(smoothWindowOctaves(1000.0, 1.0 / 3.0, 40.0), WithinAbs(1.0 / 3.0, 1e-12));
    // A 50 Hz, 47 Hz de ventana son mucho más de 1/3 de octava: f·r − f/r = 47.
    const double width = smoothWindowOctaves(50.0, 1.0 / 3.0, 47.0);
    const double ratio = std::exp2(0.5 * width);
    CHECK_THAT(50.0 * ratio - 50.0 / ratio, WithinAbs(47.0, 1e-9));
    CHECK(width > 1.0);

    // El suavizado es la media en potencia de los puntos dentro de esa ventana.
    std::vector<double> frequencies;
    std::vector<float> levels;
    for (int i = 0; i < 120; ++i)
    {
        frequencies.push_back(20.0 * std::pow(1000.0, i / 119.0));
        levels.push_back(static_cast<float>(-20.0 + 6.0 * std::sin(0.7 * i)));
    }
    std::vector<float> out(levels.size()), plain(levels.size()), same(levels.size());
    smoothOctaves(frequencies, levels, 1.0 / 3.0, out, 47.0);
    for (std::size_t i = 0; i < frequencies.size(); ++i)
    {
        const double r = std::exp2(0.5 * smoothWindowOctaves(frequencies[i], 1.0 / 3.0, 47.0));
        double sum = 0.0;
        int count = 0;
        for (std::size_t j = 0; j < frequencies.size(); ++j)
            if (frequencies[j] >= frequencies[i] / r && frequencies[j] <= frequencies[i] * r)
            {
                sum += std::pow(10.0, 0.1 * levels[j]);
                ++count;
            }
        CHECK_THAT(out[i], WithinAbs(10.0 * std::log10(sum / count), 1e-4));
    }
    // Con 0 Hz, el de siempre bit a bit; y con anchura 0, el mínimo sigue valiendo.
    smoothOctaves(frequencies, levels, 1.0 / 3.0, plain);
    smoothOctaves(frequencies, levels, 1.0 / 3.0, same, 0.0);
    CHECK(plain == same);
    smoothOctaves(frequencies, levels, 0.0, same, 47.0);
    CHECK(same != levels);
    CHECK(same.back() == levels.back());
}

TEST_CASE("Colisiones: presencia y enmascaramiento")
{
    const CollisionSettings s{};

    SECTION("presencia segun el suelo absoluto")
    {
        CHECK(presence(s.floorDb - 1.0f, -10.0f, s) == 0.0f);
        CHECK(presence(s.floorDb, -10.0f, s) == 0.0f); // con pico alto manda el rango relativo
        CHECK(presence(-10.0f, -10.0f, s) == 1.0f);
    }

    SECTION("presencia relativa al maximo de la propia pista")
    {
        const float peak = -6.0f;
        const float threshold = peak - s.relevanceRangeDb;
        CHECK(presence(threshold, peak, s) == 0.0f);
        CHECK(presence(threshold + s.presenceRampDb, peak, s) == 1.0f);
        CHECK_THAT(presence(threshold + 0.5f * s.presenceRampDb, peak, s), WithinAbs(0.5, 1e-6));
    }

    SECTION("una pista muy baja usa el suelo absoluto")
    {
        const float peak = s.floorDb + 5.0f;
        CHECK(presence(s.floorDb - 0.1f, peak, s) == 0.0f);
        CHECK(presence(s.floorDb + s.presenceRampDb, peak, s) == 1.0f);
    }

    SECTION("la presencia crece con el nivel")
    {
        float previous = -1.0f;
        for (float db = -100.0f; db <= 0.0f; db += 0.5f)
        {
            const float p = presence(db, 0.0f, s);
            CHECK(p >= previous);
            CHECK(p >= 0.0f);
            CHECK(p <= 1.0f);
            previous = p;
        }
    }

    SECTION("enmascaramiento")
    {
        CHECK(maskingFactor(-s.maskingWindowDb - 1.0f, s) == 0.0f);
        CHECK(maskingFactor(-s.maskingWindowDb, s) == 0.0f);
        CHECK_THAT(maskingFactor(-0.5f * s.maskingWindowDb, s), WithinAbs(0.5, 1e-6));
        CHECK(maskingFactor(0.0f, s) == 1.0f);
        CHECK(maskingFactor(20.0f, s) == 1.0f);
    }

    SECTION("ajustes degenerados no dividen por cero")
    {
        CollisionSettings zero{};
        zero.presenceRampDb = 0.0f;
        zero.maskingWindowDb = 0.0f;
        CHECK(std::isfinite(presence(-10.0f, 0.0f, zero)));
        CHECK(maskingFactor(-1.0f, zero) == 0.0f);
        CHECK(maskingFactor(0.0f, zero) == 1.0f);
    }
}

TEST_CASE("Colisiones: severidad entre dos espectros")
{
    const auto f = logGrid(97);
    const CollisionSettings s{};
    std::vector<float> severity(f.size());

    SECTION("dos pistas iguales chocan donde suenan")
    {
        const auto a = plateau(f, 80.0, 300.0, -12.0f);
        collisionSeverity(a, a, severity, s);
        CHECK(maxIn(f, severity, 80.0, 300.0) == 1.0f);
        CHECK(maxIn(f, severity, 20.0, 70.0) == 0.0f);
        CHECK(maxIn(f, severity, 400.0, 20000.0) == 0.0f);
    }

    SECTION("pistas en zonas distintas no chocan")
    {
        const auto bass = plateau(f, 40.0, 200.0, -12.0f);
        const auto hats = plateau(f, 6000.0, 16000.0, -12.0f);
        collisionSeverity(bass, hats, severity, s);
        CHECK(*std::max_element(severity.begin(), severity.end()) == 0.0f);
    }

    SECTION("una pista mucho mas baja no enmascara a la propia")
    {
        const auto own = plateau(f, 100.0, 1000.0, -6.0f);
        auto other = plateau(f, 100.0, 1000.0, -6.0f - s.maskingWindowDb - 3.0f);
        // La otra está presente para sí misma (es su máximo), pero queda por debajo.
        collisionSeverity(own, other, severity, s);
        CHECK(*std::max_element(severity.begin(), severity.end()) == 0.0f);
    }

    SECTION("una pista mas alta enmascara del todo")
    {
        const auto own = plateau(f, 100.0, 1000.0, -18.0f);
        const auto other = plateau(f, 100.0, 1000.0, -6.0f);
        collisionSeverity(own, other, severity, s);
        CHECK(maxIn(f, severity, 100.0, 1000.0) == 1.0f);
    }

    SECTION("la direccion importa: quien queda debajo es el enmascarado")
    {
        const auto loud = plateau(f, 100.0, 1000.0, -6.0f);
        const auto quiet = plateau(f, 100.0, 1000.0, -30.0f);
        std::vector<float> loudView(f.size());
        std::vector<float> quietView(f.size());
        collisionSeverity(loud, quiet, loudView, s);
        collisionSeverity(quiet, loud, quietView, s);
        CHECK(maxIn(f, loudView, 100.0, 1000.0) == 0.0f);
        CHECK(maxIn(f, quietView, 100.0, 1000.0) == 1.0f);
    }

    SECTION("la propia en silencio no tiene colisiones")
    {
        const std::vector<float> silent(f.size(), -150.0f);
        const auto other = plateau(f, 20.0, 20000.0, -6.0f);
        collisionSeverity(silent, other, severity, s);
        CHECK(*std::max_element(severity.begin(), severity.end()) == 0.0f);
    }

    SECTION("vacio no hace nada")
    {
        collisionSeverity({}, {}, {}, s);
    }

    SECTION("con el analizador real: dos senos a la misma frecuencia chocan, a frecuencias lejanas no")
    {
        const auto grid = logGrid(192);
        std::vector<float> a(grid.size());
        std::vector<float> b(grid.size());
        std::vector<float> sev(grid.size());

        smoothOctaves(grid, analyzeSines(grid, {110.0}), 1.0 / 3.0, a);
        smoothOctaves(grid, analyzeSines(grid, {110.0}), 1.0 / 3.0, b);
        collisionSeverity(a, b, sev, s);
        CHECK(maxIn(grid, sev, 95.0, 125.0) > 0.9f);
        CHECK(maxIn(grid, sev, 1000.0, 20000.0) == 0.0f);

        smoothOctaves(grid, analyzeSines(grid, {5000.0}), 1.0 / 3.0, b);
        collisionSeverity(a, b, sev, s);
        CHECK(*std::max_element(sev.begin(), sev.end()) < 0.05f);
    }
}

TEST_CASE("Colisiones: tramos")
{
    const auto f = logGrid(193); // la densidad de la rejilla de la sesión: ~1/19 de octava
    std::vector<float> sev(f.size(), 0.0f);
    auto fill = [&](double lo, double hi, float v)
    {
        for (std::size_t i = 0; i < f.size(); ++i)
            if (f[i] >= lo && f[i] <= hi)
                sev[i] = v;
    };

    SECTION("sin colisiones no hay tramos")
    {
        CHECK(findCollisionRegions(f, sev, 0.3f).empty());
        CHECK(findCollisionRegions({}, {}, 0.3f).empty());
    }

    SECTION("un tramo con sus limites y su pico")
    {
        fill(100.0, 400.0, 0.5f);
        fill(190.0, 210.0, 0.9f);
        const auto regions = findCollisionRegions(f, sev, 0.3f);
        REQUIRE(regions.size() == 1);
        const auto& r = regions[0];
        CHECK(r.lowHz >= 100.0);
        CHECK(r.lowHz < 110.0);
        CHECK(r.highHz <= 400.0);
        CHECK(r.highHz > 360.0);
        CHECK(r.peakSeverity == 0.9f);
        CHECK(r.peakHz >= 190.0);
        CHECK(r.peakHz <= 210.0);
        CHECK(f[r.first] == r.lowHz);
        CHECK(f[r.last] == r.highHz);
    }

    SECTION("se ordenan por severidad y los huecos pequenos se unen")
    {
        fill(50.0, 80.0, 0.4f);
        fill(2000.0, 3000.0, 0.8f);
        fill(3100.0, 4000.0, 0.6f); // a menos de 1/6 de octava del anterior
        const auto regions = findCollisionRegions(f, sev, 0.3f);
        REQUIRE(regions.size() == 2);
        CHECK(regions[0].peakSeverity == 0.8f);
        CHECK(regions[0].lowHz >= 2000.0);
        CHECK(regions[0].highHz > 3500.0);
        CHECK(regions[1].peakSeverity == 0.4f);

        const auto separate = findCollisionRegions(f, sev, 0.3f, 0.0);
        CHECK(separate.size() == 3);
    }

    SECTION("los tramos estrechos se pueden descartar")
    {
        fill(1000.0, 1000.0 * std::exp2(0.08), 0.9f); // uno o dos puntos
        fill(100.0, 200.0, 0.5f);
        CHECK(findCollisionRegions(f, sev, 0.3f).size() == 2);
        const auto wide = findCollisionRegions(f, sev, 0.3f, 1.0 / 6.0, 0.5);
        REQUIRE(wide.size() == 1);
        CHECK(wide[0].peakSeverity == 0.5f);
    }

    SECTION("en una meseta, el pico queda en el centro")
    {
        fill(100.0, 1600.0, 1.0f);
        const auto regions = findCollisionRegions(f, sev, 0.3f);
        REQUIRE(regions.size() == 1);
        CHECK(std::abs(std::log2(regions[0].peakHz / 400.0)) < 0.1);
    }

    SECTION("un tramo que llega al ultimo punto")
    {
        fill(15000.0, 20000.0, 1.0f);
        const auto regions = findCollisionRegions(f, sev, 0.3f);
        REQUIRE(regions.size() == 1);
        CHECK(regions[0].last == f.size() - 1);
        CHECK_THAT(regions[0].highHz, WithinAbs(20000.0, 1e-6));
    }
}

TEST_CASE("Colisiones: promedio temporal")
{
    SECTION("el primer espectro se toma tal cual, sin bajar del suelo")
    {
        SpectrumAverager avg(3, 0.4, -120.0f);
        CHECK(avg.empty());
        const std::vector<float> in{-10.0f, -200.0f, 0.0f};
        avg.update(in, 0.05);
        CHECK_FALSE(avg.empty());
        CHECK(avg.levels() == std::vector<float>{-10.0f, -120.0f, 0.0f});
    }

    SECTION("tras una constante de tiempo, la potencia ha recorrido 1 - 1/e del camino")
    {
        SpectrumAverager avg(1, 0.4, -200.0f);
        const std::vector<float> start{-200.0f};
        const std::vector<float> target{0.0f};
        avg.update(start, 0.0);
        avg.update(target, 0.4);
        const double power = std::pow(10.0, avg.levels()[0] / 10.0);
        CHECK_THAT(power, WithinAbs(1.0 - std::exp(-1.0), 1e-4));
    }

    SECTION("sin tiempo transcurrido no cambia, y con mucho llega al objetivo")
    {
        SpectrumAverager avg(1, 0.1, -120.0f);
        avg.update(std::vector<float>{-40.0f}, 0.0);
        avg.update(std::vector<float>{0.0f}, 0.0);
        CHECK(avg.levels()[0] == -40.0f);
        for (int i = 0; i < 100; ++i)
            avg.update(std::vector<float>{-6.0f}, 0.05);
        CHECK_THAT(avg.levels()[0], WithinAbs(-6.0, 1e-3));
        // Un dt negativo se trata como cero.
        avg.update(std::vector<float>{-80.0f}, -1.0);
        CHECK_THAT(avg.levels()[0], WithinAbs(-6.0, 1e-3));
    }

    SECTION("baja hacia el suelo de forma gradual")
    {
        SpectrumAverager avg(1, 0.4, -120.0f);
        avg.update(std::vector<float>{-10.0f}, 0.05);
        avg.update(std::vector<float>{-120.0f}, 0.05);
        CHECK(avg.levels()[0] < -10.0f);
        CHECK(avg.levels()[0] > -12.0f);
    }

    SECTION("reset vuelve al estado vacio")
    {
        SpectrumAverager avg(2, 0.4, -120.0f);
        avg.update(std::vector<float>{-3.0f, -3.0f}, 0.05);
        avg.reset();
        CHECK(avg.empty());
        CHECK(avg.levels() == std::vector<float>{-120.0f, -120.0f});
        avg.update(std::vector<float>{-50.0f, -60.0f}, 0.05);
        CHECK(avg.levels() == std::vector<float>{-50.0f, -60.0f});
    }
}
