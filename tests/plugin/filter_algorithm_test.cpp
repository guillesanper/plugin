// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Algoritmo de filtro de la instancia visto desde el host: con cuál arranca,
// cuál suena, cómo viaja con el estado, que los presets no lo cambian y que
// cambiarlo se deshace (ADR 0013 y 0014).
#include "test_support.h"
#include "../core/state_test_utils.h"

#include "esceq/core/band_model.h"
#include "esceq/core/state.h"
#include "esceq/dsp/decibels.h"
#include "esceq/dsp/equalizer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq;
using namespace esceq::test;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr auto kClassic = dsp::FilterAlgorithm::Classic;
constexpr auto kMatched = dsp::FilterAlgorithm::Matched;

std::string fixture(const juce::String& name)
{
    const auto file = juce::File(ESCEQ_FIXTURES_DIR).getChildFile("state").getChildFile(name);
    REQUIRE(file.existsAsFile());
    return file.loadFileAsString().toStdString();
}

void load(PluginProcessor& p, const std::string& text)
{
    p.setStateInformation(text.data(), static_cast<int>(text.size()));
}

std::string save(PluginProcessor& p)
{
    juce::MemoryBlock data;
    p.getStateInformation(data);
    return data.toString().toStdString();
}

/// Campana de +12 dB a 18 kHz: ahí los dos algoritmos se separan varios dB.
void highBell(PluginProcessor& p)
{
    using namespace core;
    drag(p, bandParameter(0, BandParam::Frequency), {18000.0f});
    drag(p, bandParameter(0, BandParam::Gain), {12.0f});
    drag(p, bandParameter(0, BandParam::Q), {2.0f});
    drag(p, bandParameter(0, BandParam::Enabled), {1.0f});
}

void prepare(PluginProcessor& p)
{
    p.setRateAndBufferSizeDetails(kRate, kBlock);
    p.prepareToPlay(kRate, kBlock);
}

/// Pasa `blocks` bloques (los dos canales iguales) y devuelve el canal izquierdo.
/// Si `impulse`, la entrada es un impulso; si no, silencio.
std::vector<float> render(PluginProcessor& p, int blocks, bool impulse)
{
    juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), kBlock);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        if (impulse && b == 0)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, 0, 1.0f);
        p.processBlock(buffer, midi);
        for (int i = 0; i < kBlock; ++i)
            out.push_back(buffer.getSample(0, i));
    }
    return out;
}

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

/// Magnitud de la banda 0 tal y como la diseña (y la dibuja) `algorithm`.
double designedDb(const PluginProcessor& p, dsp::FilterAlgorithm algorithm, double hz)
{
    const auto band = core::bandSettings(p.currentValues(), 0, algorithm);
    return dsp::magnitudeDb(dsp::designBand(band.filter, kRate), hz, kRate);
}

/// Cuenta los avisos al host de que el estado (no un parámetro) ha cambiado.
struct StateChangeCounter final : juce::AudioProcessorListener
{
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override
    {
        if (details.nonParameterStateChanged)
            ++count;
    }
    int count = 0;
};

} // namespace

TEST_CASE("Algoritmo: una instancia nueva usa Matched y lo guarda")
{
    auto p = makeProcessor();
    CHECK(p->filterAlgorithm() == kMatched);
    const auto state = core::deserializePluginState(save(*p));
    REQUIRE(state.has_value());
    CHECK(state->filterAlgorithm == kMatched);
}

TEST_CASE("Algoritmo: los estados anteriores a la version 3 cargan en Classic")
{
    for (const char* name : {"v1_default.txt", "v1_bands.txt", "v2_ui.txt", "release_0.1_full.txt"})
    {
        INFO(name);
        auto p = makeProcessor();
        load(*p, fixture(name));
        CHECK(p->filterAlgorithm() == kClassic);
        // Y se vuelve a guardar en Classic: el proyecto sigue sonando igual.
        CHECK(core::deserializePluginState(save(*p))->filterAlgorithm == kClassic);
    }
}

TEST_CASE("Algoritmo: los fixtures de la version 3 cargan con su algoritmo y se guardan identicos")
{
    for (const auto& [name, algorithm] : {std::pair{"v3_matched.txt", kMatched}, std::pair{"v3_classic.txt", kClassic}})
    {
        INFO(name);
        auto p = makeProcessor();
        // Partiendo del otro algoritmo: cargar el estado lo cambia.
        p->setFilterAlgorithm(algorithm == kMatched ? kClassic : kMatched);
        load(*p, fixture(name));
        CHECK(p->filterAlgorithm() == algorithm);
        if (core::kStateVersion == 3)
            CHECK(withoutParametersAddedAfter(save(*p), kParametersInV3Fixtures) == fixture(name));
    }
}

TEST_CASE("Algoritmo: el procesador suena con el algoritmo de la instancia, como la curva")
{
    for (const auto algorithm : {kMatched, kClassic})
    {
        INFO(static_cast<int>(algorithm));
        auto p = makeProcessor();
        highBell(*p);
        p->setFilterAlgorithm(algorithm);
        prepare(*p);
        const auto out = render(*p, 32, true);
        for (const double hz : {1000.0, 12000.0, 18000.0, 21000.0})
            CHECK_THAT(magnitudeDb(out, hz), WithinAbs(designedDb(*p, algorithm, hz), 0.02));
    }

    // La prueba distingue de verdad un algoritmo del otro.
    auto p = makeProcessor();
    highBell(*p);
    CHECK(std::abs(designedDb(*p, kMatched, 21000.0) - designedDb(*p, kClassic, 21000.0)) > 3.0);
}

