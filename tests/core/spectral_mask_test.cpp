// SPDX-License-Identifier: AGPL-3.0-or-later
//
// La máscara de la dinámica espectral, del hilo de audio a la interfaz
// (PLAN.md 5.7, bloque 6.6): la reducción de los bins a la rejilla de 192
// puntos y su publicación a 30 Hz.
#include "esceq/core/spectral_mask.h"
#include "esceq/dsp/phase_equalizer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq;
using namespace esceq::core;

namespace
{

constexpr double kFs = 48000.0;
constexpr double kBinHz = kFs / 2048.0;
constexpr int kBins = 1025;

/// Punto de la rejilla más cercano a `hz` (en escala logarítmica).
int nearestPoint(double hz)
{
    int best = 0;
    for (int i = 1; i < kSpectralMaskPoints; ++i)
        if (std::abs(std::log(spectralMaskPointHz(i) / hz)) < std::abs(std::log(spectralMaskPointHz(best) / hz)))
            best = i;
    return best;
}

/// Muestra `n` de un seno de 2 kHz con amplitud 0,5.
float tone(long long n)
{
    return static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 2000.0 * static_cast<double>(n) / kFs));
}

dsp::BandSettings spectralBell(double hz, double thresholdDb, double rangeDb, bool spectral = true)
{
    dsp::BandSettings band{true, {dsp::FilterType::Bell, hz, 0.0, 2.0}, {true, thresholdDb, rangeDb, 0.0, 5.0, 100.0}};
    band.dynamics.spectral = spectral;
    return band;
}

} // namespace

TEST_CASE("Mascara espectral: sin bins, todo a cero")
{
    SpectralMaskPoints out;
    out.fill(-3.0f);
    reduceSpectralMask({}, kBinHz, out);
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.0f; }));

    const std::vector<float> bins(kBins, -3.0f);
    out.fill(-3.0f);
    reduceSpectralMask(bins, 0.0, out);
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.0f; }));
}

TEST_CASE("Mascara espectral: en agudos, cada punto se queda con el bin que mas cambia")
{
    // Un solo bin reducido a 5 kHz, donde caben unos 8 bins por punto: el
    // punto que lo contiene lo ve entero, y los demás no ven nada.
    std::vector<float> bins(kBins, 0.0f);
    const int tone = 213; // 4992 Hz
    bins[tone] = -6.0f;
    SpectralMaskPoints out;
    reduceSpectralMask(bins, kBinHz, out);
    const int point = nearestPoint(tone * kBinHz);
    CHECK(out[static_cast<std::size_t>(point)] == -6.0f);
    for (int i = 0; i < kSpectralMaskPoints; ++i)
        if (i != point)
            CHECK(out[static_cast<std::size_t>(i)] == 0.0f);

    // Con un realce y una reducción en el mismo tramo, la mayor, con su signo.
    bins[tone + 1] = 4.0f;
    reduceSpectralMask(bins, kBinHz, out);
    CHECK(out[static_cast<std::size_t>(point)] == -6.0f);
    bins[tone + 1] = 7.0f;
    reduceSpectralMask(bins, kBinHz, out);
    CHECK(out[static_cast<std::size_t>(point)] == 7.0f);
}

TEST_CASE("Mascara espectral: en graves, entre los dos bins vecinos")
{
    // Por debajo de unos 600 Hz los bins (23,4 Hz) están más separados que los
    // puntos: una rampa en los bins sale como la rampa en la frecuencia del
    // punto (o, si un bin cae en su tramo, como ese bin: a menos de medio
    // tramo, 3,7 Hz a 200 Hz).
    std::vector<float> bins(kBins);
    for (int k = 0; k < kBins; ++k)
        bins[static_cast<std::size_t>(k)] = -0.01f * static_cast<float>(k);
    SpectralMaskPoints out;
    reduceSpectralMask(bins, kBinHz, out);
    for (int i = 0; i < kSpectralMaskPoints; ++i)
    {
        const double hz = spectralMaskPointHz(i);
        if (hz < 200.0)
            CHECK_THAT(out[static_cast<std::size_t>(i)], WithinAbs(-0.01 * hz / kBinHz, 0.002));
        // Y en agudos, el bin más alto del tramo (el que más cambia en la rampa).
        CHECK(out[static_cast<std::size_t>(i)] <= 0.0f);
    }
}

TEST_CASE("Mascara espectral: por encima del ultimo bin, cero")
{
    // 64 bins de 100 Hz: hasta 6,3 kHz.
    const std::vector<float> bins(64, -2.0f);
    SpectralMaskPoints out;
    reduceSpectralMask(bins, 100.0, out);
    for (int i = 0; i < kSpectralMaskPoints; ++i)
    {
        const double hz = spectralMaskPointHz(i);
        if (hz < 6000.0)
            CHECK(out[static_cast<std::size_t>(i)] == -2.0f);
        else if (hz > 7000.0)
            CHECK(out[static_cast<std::size_t>(i)] == 0.0f);
    }
}

