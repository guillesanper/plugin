// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Compensación automática de ganancia vista desde el host: apagada en los
// proyectos anteriores, bit a bit con la curva plana, sin chasquidos al
// encenderla, igual en render, con bypass y en los cuatro modos de fase.
#include "test_support.h"

#include "../dsp/loudness_test_utils.h"
#include "esceq/core/band_model.h"
#include "esceq/dsp/auto_gain.h"
#include "esceq/dsp/decibels.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq;
using namespace esceq::test;

namespace
{

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
using Stereo = std::vector<std::vector<float>>;

void prepare(PluginProcessor& p)
{
    p.setRateAndBufferSizeDetails(kRate, kBlock);
    p.prepareToPlay(kRate, kBlock);
}

/// Pasa `input` (dos canales) por el procesador en bloques de kBlock.
/// `beforeBlock(b)` se llama antes de cada bloque.
template <typename BeforeBlock> Stereo render(PluginProcessor& p, const Stereo& input, BeforeBlock beforeBlock)
{
    juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), kBlock);
    juce::MidiBuffer midi;
    Stereo out(2);
    const std::size_t length = input[0].size();
    for (std::size_t pos = 0, block = 0; pos < length; pos += kBlock, ++block)
    {
        beforeBlock(static_cast<int>(block));
        buffer.clear();
        for (int i = 0; i < kBlock && pos + static_cast<std::size_t>(i) < length; ++i)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, i, input[static_cast<std::size_t>(ch)][pos + static_cast<std::size_t>(i)]);
        p.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                out[static_cast<std::size_t>(ch)].push_back(buffer.getSample(ch, i));
    }
    for (auto& channel : out)
        channel.resize(length);
    return out;
}

Stereo render(PluginProcessor& p, const Stereo& input)
{
    return render(p, input, [](int) {});
}

Stereo noise(int length)
{
    return {pinkNoise(static_cast<std::size_t>(length), 11), pinkNoise(static_cast<std::size_t>(length), 12)};
}

Stereo constant(int length, float value)
{
    return Stereo(2, std::vector<float>(static_cast<std::size_t>(length), value));
}

void setBand(PluginProcessor& p, int band, float type, float frequency, float gain, float q = 1.0f)
{
    using namespace core;
    drag(p, bandParameter(band, BandParam::Type), {type});
    drag(p, bandParameter(band, BandParam::Frequency), {frequency});
    drag(p, bandParameter(band, BandParam::Gain), {gain});
    drag(p, bandParameter(band, BandParam::Q), {q});
    drag(p, bandParameter(band, BandParam::Enabled), {1.0f});
}

/// Compensación de la curva actual del procesador, calculada aparte.
double expectedDb(PluginProcessor& p)
{
    const auto values = p.currentValues();
    return dsp::autoGainDb(core::allBandSettings(values, p.filterAlgorithm()), kRate, 2);
}

