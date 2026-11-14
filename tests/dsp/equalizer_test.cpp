// SPDX-License-Identifier: MIT
#include "esceq/dsp/equalizer.h"

#include "filter_test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;
using namespace esceq::test;

namespace {

constexpr double kFs = 48000.0;

std::vector<float> noise(int numSamples, std::uint32_t seed = 4242u)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples));
    std::uint32_t state = seed;
    for (auto& sample : out)
    {
        state = state * 1664525u + 1013904223u;
        sample = 0.5f * (static_cast<float>(state >> 8) / 8388608.0f - 1.0f);
    }
    return out;
}

std::vector<float> sine(int numSamples, double hz, double amplitude = 0.5)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kFs));
    return out;
}

std::vector<float> run(Equalizer& eq, std::vector<float> signal, int blockSize)
{
    const int total = static_cast<int>(signal.size());
    for (int pos = 0; pos < total; pos += blockSize)
    {
        float* channel = signal.data() + pos;
        eq.process(&channel, 1, std::min(blockSize, total - pos));
    }
    return signal;
}

BandSettings bell(double hz, double gain, double q = 1.0)
{
    return {true, {FilterType::Bell, hz, gain, q}};
}

/// Mayor diferencia entre muestras consecutivas: detecta chasquidos.
float maxStep(const std::vector<float>& signal)
{
    float worst = 0.0f;
    for (std::size_t i = 1; i < signal.size(); ++i)
        worst = std::max(worst, std::abs(signal[i] - signal[i - 1]));
    return worst;
}

} // namespace

TEST_CASE("Equalizer: con todas las bandas desactivadas el audio pasa bit a bit")
{
    Equalizer eq;
    eq.prepare(kFs);
    const auto input = noise(4096);
    CHECK(run(eq, input, 100) == input);
}

TEST_CASE("Equalizer: una banda asentada suena igual que BandFilter")
{
    for (auto type : kAllTypes)
    {
        const BandSettings settings{true, {type, 700.0, 5.0, 2.0, 36}};

        Equalizer eq;
        eq.setBand(5, settings);
        eq.prepare(kFs);

        BandFilter reference;
        reference.setDesign(designBand(settings.filter, kFs));

        auto expected = noise(4096);
        float* channel = expected.data();
        reference.process(&channel, 1, 4096);

        INFO(typeName(type));
        CHECK(run(eq, noise(4096), 256) == expected);
    }
}

TEST_CASE("Equalizer: varias bandas en serie suman sus respuestas en dB")
{
    Equalizer eq;
    eq.setBand(0, bell(100.0, 6.0));
    eq.setBand(1, {true, {FilterType::HighCut, 5000.0, 0.0, kButterworthQ, 24}});
    eq.setBand(7, bell(1000.0, -9.0, 4.0));
    eq.prepare(kFs);

    std::vector<float> impulse(static_cast<std::size_t>(kFs), 0.0f);
    impulse[0] = 1.0f;
    const auto ir = run(eq, impulse, 512);

    for (double f : {50.0, 100.0, 1000.0, 3000.0, 5000.0, 12000.0})
    {
        const double expected = magnitudeDb(designBand(eq.target(0).filter, kFs), f, kFs) +
                                magnitudeDb(designBand(eq.target(1).filter, kFs), f, kFs) +
                                magnitudeDb(designBand(eq.target(7).filter, kFs), f, kFs);
        INFO(f << " Hz");
        CHECK_THAT(measuredMagnitudeDb(ir, f, kFs), WithinAbs(expected, 0.01));
    }
}

TEST_CASE("Equalizer: con parametros fijos no depende del tamano de bloque")
{
    Equalizer reference;
    reference.prepare(kFs);
    reference.setBand(0, bell(300.0, 8.0, 3.0));
    reference.setBand(3, {true, {FilterType::LowCut, 80.0, 0.0, 1.0, 48}});
    const auto expected = run(reference, noise(16384), 16384);

    for (int blockSize : {1, 13, 64, 511, 4096})
    {
        Equalizer eq;
        eq.prepare(kFs);
        eq.setBand(0, bell(300.0, 8.0, 3.0));
        eq.setBand(3, {true, {FilterType::LowCut, 80.0, 0.0, 1.0, 48}});
        INFO("bloque " << blockSize);
        CHECK(run(eq, noise(16384), blockSize) == expected);
    }
}

