// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Saturación vista desde el host (PLAN.md 5.8, ADR 0026): con Off, el sonido,
// la latencia y la cola de antes; la latencia que se comunica y la que se
// mide en los cuatro modos; los proyectos y presets anteriores en Off; y la
// automatización de los tres parámetros, con el aviso al host en fase lineal.
#include "test_support.h"

#include "esceq/core/band_model.h"
#include "esceq/dsp/linear_phase.h"
#include "esceq/dsp/phase_equalizer.h"
#include "esceq/dsp/saturation.h"
#include "esceq/dsp/spectral_dynamics.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq;
using namespace esceq::test;

namespace
{

constexpr int kBlock = 256;
constexpr double kRate = 48000.0;
constexpr float kModes[] = {0.0f, 1.0f, 2.0f, 3.0f};

void prepare(PluginProcessor& p, double rate = kRate)
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

std::vector<float> sine(int length, double hz, double amplitude, double rate = kRate)
{
    std::vector<float> x(static_cast<std::size_t>(length));
    for (std::size_t n = 0; n < x.size(); ++n)
        x[n] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(n) / rate));
    return x;
}

std::vector<float> noise(int length, std::uint32_t seed)
{
    std::vector<float> x(static_cast<std::size_t>(length));
    for (auto& v : x)
    {
        seed = seed * 1664525u + 1013904223u;
        v = 0.4f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
    return x;
}

/// Una mezcla pequeña: una campana y una banda dinámica.
void someBands(PluginProcessor& p)
{
    using core::BandParam;
    using core::DynParam;
    drag(p, core::bandParameter(0, BandParam::Frequency), {1000.0f});
    drag(p, core::bandParameter(0, BandParam::Gain), {3.0f});
    drag(p, core::bandParameter(0, BandParam::Enabled), {1.0f});
    drag(p, core::bandParameter(1, BandParam::Frequency), {5000.0f});
    drag(p, core::bandParameter(1, BandParam::Enabled), {1.0f});
    drag(p, core::dynamicParameter(1, DynParam::Enabled), {1.0f});
    drag(p, core::dynamicParameter(1, DynParam::Threshold), {-30.0f});
}

void saturate(PluginProcessor& p, float mode, float driveDb = 12.0f, float mix = 100.0f)
{
    drag(p, core::kSaturationDrive, {driveDb});
    drag(p, core::kSaturationMix, {mix});
    drag(p, core::kSaturationMode, {mode});
}

/// La latencia del modo, más la de la saturación si se pide.
int expectedLatency(float mode, double rate, bool saturating)
{
    const auto phase = core::phaseModeFromChoice(mode);
    return dsp::linearPhaseSize(phase, rate).latency() +
           (saturating ? dsp::PhaseEqualizer::saturationLatency(phase, rate) : 0);
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
        const double phase = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / kRate;
        re += w * x[i] * std::cos(phase);
        im -= w * x[i] * std::sin(phase);
        sum += w;
    }
    return 2.0 * std::hypot(re, im) / sum;
}

float maxStep(const std::vector<float>& x)
{
    float worst = 0.0f;
    for (std::size_t i = 1; i < x.size(); ++i)
        worst = std::max(worst, std::abs(x[i] - x[i - 1]));
    return worst;
}

std::string stateFixture(const char* name)
{
    const auto file = juce::File(ESCEQ_FIXTURES_DIR).getChildFile("state").getChildFile(name);
    REQUIRE(file.existsAsFile());
    juce::MemoryBlock data;
    REQUIRE(file.loadFileAsData(data));
    return std::string(static_cast<const char*>(data.getData()), data.getSize());
}

} // namespace

TEST_CASE("Saturacion: en Off, la latencia, la cola y el sonido de antes en los cuatro modos")
{
    // Criterio de PLAN.md 5.8: con sat_mode en Off, bit a bit lo de antes y la
    // latencia de antes, aunque el drive y la mezcla no estén por defecto. Una
    // instancia que la ha encendido y apagado suena igual que otra que no la ha
    // tocado nunca; la comparación con el código anterior a la saturación está
    // en docs/dsp/saturacion.md.
    const auto input = noise(30000, 11u);
    for (const float mode : kModes)
    {
        INFO("modo " << mode);
        std::vector<float> outputs[2];
        for (int touched = 0; touched < 2; ++touched)
        {
            auto p = makeProcessor();
            p->setNonRealtime(true);
            someBands(*p);
            drag(*p, core::kPhaseMode, {mode});
            if (touched == 1)
                saturate(*p, 0.0f, 20.0f, 35.0f);
            prepare(*p);
            const auto size = dsp::linearPhaseSize(core::phaseModeFromChoice(mode), kRate);
            CHECK(p->getLatencySamples() == size.latency());
            CHECK(p->getTailLengthSeconds() == static_cast<double>(size.latency() + size.firLength / 2) / kRate);
            if (touched == 1)
            {
                drag(*p, core::kSaturationMode, {2.0f});
                (void)render(*p, input);
                CHECK(p->engineLatencySamples() == expectedLatency(mode, kRate, true));
                drag(*p, core::kSaturationMode, {0.0f});
                (void)render(*p, input);
                CHECK(p->engineLatencySamples() == size.latency());
                prepare(*p);
            }
            CHECK(p->getLatencySamples() == size.latency());
            outputs[touched] = render(*p, input);
        }
        CHECK(outputs[0] == outputs[1]);
    }
}

