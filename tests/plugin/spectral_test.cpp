// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Dinámica espectral vista desde el host (PLAN.md 5.7, ADR 0024 y 0025): la
// latencia que se comunica y la que se mide, el aviso al host al encender la
// primera banda, el interruptor llegando al audio y al medidor, y la cola.
#include "test_support.h"

#include "esceq/core/band_model.h"
#include "esceq/dsp/linear_phase.h"
#include "esceq/dsp/spectral_dynamics.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq;
using namespace esceq::test;

namespace
{

constexpr int kBlock = 256;

void prepare(PluginProcessor& p, double rate)
{
    p.setRateAndBufferSizeDetails(rate, kBlock);
    p.prepareToPlay(rate, kBlock);
}

/// Pasa `input` (los dos canales iguales) en bloques de kBlock; `beforeBlock(b)` antes de cada uno.
template <typename BeforeBlock>
std::vector<float> render(PluginProcessor& p, const std::vector<float>& input, BeforeBlock beforeBlock)
{
    juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), kBlock);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (std::size_t pos = 0, block = 0; pos < input.size(); pos += kBlock, ++block)
    {
        beforeBlock(static_cast<int>(block));
        buffer.clear();
        for (int i = 0; i < kBlock && pos + static_cast<std::size_t>(i) < input.size(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, i, input[pos + static_cast<std::size_t>(i)]);
        p.processBlock(buffer, midi);
        for (int i = 0; i < kBlock; ++i)
            out.push_back(buffer.getSample(0, i));
    }
    out.resize(input.size());
    return out;
}

std::vector<float> render(PluginProcessor& p, const std::vector<float>& input)
{
    return render(p, input, [](int) {});
}

std::vector<float> sine(int length, double hz, double amplitude, double rate = 48000.0)
{
    std::vector<float> x(static_cast<std::size_t>(length));
    for (std::size_t n = 0; n < x.size(); ++n)
        x[n] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(n) / rate));
    return x;
}

/// Banda 1: campana de 2 kHz, sin ganancia, dinámica (umbral y rango dados), espectral si se pide.
void spectralBand(PluginProcessor& p, float thresholdDb, bool spectral)
{
    using core::BandParam;
    using core::DynParam;
    drag(p, core::bandParameter(0, BandParam::Frequency), {2000.0f});
    drag(p, core::bandParameter(0, BandParam::Q), {2.0f});
    drag(p, core::bandParameter(0, BandParam::Enabled), {1.0f});
    drag(p, core::dynamicParameter(0, DynParam::Enabled), {1.0f});
    drag(p, core::dynamicParameter(0, DynParam::Threshold), {thresholdDb});
    drag(p, core::dynamicParameter(0, DynParam::Range), {-6.0f});
    drag(p, core::dynamicParameter(0, DynParam::Knee), {0.0f});
    drag(p, core::spectralParameter(0), {spectral ? 1.0f : 0.0f});
}

/// Amplitud de la componente de `hz` entre `from` y `to`, con ventana de Blackman.
double toneAmplitude(const std::vector<float>& x, double hz, std::size_t from, std::size_t to)
{
    double re = 0.0, im = 0.0, sum = 0.0;
    const double length = static_cast<double>(to - from);
    for (std::size_t i = from; i < to; ++i)
    {
        const double u = static_cast<double>(i - from) / length;
        const double w =
            0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * u) + 0.08 * std::cos(4.0 * std::numbers::pi * u);
        const double phase = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / 48000.0;
        re += w * x[i] * std::cos(phase);
        im -= w * x[i] * std::sin(phase);
        sum += w;
    }
    return 2.0 * std::hypot(re, im) / sum;
}

} // namespace

TEST_CASE("Espectral: la latencia comunicada coincide con la medida en los cuatro modos")
{
    // Criterio de PLAN.md 5.7, a 44,1, 48, 96 y 192 kHz. Un impulso, por
    // debajo del umbral, sale entero en la latencia que conoce el host.
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
        for (const float mode : {0.0f, 1.0f, 2.0f, 3.0f})
        {
            INFO("fs " << rate << ", modo " << mode);
            auto p = makeProcessor();
            spectralBand(*p, 0.0f, true);
            drag(*p, core::kPhaseMode, {mode});
            prepare(*p, rate);
            const auto phase = core::phaseModeFromChoice(mode);
            const int expected =
                dsp::linearPhaseSize(phase, rate).latency() + dsp::SpectralDynamics::latencySamples(rate);
            CHECK(p->getLatencySamples() == expected);
            CHECK(p->engineLatencySamples() == expected);
            CHECK(p->getTailLengthSeconds() >= static_cast<double>(expected) / rate);

            std::vector<float> input(static_cast<std::size_t>(expected + 4096), 0.0f);
            input[0] = 0.5f;
            const auto out = render(*p, input);
            std::size_t peak = 0;
            for (std::size_t i = 0; i < out.size(); ++i)
                if (std::abs(out[i]) > std::abs(out[peak]))
                    peak = i;
            CHECK(peak == static_cast<std::size_t>(expected));
            CHECK_THAT(out[peak], WithinAbs(0.5, 1.0e-4));
        }
}