TEST_CASE("Equalizer: los cambios de ganancia y frecuencia se suavizan hasta el objetivo")
{
    Equalizer eq;
    eq.setBand(0, bell(200.0, 0.0));
    eq.prepare(kFs);

    eq.setBand(0, bell(4000.0, 12.0, 3.0));
    CHECK_FALSE(eq.isSettled());

    (void) run(eq, std::vector<float>(static_cast<std::size_t>(kFs * 0.5), 0.0f), 512);
    CHECK(eq.isSettled());

    // Ya asentado, el estado interno coincide con el objetivo.
    CHECK(eq.target(0) == bell(4000.0, 12.0, 3.0));
}

TEST_CASE("Equalizer: mover una banda con fuerza no produce chasquidos")
{
    // Seno de 1 kHz: su paso máximo entre muestras es 2π·f/fs·A = 0,065.
    const auto input = sine(48000, 1000.0);
    const float clean = maxStep(input);

    Equalizer eq;
    eq.setBand(0, bell(1000.0, 0.0, 2.0));
    eq.prepare(kFs);

    auto signal = input;
    const int block = 128;
    for (int pos = 0, n = 0; pos < 48000; pos += block, ++n)
    {
        // Cada bloque salta la ganancia entre -15 y +15 dB y la frecuencia una octava.
        const bool up = (n % 2) == 0;
        eq.setBand(0, bell(up ? 1400.0 : 700.0, up ? 15.0 : -15.0, 2.0));
        float* ch = signal.data() + pos;
        eq.process(&ch, 1, block);
    }

    // Con +15 dB la señal crece hasta x5,6; un chasquido sería un salto muy superior.
    INFO("paso limpio " << clean << ", paso con modulacion " << maxStep(signal));
    CHECK(maxStep(signal) < clean * 8.0f);
    CHECK(std::all_of(signal.begin(), signal.end(), [](float v) { return std::isfinite(v); }));
}

TEST_CASE("Equalizer: activar, desactivar y cambiar de tipo hace un fundido sin saltos")
{
    const auto input = sine(24000, 220.0);
    const float clean = maxStep(input);

    Equalizer eq;
    eq.prepare(kFs);

    auto signal = input;
    const std::vector<BandSettings> sequence = {
        bell(220.0, 18.0, 1.0),
        {true, {FilterType::LowCut, 2000.0, 0.0, kButterworthQ, 96}},
        {true, {FilterType::LowCut, 2000.0, 0.0, kButterworthQ, 12}},
        {true, {FilterType::Notch, 220.0, 0.0, 2.0, 12}},
        BandSettings{},
    };

    const int block = 4800;
    for (std::size_t i = 0; i < sequence.size(); ++i)
    {
        eq.setBand(2, sequence[i]);
        float* ch = signal.data() + static_cast<int>(i) * block;
        eq.process(&ch, 1, block);
    }

    // +18 dB multiplica por 8 el seno; el fundido no debe añadir nada encima.
    INFO("paso limpio " << clean << ", paso con cambios " << maxStep(signal));
    CHECK(maxStep(signal) < clean * 8.5f);
    CHECK(eq.isSettled());
}

TEST_CASE("Equalizer: bypass hace un fundido y, completo, deja pasar el audio bit a bit")
{
    Equalizer eq;
    eq.setBand(0, bell(1000.0, 12.0));
    eq.prepare(kFs);

    const auto input = sine(9600, 1000.0);
    auto signal = input;
    eq.setBypassed(true);
    float* ch = signal.data();
    eq.process(&ch, 1, 9600);

    // Tras el fundido (10 ms = 480 muestras) la salida es la entrada.
    CHECK(std::equal(signal.begin() + 600, signal.end(), input.begin() + 600));
    CHECK(maxStep(signal) < maxStep(input) * 4.5f);
    CHECK(eq.isSettled());

    eq.setBypassed(false);
    auto back = input;
    ch = back.data();
    eq.process(&ch, 1, 9600);
    CHECK(maxStep(back) < maxStep(input) * 4.5f);
    CHECK(eq.isSettled());
}

TEST_CASE("Equalizer: ajustes absurdos no producen NaN")
{
    Equalizer eq;
    eq.prepare(44100.0);
    for (int band = 0; band < kMaxBands; ++band)
        eq.setBand(band, {true, {static_cast<FilterType>(band % 9), band * 1.0e4 - 5.0, 1000.0, -3.0, 999}});
    eq.setBand(-1, bell(100.0, 3.0));
    eq.setBand(kMaxBands, bell(100.0, 3.0));

    std::vector<float> left = noise(8192, 1u), right = noise(8192, 2u);
    float* channels[] = {left.data(), right.data()};
    eq.process(channels, 2, 8192);

    CHECK(std::all_of(left.begin(), left.end(), [](float v) { return std::isfinite(v); }));
    CHECK(std::all_of(right.begin(), right.end(), [](float v) { return std::isfinite(v); }));
}