TEST_CASE("Saturacion: la latencia comunicada coincide con la medida en los cuatro modos")
{
    // Criterio de PLAN.md 5.8, a 44,1, 48, 96 y 192 kHz. Un impulso pequeño
    // (la curva, casi lineal) sale entero en la latencia que conoce el host.
    // En IIR no hay latencia: el pico sale en las primeras muestras, por el
    // retardo de grupo de los filtros IIR (unas 6 muestras en graves).
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
        for (const float mode : kModes)
        {
            INFO("fs " << rate << ", modo " << mode);
            auto p = makeProcessor();
            saturate(*p, 1.0f, 6.0f);
            drag(*p, core::kPhaseMode, {mode});
            prepare(*p, rate);
            const int expected = expectedLatency(mode, rate, true);
            CHECK(p->getLatencySamples() == expected);
            CHECK(p->engineLatencySamples() == expected);
            const auto size = dsp::linearPhaseSize(core::phaseModeFromChoice(mode), rate);
            CHECK(p->getTailLengthSeconds() == static_cast<double>(expected + size.firLength / 2) / rate);

            std::vector<float> input(static_cast<std::size_t>(expected + 4096), 0.0f);
            input[0] = 0.01f;
            const auto out = render(*p, input);
            std::size_t peak = 0;
            for (std::size_t i = 0; i < out.size(); ++i)
                if (std::abs(out[i]) > std::abs(out[peak]))
                    peak = i;
            if (mode == 0.0f)
            {
                CHECK(expected == 0);
                CHECK(peak < 8);
            }
            else
            {
                CHECK(peak == static_cast<std::size_t>(expected));
                CHECK_THAT(out[peak], WithinAbs(0.01, 0.002));
            }
        }

    // Con la dinámica espectral, las tres latencias se suman.
    auto p = makeProcessor();
    saturate(*p, 2.0f);
    drag(*p, core::kPhaseMode, {1.0f});
    drag(*p, core::spectralParameter(0), {1.0f});
    prepare(*p);
    CHECK(p->getLatencySamples() == expectedLatency(1.0f, kRate, true) + dsp::SpectralDynamics::latencySamples(kRate));
}

TEST_CASE("Saturacion: los proyectos y presets anteriores la dejan en Off")
{
    // El estado sigue en la versión 3: lo que no lleva sat_mode es Off, con el
    // drive y la mezcla por defecto, y suena y se retrasa como antes.
    for (const char* name : {"v1_custom.txt", "v3_classic.txt", "release_0.1_full.txt"})
    {
        INFO(name);
        auto p = makeProcessor();
        saturate(*p, 2.0f, 20.0f, 30.0f);
        const std::string text = stateFixture(name);
        p->setStateInformation(text.data(), static_cast<int>(text.size()));
        CHECK(valueOf(*p, core::kSaturationMode) == 0.0f);
        CHECK(valueOf(*p, core::kSaturationDrive) == 6.0f);
        CHECK(valueOf(*p, core::kSaturationMix) == 100.0f);
        prepare(*p);
        CHECK(p->getLatencySamples() == expectedLatency(valueOf(*p, core::kPhaseMode), kRate, false));
    }

    auto p = makeProcessor();
    saturate(*p, 1.0f, 18.0f, 50.0f);
    REQUIRE(p->presets().load({"Vocal", true}));
    CHECK(valueOf(*p, core::kSaturationMode) == 0.0f);
    CHECK(valueOf(*p, core::kSaturationDrive) == 6.0f);
    CHECK(valueOf(*p, core::kSaturationMix) == 100.0f);
}

TEST_CASE("Saturacion: viaja con el estado del proyecto")
{
    auto a = makeProcessor();
    saturate(*a, 2.0f, 15.0f, 40.0f);
    drag(*a, core::kPhaseMode, {2.0f});
    juce::MemoryBlock state;
    a->getStateInformation(state);
    CHECK(state.toString().startsWith("esceQ-state 3\n"));
    CHECK(state.toString().contains("sat_mode 2"));

    auto b = makeProcessor();
    b->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(valueOf(*b, core::kSaturationMode) == 2.0f);
    CHECK(valueOf(*b, core::kSaturationDrive) == 15.0f);
    CHECK(valueOf(*b, core::kSaturationMix) == 40.0f);
    prepare(*b);
    CHECK(b->getLatencySamples() == expectedLatency(2.0f, kRate, true));
}