TEST_CASE("Mascara espectral: se publica 30 veces por segundo de audio, con la forma de la mascara")
{
    // Un tono fuerte a 2 kHz por encima del umbral: la máscara publicada baja
    // en 2 kHz lo que marca el rango, como el medidor de la banda, y no lejos.
    for (const int block : {32, 1000, 4096})
    {
        INFO("bloque " << block);
        dsp::PhaseEqualizer eq;
        eq.setBand(0, spectralBell(2000.0, -30.0, -6.0));
        eq.prepare(kFs, 2);
        eq.update();
        SpectralMaskPublisher publisher;
        publisher.prepare(kFs);
        CHECK(publisher.interval() == 1600);

        const int total = 2 * static_cast<int>(kFs);
        std::vector<float> left(static_cast<std::size_t>(block)), right(left.size());
        int published = 0;
        const SpectralMaskFrame* last = nullptr;
        long long phase = 0;
        for (int pos = 0; pos < total; pos += block)
        {
            for (int i = 0; i < block; ++i, ++phase)
                left[static_cast<std::size_t>(i)] = right[static_cast<std::size_t>(i)] = tone(phase);
            float* channels[2] = {left.data(), right.data()};
            eq.process(channels, 2, block);
            publisher.publish(eq, block);
            if (const auto* frame = publisher.read())
            {
                ++published;
                last = frame;
            }
        }
        // Cada 1600 muestras, aunque los bloques no lo dividan (con bloques
        // de 4096, una vez por bloque: lo que se pierde no importa).
        const int expected = block <= 1600 ? total / 1600 : total / block;
        CHECK(std::abs(published - expected) <= 1);

        REQUIRE(last != nullptr);
        CHECK(last->active[0]);
        CHECK(last->anyActive());
        for (std::size_t band = 1; band < last->active.size(); ++band)
            CHECK_FALSE(last->active[band]);
        const auto& mask = last->gainsDb[0];
        const float deepest = *std::min_element(mask.begin(), mask.end());
        CHECK_THAT(deepest, WithinAbs(-6.0, 0.5));
        CHECK_THAT(deepest, WithinAbs(eq.dynamicGainDb(0), 0.3));
        CHECK_THAT(mask[static_cast<std::size_t>(nearestPoint(2000.0))], WithinAbs(-6.0, 0.5));
        // A dos octavas no se toca.
        CHECK(std::abs(mask[static_cast<std::size_t>(nearestPoint(500.0))]) < 0.1f);
        CHECK(std::abs(mask[static_cast<std::size_t>(nearestPoint(8000.0))]) < 0.1f);
    }
}

TEST_CASE("Mascara espectral: sin dinamica espectral no se publica nada; al apagarla, una vez")
{
    dsp::PhaseEqualizer eq;
    eq.setBand(0, spectralBell(2000.0, -30.0, -6.0, false));
    eq.prepare(kFs, 2);
    eq.update();
    SpectralMaskPublisher publisher;
    publisher.prepare(kFs);

    std::vector<float> left(256), right(256);
    auto run = [&](int blocks) {
        int published = 0;
        const SpectralMaskFrame* last = nullptr;
        for (int b = 0; b < blocks; ++b)
        {
            for (std::size_t i = 0; i < left.size(); ++i)
                left[i] = right[i] = tone(b * 256 + static_cast<long long>(i));
            float* channels[2] = {left.data(), right.data()};
            eq.process(channels, 2, 256);
            publisher.publish(eq, 256);
            if (const auto* frame = publisher.read())
            {
                ++published;
                last = frame;
            }
        }
        return std::pair{published, last};
    };

    // La banda dinámica normal: nada.
    CHECK(run(400).first == 0);

    // Espectral: se publica.
    eq.setBand(0, spectralBell(2000.0, -30.0, -6.0));
    eq.update();
    CHECK(run(400).first > 0);

    // Con el interruptor encendido pero la dinámica apagada, la dinámica
    // espectral sigue sonando (la latencia sigue, ADR 0025) sin bandas que
    // actúen: una publicación sin máscaras y luego nada.
    auto off = spectralBell(2000.0, -30.0, -6.0);
    off.dynamics.enabled = false;
    eq.setBand(0, off);
    eq.update();
    const auto [published, last] = run(400);
    CHECK(published == 1);
    REQUIRE(last != nullptr);
    CHECK_FALSE(last->anyActive());
    CHECK(std::all_of(last->gainsDb[0].begin(), last->gainsDb[0].end(), [](float v) { return v == 0.0f; }));
    CHECK(eq.spectralActive());

    // Y al apagar el interruptor, tampoco.
    eq.setBand(0, spectralBell(2000.0, -30.0, -6.0, false));
    eq.update();
    CHECK(run(400).first == 0);
}