TEST_CASE("Equalizer: canales extra se dejan intactos")
{
    Equalizer eq;
    eq.setBand(0, bell(1000.0, 6.0));
    eq.prepare(kFs);

    auto a = noise(512, 1u), b = noise(512, 2u), c = noise(512, 3u);
    const auto original = c;
    float* channels[] = {a.data(), b.data(), c.data()};
    eq.process(channels, 3, 512);
    CHECK(c == original);
}

TEST_CASE("Ecualizador: un NaN o un infinito en la entrada no lo deja estropeado")
{
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity()})
    {
        Equalizer eq;
        BandSettings bell;
        bell.enabled = true;
        bell.filter = {FilterType::Bell, 1000.0, 6.0, 1.0};
        eq.setBand(0, bell);
        BandSettings cut = bell;
        cut.filter = {FilterType::LowCut, 80.0, 0.0, kButterworthQ, 96};
        eq.setBand(1, cut);
        BandSettings dynamic = bell;
        dynamic.filter.frequencyHz = 3000.0;
        dynamic.dynamics.enabled = true;
        dynamic.dynamics.thresholdDb = -40.0;
        eq.setBand(2, dynamic);
        eq.prepare(kFs);

        const int block = 512;
        auto left = noise(block * 40, 1u);
        auto right = noise(block * 40, 2u);
        left[block * 3 + 100] = bad;
        for (int b = 0; b < 40; ++b)
        {
            float* channels[] = {left.data() + b * block, right.data() + b * block};
            eq.process(channels, 2, block);
            const bool finite = std::all_of(channels[0], channels[0] + block, [](float x) { return std::isfinite(x); }) &&
                                std::all_of(channels[1], channels[1] + block, [](float x) { return std::isfinite(x); });
            REQUIRE(finite);
            if (b == 3)
            {
                // El bloque que trae el valor no finito sale en silencio.
                CHECK(std::all_of(channels[0], channels[0] + block, [](float x) { return x == 0.0f; }));
            }
        }
        // Y después vuelve a sonar.
        CHECK(std::any_of(left.end() - block, left.end(), [](float x) { return std::abs(x) > 0.01f; }));
    }
}

TEST_CASE("Ecualizador: ajustes no finitos se sanean")
{
    for (const double bad : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()})
    {
        Equalizer eq;
        BandSettings s;
        s.enabled = true;
        s.filter = {FilterType::Bell, 1000.0, 6.0, 1.0};
        eq.setBand(0, s);
        eq.prepare(kFs);
        s.filter = {FilterType::Bell, bad, bad, bad};
        eq.setBand(0, s);

        auto signal = noise(4800);
        float* channel = signal.data();
        eq.process(&channel, 1, static_cast<int>(signal.size()));
        CHECK(std::all_of(signal.begin(), signal.end(), [](float x) { return std::isfinite(x); }));
        CHECK(std::any_of(signal.begin(), signal.end(), [](float x) { return x != 0.0f; }));
    }
}

TEST_CASE("Ecualizador: dos cambios estructurales seguidos no producen saltos")
{
    // Se activa un corte de graves y, antes de que acabe su fundido de 10 ms,
    // cambia a corte de agudos. Antes, el segundo cambio descartaba de golpe
    // el filtro que se estaba desvaneciendo.
    for (const int gap : {1, 48, 240, 470})
    {
        Equalizer eq;
        eq.prepare(kFs);
        BandSettings lowCut;
        lowCut.enabled = true;
        lowCut.filter = {FilterType::LowCut, 2000.0, 0.0, kButterworthQ, 48};
        BandSettings highCut = lowCut;
        highCut.filter.type = FilterType::HighCut;

        const auto signal = sine(4800, 300.0);
        std::vector<float> out(signal.size());
        for (std::size_t i = 0; i < signal.size(); ++i)
        {
            if (i == 1000)
                eq.setBand(0, lowCut);
            if (i == 1000 + static_cast<std::size_t>(gap))
                eq.setBand(0, highCut);
            float sample = signal[i];
            float* channel = &sample;
            eq.process(&channel, 1, 1);
            out[i] = sample;
        }
        const float cleanStep = maxStep(signal);
        INFO("segundo cambio " << gap << " muestras despues");
        CHECK(maxStep(out) <= 1.05f * cleanStep);
    }
}