TEST_CASE("Espectral: apagada en todas las bandas, la latencia y la cola de siempre")
{
    // El sonido, bit a bit, lo comprueba "Fase: sin dinamica espectral..." (phase_test.cpp).
    for (const float mode : {0.0f, 1.0f, 2.0f, 3.0f})
    {
        INFO("modo " << mode);
        auto p = makeProcessor();
        spectralBand(*p, -30.0f, false);
        drag(*p, core::kPhaseMode, {mode});
        prepare(*p, 48000.0);
        const auto size = dsp::linearPhaseSize(core::phaseModeFromChoice(mode), 48000.0);
        CHECK(p->getLatencySamples() == size.latency());
        CHECK(p->getTailLengthSeconds() == static_cast<double>(size.latency() + size.firLength / 2) / 48000.0);
    }
}

TEST_CASE("Espectral: encender la primera banda avisa al host y no da chasquidos")
{
    // Determinista, como el cambio de modo: el trabajo del hilo de control lo
    // hace el test con flushControlThread().
    auto p = makeProcessor();
    spectralBand(*p, -30.0f, false);
    prepare(*p, 48000.0);
    REQUIRE(p->getLatencySamples() == 0);

    const int spectral = dsp::SpectralDynamics::latencySamples(48000.0);
    const auto input = sine(kBlock * 4, 440.0, 0.4);
    for (const auto& [on, expected] : {std::pair{1.0f, spectral}, std::pair{0.0f, 0}})
    {
        const int before = p->getLatencySamples();
        drag(*p, core::spectralParameter(0), {on});
        (void)render(*p, input);
        // El cambio llega al audio tras bajar a silencio en 10 ms.
        for (int i = 0; i < 40 && p->engineLatencySamples() != expected; ++i)
            (void)render(*p, input);
        REQUIRE(p->engineLatencySamples() == expected);
        CHECK(p->getLatencySamples() == before);
        p->flushControlThread();
        CHECK(pumpUntil([&] { return p->getLatencySamples() == expected; }));
    }

    // Con un seno, encender y apagar no salta: un seno de 440 Hz a 0,4 cambia
    // como mucho 0,023 por muestra.
    const auto tone = sine(48000 * 2, 440.0, 0.4);
    const auto out = render(*p, tone,
                            [&](int block)
                            {
                                if (block == 40)
                                    drag(*p, core::spectralParameter(0), {1.0f});
                                if (block == 250)
                                    drag(*p, core::spectralParameter(0), {0.0f});
                            });
    float worst = 0.0f;
    for (std::size_t i = 1; i < out.size(); ++i)
        worst = std::max(worst, std::abs(out[i] - out[i - 1]));
    CHECK(worst < 0.03f);
}

TEST_CASE("Espectral: el interruptor de la banda llega al audio y al medidor")
{
    // Un tono fuerte por encima del umbral y uno débil a 1/3 de octava: la
    // banda espectral solo baja el fuerte; la normal, los dos.
    const double quietHz = 2000.0 * std::exp2(1.0 / 3.0);
    auto input = sine(48000, 2000.0, 0.5);
    const auto quiet = sine(48000, quietHz, 0.01);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] += quiet[i];
    for (const bool spectral : {true, false})
    {
        INFO((spectral ? "espectral" : "normal"));
        auto p = makeProcessor();
        spectralBand(*p, -30.0f, spectral);
        prepare(*p, 48000.0);
        const auto out = render(*p, input);
        const auto lag = static_cast<std::size_t>(p->getLatencySamples());
        const std::vector<float> aligned(out.begin() + static_cast<std::ptrdiff_t>(lag), out.end());
        const double loud =
            20.0 * std::log10(toneAmplitude(aligned, 2000.0, 20000, 40000) / toneAmplitude(input, 2000.0, 20000, 40000));
        const double weak = 20.0 * std::log10(toneAmplitude(aligned, quietHz, 20000, 40000) /
                                              toneAmplitude(input, quietHz, 20000, 40000));
        CHECK_THAT(loud, WithinAbs(-6.0, 0.5));
        CHECK_THAT(p->dynamicGainDb(0), WithinAbs(-6.0, 0.5));
        if (spectral)
            CHECK(std::abs(weak) < 0.2);
        else
            CHECK(weak < -1.0);
    }
}
