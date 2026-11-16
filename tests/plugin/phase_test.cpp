// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Modos de fase vistos desde el host: latencia que se le comunica, cola,
// render sin tiempo real, bypass y estado guardado.
#include "test_support.h"

#include "esceq/core/band_model.h"
#include "esceq/dsp/linear_phase.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq;
using namespace esceq::test;

namespace
{

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

void prepare(PluginProcessor& p)
{
    p.setRateAndBufferSizeDetails(kRate, kBlock);
    p.prepareToPlay(kRate, kBlock);
}

/// Pasa `input` (los dos canales iguales) por el procesador en bloques de kBlock.
/// `beforeBlock(b)` se llama antes de cada bloque.
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

std::vector<float> impulse(int length)
{
    std::vector<float> x(static_cast<std::size_t>(length), 0.0f);
    x[0] = 1.0f;
    return x;
}

std::vector<float> noise(int length, std::uint32_t seed = 3u)
{
    std::vector<float> x(static_cast<std::size_t>(length));
    for (auto& v : x)
    {
        seed = seed * 1664525u + 1013904223u;
        v = 0.4f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
    return x;
}

/// Magnitud en dB de la transformada de `x` en `hz`.
double magnitudeDb(const std::vector<float>& x, double hz)
{
    const double w = 2.0 * std::numbers::pi * hz / kRate;
    double re = 0.0, im = 0.0;
    for (std::size_t n = 0; n < x.size(); ++n)
    {
        re += x[n] * std::cos(w * static_cast<double>(n));
        im -= x[n] * std::sin(w * static_cast<double>(n));
    }
    return 20.0 * std::log10(std::hypot(re, im));
}

void bellAt1k(PluginProcessor& p, float gainDb)
{
    using namespace core;
    drag(p, bandParameter(0, BandParam::Frequency), {1000.0f});
    drag(p, bandParameter(0, BandParam::Gain), {gainDb});
    drag(p, bandParameter(0, BandParam::Enabled), {1.0f});
}

} // namespace

TEST_CASE("Fase: por defecto IIR, sin latencia ni cola")
{
    auto p = makeProcessor();
    prepare(*p);
    CHECK(valueOf(*p, core::kPhaseMode) == 0.0f);
    CHECK(p->getLatencySamples() == 0);
    CHECK(p->getTailLengthSeconds() == 0.0);
}

TEST_CASE("Fase: preparar en un modo lineal comunica la latencia antes del primer bloque")
{
    const std::pair<float, int> cases[] = {{1.0f, 1280}, {2.0f, 4608}, {3.0f, 17408}};
    for (const auto& [choice, latency] : cases)
    {
        auto p = makeProcessor();
        drag(*p, core::kPhaseMode, {choice});
        prepare(*p);
        CHECK(p->getLatencySamples() == latency);
        CHECK(p->engineLatencySamples() == latency);
        CHECK(p->getTailLengthSeconds() > static_cast<double>(latency) / kRate);
    }
}

TEST_CASE("Fase: un impulso sale retrasado la latencia, con la magnitud de la curva")
{
    auto p = makeProcessor();
    bellAt1k(*p, 6.0f);
    drag(*p, core::kPhaseMode, {2.0f});
    prepare(*p);
    const int latency = p->getLatencySamples();
    const auto out = render(*p, impulse(latency + 8192));

    std::size_t peak = 0;
    for (std::size_t i = 0; i < out.size(); ++i)
        if (std::abs(out[i]) > std::abs(out[peak]))
            peak = i;
    CHECK(peak == static_cast<std::size_t>(latency));
    // La magnitud de la curva: la del filtro de la banda tal y como se dibuja.
    const auto band = core::bandSettings(p->currentValues(), 0, p->filterAlgorithm());
    for (const double hz : {100.0, 1000.0, 5000.0})
        CHECK_THAT(magnitudeDb(out, hz),
                   WithinAbs(dsp::magnitudeDb(dsp::designBand(band.filter, kRate), hz, kRate), 0.02));
}

TEST_CASE("Fase: cambiar de modo con el audio en marcha avisa al host desde el hilo de mensajes")
{
    auto p = makeProcessor();
    bellAt1k(*p, 4.0f);
    prepare(*p);
    REQUIRE(p->getLatencySamples() == 0);

    drag(*p, core::kPhaseMode, {1.0f});
    // El hilo de diseño prepara el modo; el audio sigue en IIR mientras.
    const auto input = noise(kBlock * 4);
    for (int i = 0; i < 400 && p->engineLatencySamples() == 0; ++i)
    {
        (void)render(*p, input);
        pump(5);
    }
    REQUIRE(p->engineLatencySamples() == 1280);

    // El host se entera desde el hilo de mensajes.
    pump(1000 / PluginProcessor::kLatencyPollHz + 100);
    CHECK(p->getLatencySamples() == 1280);

    drag(*p, core::kPhaseMode, {0.0f});
    for (int i = 0; i < 400 && p->engineLatencySamples() != 0; ++i)
        (void)render(*p, input);
    pump(1000 / PluginProcessor::kLatencyPollHz + 100);
    CHECK(p->getLatencySamples() == 0);
}

TEST_CASE("Fase: sin tiempo real el render no depende del hilo de diseno")
{
    // Con una automatización que cambia la banda en mitad del render, dos
    // renders dan exactamente lo mismo: cada FIR se diseña en su bloque.
    const auto input = noise(48000);
    std::vector<float> first;
    for (int run = 0; run < 2; ++run)
    {
        auto p = makeProcessor();
        p->setNonRealtime(true);
        bellAt1k(*p, 0.0f);
        drag(*p, core::kPhaseMode, {1.0f});
        prepare(*p);
        const auto out = render(*p, input,
                                [&](int block)
                                {
                                    if (block % 20 == 10)
                                        drag(*p, core::bandParameter(0, core::BandParam::Gain),
                                             {static_cast<float>(block % 40) - 15.0f});
                                });
        if (run == 0)
            first = out;
        else
            CHECK(out == first);
    }
}

TEST_CASE("Fase: con bypass en fase lineal la salida es la entrada retrasada")
{
    auto p = makeProcessor();
    bellAt1k(*p, 9.0f);
    drag(*p, core::kPhaseMode, {1.0f});
    drag(*p, core::kBypass, {1.0f});
    prepare(*p);
    const int latency = p->getLatencySamples();
    REQUIRE(latency == 1280);
    const auto input = noise(latency + 4000);
    const auto out = render(*p, input);
    bool exact = true;
    for (std::size_t i = static_cast<std::size_t>(latency); i < input.size(); ++i)
        exact = exact && out[i] == input[i - static_cast<std::size_t>(latency)];
    CHECK(exact);
}

TEST_CASE("Fase: el modo viaja con el estado del proyecto")
{
    auto a = makeProcessor();
    drag(*a, core::kPhaseMode, {3.0f});
    juce::MemoryBlock state;
    a->getStateInformation(state);
    CHECK(state.toString().contains("phase_mode 3"));

    auto b = makeProcessor();
    b->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(valueOf(*b, core::kPhaseMode) == 3.0f);
    prepare(*b);
    CHECK(b->getLatencySamples() == 17408);
}