TEST_CASE("Algoritmo: cambiarlo con el audio en marcha se aplica tras el fundido")
{
    auto p = makeProcessor();
    highBell(*p);
    prepare(*p);
    render(*p, 4, false);
    p->changeFilterAlgorithm(kClassic);
    render(*p, 8, false); // el fundido de un cambio estructural dura 10 ms (480 muestras)
    const auto out = render(*p, 32, true);
    CHECK_THAT(magnitudeDb(out, 21000.0), WithinAbs(designedDb(*p, kClassic, 21000.0), 0.02));
}

TEST_CASE("Algoritmo: un proyecto de la 0.1 suena en el procesador como en la 0.1")
{
    // El contrato "un proyecto guardado con la 0.1 suena igual" compara
    // dsp::Equalizer con las bandas de la 0.1 (Classic) contra su audio de
    // referencia. Aquí, que el procesador que carga ese proyecto suena
    // exactamente como ese ecualizador.
    const auto text = fixture("release_0.1_full.txt");
    const auto state = core::deserializePluginState(text);
    REQUIRE(state.has_value());

    auto p = makeProcessor();
    load(*p, text);
    prepare(*p);

    dsp::Equalizer reference;
    for (int band = 0; band < core::kNumBands; ++band)
        reference.setBand(band, core::bandSettings(state->parameters, band));
    reference.prepare(kRate);
    const auto outputGain = static_cast<float>(dsp::dbToGain(state->parameters[core::kOutputGain]));

    // Sin sidechain conectado (lo normal al abrir el proyecto): el
    // ecualizador de referencia tampoco lo tiene.
    REQUIRE_FALSE(p->sidechainConnected());
    juce::AudioBuffer<float> buffer(std::max(p->getTotalNumInputChannels(), p->getTotalNumOutputChannels()), kBlock);
    juce::MidiBuffer midi;
    std::uint32_t seed = 7u;
    double worst = 0.0;
    for (int b = 0; b < 64; ++b)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < kBlock; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                buffer.setSample(ch, i, 0.3f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f));
            }
        juce::AudioBuffer<float> expected;
        expected.makeCopyOf(buffer);
        float* channels[2] = {expected.getWritePointer(0), expected.getWritePointer(1)};
        reference.process(channels, 2, kBlock, nullptr, 0);

        p->processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                worst = std::max(worst, static_cast<double>(std::abs(buffer.getSample(ch, i) -
                                                                     outputGain * expected.getSample(ch, i))));
    }
    INFO("mayor diferencia: " << worst);
    CHECK(worst < 1.0e-6);
}

TEST_CASE("Algoritmo: cambiarlo es una accion que se deshace y avisa al host")
{
    auto p = makeProcessor();
    StateChangeCounter host;
    p->addListener(&host);
    settle();

    p->changeFilterAlgorithm(kClassic);
    CHECK(p->filterAlgorithm() == kClassic);
    CHECK(host.count == 1);
    CHECK(p->history().canUndo());

    p->history().undo();
    CHECK(p->filterAlgorithm() == kMatched);
    CHECK(host.count == 2); // deshacer también cambia el proyecto
    p->history().redo();
    CHECK(p->filterAlgorithm() == kClassic);

    // Elegir el que ya suena no es una acción.
    p->changeFilterAlgorithm(kClassic);
    p->history().undo();
    CHECK(p->filterAlgorithm() == kMatched);
    CHECK_FALSE(p->history().canUndo());
    p->removeListener(&host);
}

TEST_CASE("Algoritmo: un gesto que espera su registro es una accion aparte")
{
    const auto gain = core::bandParameter(0, core::BandParam::Gain);
    auto p = makeProcessor();
    settle();
    drag(*p, gain, {3.0f, 6.0f});
    p->changeFilterAlgorithm(kClassic); // antes de que el arrastre se registre

    p->history().undo();
    CHECK(p->filterAlgorithm() == kMatched);
    CHECK(valueOf(*p, gain) == 6.0f);
    p->history().undo();
    CHECK(valueOf(*p, gain) == 0.0f);
    CHECK_FALSE(p->history().canUndo());
}

TEST_CASE("Algoritmo: cargar un estado no avisa al host ni se puede deshacer")
{
    auto p = makeProcessor();
    StateChangeCounter host;
    p->addListener(&host);
    load(*p, fixture("v2_ui.txt"));
    CHECK(p->filterAlgorithm() == kClassic);
    CHECK(host.count == 0);
    settle();
    CHECK_FALSE(p->history().canUndo());
    p->removeListener(&host);
}

TEST_CASE("Algoritmo: los presets no lo cambian")
{
    PresetManager::userDirectory().deleteRecursively();
    for (const auto algorithm : {kClassic, kMatched})
    {
        INFO(static_cast<int>(algorithm));
        auto p = makeProcessor();
        p->changeFilterAlgorithm(algorithm);

        REQUIRE(p->presets().load({"Vocal", true}));
        CHECK(p->filterAlgorithm() == algorithm);
        CHECK_FALSE(p->presets().isModified());

        p->presets().loadDefault();
        CHECK(p->filterAlgorithm() == algorithm);

        // Un preset de usuario no lo guarda, ni al cargarlo lo cambia.
        REQUIRE(p->presets().saveUser("Con algoritmo"));
        const auto file = PresetManager::userDirectory().getChildFile("Con algoritmo.esceq");
        REQUIRE(file.existsAsFile());
        CHECK_FALSE(file.loadFileAsString().contains("filter_algorithm"));
        p->setFilterAlgorithm(algorithm == kClassic ? kMatched : kClassic);
        REQUIRE(p->presets().load({"Con algoritmo", false}));
        CHECK(p->filterAlgorithm() != algorithm);
        // Y cambiarlo no marca el preset como modificado.
        CHECK_FALSE(p->presets().isModified());
        PresetManager::userDirectory().deleteRecursively();
    }
}