std::string fixture(const char* name)
{
    std::ifstream in(std::string(ESCEQ_FIXTURES_DIR) + "/state/" + name, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

TEST_CASE("Compensacion: apagada por defecto y en los proyectos anteriores")
{
    auto p = makeProcessor();
    CHECK(valueOf(*p, core::kAutoGain) == 0.0f);
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v2_ui.txt", "v3_classic.txt", "v3_matched.txt"})
    {
        INFO(name);
        drag(*p, core::kAutoGain, {1.0f});
        const auto text = fixture(name);
        REQUIRE(!text.empty());
        p->setStateInformation(text.data(), static_cast<int>(text.size()));
        CHECK(valueOf(*p, core::kAutoGain) == 0.0f);
    }
}

TEST_CASE("Compensacion: con la curva plana la salida es bit a bit la de apagada")
{
    const auto input = noise(24000);
    Stereo off;
    for (const bool on : {false, true})
    {
        auto p = makeProcessor();
        setBand(*p, 0, 0.0f, 1000.0f, 0.0f);  // campana a 0 dB
        setBand(*p, 1, 7.0f, 3000.0f, 0.0f);  // pasa todo
        setBand(*p, 2, 8.0f, 500.0f, 0.0f);   // tilt a 0 dB
        drag(*p, core::kOutputGain, {-3.0f});
        drag(*p, core::kAutoGain, {on ? 1.0f : 0.0f});
        prepare(*p);
        const auto out = render(*p, input);
        if (!on)
            off = out;
        else
            CHECK(out == off);
    }
}

TEST_CASE("Compensacion: la salida cambia lo calculado, sumado a la ganancia de salida")
{
    const auto input = noise(24000);
    Stereo reference;
    for (const bool on : {false, true})
    {
        auto p = makeProcessor();
        p->setNonRealtime(true);
        setBand(*p, 0, 0.0f, 1000.0f, 6.0f);
        setBand(*p, 1, 2.0f, 8000.0f, 3.0f, 0.7f);
        drag(*p, core::kOutputGain, {2.0f});
        drag(*p, core::kAutoGain, {on ? 1.0f : 0.0f});
        prepare(*p);
        const auto out = render(*p, input);
        if (!on)
        {
            reference = out;
            continue;
        }
        const double compensation = expectedDb(*p);
        CHECK(compensation < -1.0);
        const float gain = dsp::dbToGain(static_cast<float>(compensation));
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t i = 0; i < out[ch].size(); ++i)
                REQUIRE_THAT(out[ch][i], WithinAbs(reference[ch][i] * gain, 1.0e-5));
    }
}

TEST_CASE("Compensacion: al encender y apagar no hay chasquidos")
{
    // Continua por una campana en 1 kHz: la curva no la toca, así que lo que
    // sale es la ganancia. Con el hilo de control propio, como en un host.
    auto p = makeProcessor();
    setBand(*p, 0, 0.0f, 1000.0f, 12.0f);
    prepare(*p);
    const auto input = constant(kBlock, 0.5f);
    for (int i = 0; i < 100; ++i)
        render(*p, input);
    const float gain = dsp::dbToGain(static_cast<float>(expectedDb(*p)));
    REQUIRE(gain < 0.8f);

    for (const bool on : {true, false})
    {
        drag(*p, core::kAutoGain, {on ? 1.0f : 0.0f});
        const float from = on ? 0.5f : 0.5f * gain;
        const float to = on ? 0.5f * gain : 0.5f;
        // Una rampa de 20 ms: como mucho esto por muestra.
        const float maxStep = std::abs(to - from) / (0.02f * static_cast<float>(kRate)) * 1.01f + 1.0e-6f;
        std::vector<float> out{from};
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::abs(out.back() - to) > 1.0e-5f && std::chrono::steady_clock::now() < limit)
        {
            const auto block = render(*p, input)[0];
            out.insert(out.end(), block.begin(), block.end());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        INFO((on ? "al encender" : "al apagar"));
        CHECK_THAT(out.back(), WithinAbs(to, 1.0e-5));
        float worst = 0.0f;
        for (std::size_t i = 1; i < out.size(); ++i)
            worst = std::max(worst, std::abs(out[i] - out[i - 1]));
        CHECK(worst <= maxStep);
    }
}

TEST_CASE("Compensacion: al volver a encenderla no se aplica un valor antiguo")
{
    // Encendida con un realce (compensa hacia abajo), apagada, la banda pasa
    // a atenuar y se vuelve a encender: la ganancia espera al valor nuevo
    // (hacia arriba) y no baja nunca hacia el antiguo.
    auto p = makeProcessor();
    setBand(*p, 0, 0.0f, 1000.0f, 12.0f);
    prepare(*p);
    const auto input = constant(kBlock, 0.5f);
    auto runFor = [&](int milliseconds)
    {
        std::vector<float> out;
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        while (std::chrono::steady_clock::now() < until)
        {
            const auto block = render(*p, input)[0];
            out.insert(out.end(), block.begin(), block.end());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return out;
    };
    runFor(200);
    drag(*p, core::kAutoGain, {1.0f});
    REQUIRE(runFor(300).back() < 0.45f);
    drag(*p, core::kAutoGain, {0.0f});
    REQUIRE_THAT(runFor(300).back(), WithinAbs(0.5f, 1.0e-5));

    drag(*p, core::bandParameter(0, core::BandParam::Gain), {-12.0f});
    drag(*p, core::kAutoGain, {1.0f});
    const auto out = runFor(300);
    CHECK(*std::min_element(out.begin(), out.end()) >= 0.5f - 1.0e-5f);
    CHECK(out.back() > 0.55f);
}

TEST_CASE("Compensacion: con bypass no hay compensacion")
{
    auto p = makeProcessor();
    p->setNonRealtime(true);
    setBand(*p, 0, 0.0f, 1000.0f, 9.0f);
    drag(*p, core::kAutoGain, {1.0f});
    drag(*p, core::kBypass, {1.0f});
    prepare(*p);
    const auto input = noise(9600);
    CHECK(render(*p, input) == input);
}

TEST_CASE("Compensacion: sin tiempo real el render no depende del hilo de control")
{
    // Con una automatización que mueve la banda en mitad del render, dos
    // renders dan exactamente lo mismo: cada compensación se calcula en su bloque.
    const auto input = noise(48000);
    Stereo first;
    for (int run = 0; run < 2; ++run)
    {
        auto p = makeProcessor();
        p->setNonRealtime(true);
        setBand(*p, 0, 0.0f, 1000.0f, 0.0f);
        drag(*p, core::kAutoGain, {1.0f});
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

TEST_CASE("Compensacion: ruido rosa suena igual de fuerte en los cuatro modos de fase")
{
    const auto input = noise(6 * 48000);
    const double dry = integratedLoudness(input);
    for (const float mode : {0.0f, 1.0f, 2.0f, 3.0f})
    {
        INFO("modo " << mode);
        auto p = makeProcessor();
        p->setNonRealtime(true);
        setBand(*p, 0, 3.0f, 80.0f, 0.0f, 0.7f); // corte de graves
        setBand(*p, 1, 0.0f, 300.0f, -3.0f, 1.4f);
        setBand(*p, 2, 0.0f, 3000.0f, 5.0f, 2.0f);
        setBand(*p, 3, 2.0f, 10000.0f, 3.0f, 0.7f);
        drag(*p, core::kPhaseMode, {mode});
        drag(*p, core::kAutoGain, {1.0f});
        prepare(*p);
        // La latencia de la fase lineal se descarta al medir: el principio es silencio.
        auto out = render(*p, input);
        const auto latency = static_cast<std::ptrdiff_t>(p->getLatencySamples());
        for (auto& channel : out)
            channel.erase(channel.begin(), channel.begin() + latency);
        CHECK_THAT(integratedLoudness(out), WithinAbs(dry, 0.5));
    }
}

TEST_CASE("Compensacion: los presets no la cambian ni la guardan")
{
    auto p = makeProcessor();
    drag(*p, core::kAutoGain, {1.0f});
    REQUIRE(p->presets().load({"Gentle Master", true}));
    CHECK(valueOf(*p, core::kAutoGain) == 1.0f);
    CHECK_FALSE(p->presets().isModified());

    drag(*p, core::kAutoGain, {0.0f});
    CHECK_FALSE(p->presets().isModified()); // cambiarla no modifica el preset
    p->presets().loadDefault();
    CHECK(valueOf(*p, core::kAutoGain) == 0.0f);
}

TEST_CASE("Compensacion: viaja con el estado del proyecto")
{
    auto a = makeProcessor();
    drag(*a, core::kAutoGain, {1.0f});
    juce::MemoryBlock state;
    a->getStateInformation(state);
    CHECK(state.toString().contains("auto_gain 1"));
    auto b = makeProcessor();
    b->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(valueOf(*b, core::kAutoGain) == 1.0f);
}