TEST_CASE("Saturacion: encenderla en fase lineal avisa al host; en IIR, no cambia nada")
{
    // Determinista, como el cambio de modo: el trabajo del hilo de control lo
    // hace el test con flushControlThread().
    const auto input = sine(kBlock * 4, 440.0, 0.4);
    for (const float mode : {0.0f, 1.0f})
    {
        INFO("modo " << mode);
        auto p = makeProcessor();
        drag(*p, core::kPhaseMode, {mode});
        prepare(*p);
        const int base = expectedLatency(mode, kRate, false);
        REQUIRE(p->getLatencySamples() == base);
        for (const auto& [on, expected] : {std::pair{2.0f, expectedLatency(mode, kRate, true)}, std::pair{0.0f, base}})
        {
            const int before = p->getLatencySamples();
            drag(*p, core::kSaturationMode, {on});
            (void)render(*p, input);
            // En fase lineal, el cambio llega al audio tras bajar a silencio en 10 ms.
            for (int i = 0; i < 40 && p->engineLatencySamples() != expected; ++i)
                (void)render(*p, input);
            REQUIRE(p->engineLatencySamples() == expected);
            CHECK(p->getLatencySamples() == before);
            p->flushControlThread();
            CHECK(pumpUntil([&] { return p->getLatencySamples() == expected; }));
        }
        CHECK(p->getLatencySamples() == base);
    }
}

TEST_CASE("Saturacion: la automatizacion llega al audio sin chasquidos")
{
    // Los tres parámetros automatizados en mitad del render, en IIR y en fase
    // lineal: el modo (Off, Soft, Warm y Off), el drive y la mezcla. Sin
    // saltos (un seno de 440 Hz a 0,3 cambia como mucho 0,017 por muestra), y
    // cada uno se oye: con menos drive baja el tercer armónico, con Warm
    // aparece el segundo, que con Soft no está, y con la mezcla a 0 desaparece.
    const auto tone = sine(48000 * 3, 440.0, 0.3);
    for (const float mode : {0.0f, 1.0f})
    {
        INFO("modo " << mode);
        auto p = makeProcessor();
        drag(*p, core::kPhaseMode, {mode});
        prepare(*p);
        const auto out = render(*p, tone,
                                [&](int block)
                                {
                                    if (block == 20)
                                        saturate(*p, 1.0f, 18.0f);
                                    if (block == 130)
                                        drag(*p, core::kSaturationDrive, {3.0f});
                                    if (block == 240)
                                        drag(*p, core::kSaturationMode, {2.0f});
                                    if (block == 350)
                                        drag(*p, core::kSaturationMix, {0.0f});
                                    if (block == 460)
                                        drag(*p, core::kSaturationMode, {0.0f});
                                });
        CHECK(maxStep(out) < 0.03f);

        // El armónico `n`, relativo a la fundamental, desde 40 bloques después de `fromBlock`.
        auto harmonic = [&](int n, int fromBlock)
        {
            const auto from = static_cast<std::size_t>((fromBlock + 40) * kBlock);
            const auto to = from + 16384;
            return 20.0 * std::log10(toneAmplitude(out, 440.0 * n, from, to) / toneAmplitude(out, 440.0, from, to));
        };
        INFO("tercero: 18 dB " << harmonic(3, 20) << ", 3 dB " << harmonic(3, 130));
        INFO("segundo: Soft " << harmonic(2, 20) << ", Warm " << harmonic(2, 240) << ", mezcla 0 " << harmonic(2, 350));
        CHECK(harmonic(3, 20) > -40.0);
        CHECK(harmonic(3, 130) < harmonic(3, 20) - 10.0);
        CHECK(harmonic(2, 20) < -100.0);
        CHECK(harmonic(2, 240) > -50.0);
        CHECK(harmonic(2, 350) < -100.0);
        CHECK(harmonic(3, 350) < -100.0);
        CHECK(p->engineLatencySamples() == expectedLatency(mode, kRate, false));
    }
}

TEST_CASE("Saturacion: con bypass en fase lineal la salida es la entrada retrasada")
{
    auto p = makeProcessor();
    someBands(*p);
    saturate(*p, 2.0f, 24.0f);
    drag(*p, core::kPhaseMode, {1.0f});
    drag(*p, core::kBypass, {1.0f});
    prepare(*p);
    const int latency = p->getLatencySamples();
    REQUIRE(latency == expectedLatency(1.0f, kRate, true));
    const auto input = noise(latency + 4000, 13u);
    const auto out = render(*p, input);
    bool exact = true;
    for (std::size_t i = static_cast<std::size_t>(latency); i < input.size(); ++i)
        exact = exact && out[i] == input[i - static_cast<std::size_t>(latency)];
    CHECK(exact);
}
