// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_support.h"

#include "esceq/core/state.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

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
    // Y se vuelve a guardar identico.
    CHECK(save(*p) == fixture("v2_ui.txt"));
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
