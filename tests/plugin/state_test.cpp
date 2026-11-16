// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_support.h"

#include "esceq/core/state.h"
#include "../core/state_test_utils.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <string>
#include <thread>

using namespace esceq;
using namespace esceq::test;

namespace {

std::string fixture(const juce::String& name)
{
    const auto file = juce::File(ESCEQ_FIXTURES_DIR).getChildFile("state").getChildFile(name);
    REQUIRE(file.existsAsFile());
    juce::MemoryBlock data;
    REQUIRE(file.loadFileAsData(data));
    return std::string(static_cast<const char*>(data.getData()), data.getSize());
}

void load(PluginProcessor& p, const std::string& text)
{
    p.setStateInformation(text.data(), static_cast<int>(text.size()));
}

std::string save(PluginProcessor& p)
{
    juce::MemoryBlock data;
    p.getStateInformation(data);
    return std::string(static_cast<const char*>(data.getData()), data.getSize());
}

} // namespace

TEST_CASE("Estado del plugin: lo que se guarda es el formato de core, version actual")
{
    auto p = makeProcessor();
    const auto text = save(*p);
    CHECK(text.starts_with("esceQ-state " + std::to_string(core::kStateVersion) + "\n"));
    const auto state = core::deserializePluginState(text);
    REQUIRE(state.has_value());
    CHECK(state->parameters == core::defaultValues());
    CHECK(state->ui == core::UiSettings{});
}

TEST_CASE("Estado del plugin: ida y vuelta de parametros y preferencias de la interfaz")
{
    auto a = makeProcessor();
    drag(*a, core::bandParameter(2, core::BandParam::Enabled), {1.0f});
    drag(*a, core::bandParameter(2, core::BandParam::Frequency), {432.0f});
    drag(*a, core::channelParameter(2), {4.0f});
    drag(*a, core::dynamicParameter(2, core::DynParam::Threshold), {-41.0f});

    core::UiSettings ui;
    ui.analyzerMode = 2;
    ui.displayRangeDb = 30;
    ui.windowWidth = 1480;
    ui.windowHeight = 860;
    ui.presetName = "Vocal";
    a->setUiSettings(ui);

    auto b = makeProcessor();
    const int revision = b->stateRevision();
    load(*b, save(*a));
    CHECK(b->currentValues() == a->currentValues());
    CHECK(b->uiSettings() == ui);
    CHECK(b->stateRevision() != revision); // la interfaz abierta se entera
}

TEST_CASE("Estado del plugin: los estados de la version 1 siguen cargando")
{
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v1_bands.txt", "v1_dynamics.txt", "v1_channels.txt"})
    {
        INFO(name);
        const auto text = fixture(name);
        auto p = makeProcessor();
        // Preferencias previas: un estado de la version 1 no trae, y deben volver a su valor por defecto.
        core::UiSettings ui;
        ui.displayRangeDb = 6;
        p->setUiSettings(ui);

        load(*p, text);
        CHECK(p->currentValues() == *core::deserializeState(text));
        CHECK(p->uiSettings() == core::UiSettings{});
    }
}

TEST_CASE("Estado del plugin: el fixture de la version 2 carga con sus preferencias")
{
    auto p = makeProcessor();
    load(*p, fixture("v2_ui.txt"));
    CHECK(p->uiSettings().analyzerMode == 2);
    CHECK(p->uiSettings().displayRangeDb == 12);
    CHECK(p->uiSettings().windowWidth == 1480);
    CHECK(p->uiSettings().presetName == "Low-End Cleanup");
    CHECK(valueOf(*p, core::kOutputGain) == -2.0f);
    // Mientras kStateVersion sea 2, se vuelve a guardar identico (salvo los
    // parametros añadidos despues). Con la 3, ver filter_algorithm_test.cpp.
    if (core::kStateVersion == 2)
        CHECK(withoutParametersAddedAfter(save(*p), kParametersInV2Fixtures) == fixture("v2_ui.txt"));
}

TEST_CASE("Estado del plugin: datos que no son un estado no cambian nada")
{
    auto p = makeProcessor();
    drag(*p, core::kOutputGain, {5.0f});
    core::UiSettings ui;
    ui.displayRangeDb = 12;
    p->setUiSettings(ui);

    load(*p, "esto no es un estado");
    load(*p, std::string("\0\x01\x02", 3));
    p->setStateInformation(nullptr, 0);
    CHECK(valueOf(*p, core::kOutputGain) == 5.0f);
    CHECK(p->uiSettings().displayRangeDb == 12);
}

TEST_CASE("Estado del plugin: preferencias absurdas se sanean al guardarlas")
{
    auto p = makeProcessor();
    core::UiSettings ui;
    ui.analyzerMode = 99;
    ui.displayRangeDb = 7;
    ui.windowWidth = 5;
    ui.windowHeight = 700;
    p->setUiSettings(ui);
    CHECK(p->uiSettings() == core::UiSettings{});
}

// VST3 y CLAP exigen que el host cargue el estado desde el hilo de mensajes,
// pero AU no dice nada y un host puede saltárselo. Este test lo hace a
// propósito: un hilo "host" carga y guarda estados sin parar mientras el hilo
// de mensajes hace gestos, deshacer y rehacer, y un hilo de audio procesa.
// Con ThreadSanitizer no aparece ninguna carrera de datos; sin él, comprueba
// que nada se bloquea ni se rompe y que el historial queda bien después.
TEST_CASE("Estado del plugin: cargar estados desde otro hilo no se cruza con deshacer ni con el audio")
{
    auto p = makeProcessor();
    const auto gain = core::bandParameter(0, core::BandParam::Gain);
    drag(*p, gain, {3.0f});
    settle();
    const std::string first = save(*p);
    drag(*p, gain, {-6.0f});
    settle();
    const std::string second = save(*p);

    p->setPlayConfigDetails(2, 2, 48000.0, 256);
    p->prepareToPlay(48000.0, 256);

    std::atomic<bool> stop{false};
    std::thread audio([&] {
        juce::AudioBuffer<float> buffer(4, 256); // principal y sidechain
        juce::MidiBuffer midi;
        juce::Random random(1);
        while (!stop.load())
        {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample(ch, i, random.nextFloat() - 0.5f);
            p->processBlock(buffer, midi);
        }
    });
    std::thread host([&] {
        for (int i = 0; !stop.load(); ++i)
        {
            load(*p, (i % 2 == 0) ? first : second);
            (void) save(*p);
        }
    });

    const auto end = juce::Time::getMillisecondCounter() + 1000;
    for (int n = 0; juce::Time::getMillisecondCounter() < end; ++n)
    {
        drag(*p, core::bandParameter(1 + n % 5, core::BandParam::Gain), {static_cast<float>(n % 7)});
        pump(5);
        if (n % 3 == 0)
            p->history().undo();
        if (n % 5 == 0)
            p->history().redo();
        if (n % 13 == 0)
            settle(); // deja que el historial registre alguna acción
    }
    stop.store(true);
    host.join();
    audio.join();

    // Después, una carga normal deja el estado y el historial como siempre.
    load(*p, first);
    settle();
    CHECK(valueOf(*p, gain) == 3.0f);
    CHECK_FALSE(p->history().canUndo());
    CHECK_FALSE(p->history().canRedo());
    p->releaseResources();
}
