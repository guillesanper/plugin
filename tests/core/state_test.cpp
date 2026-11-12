// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/state.h"

#include "esceq/core/band_model.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

using namespace esceq::core;

namespace {

std::string readFixture(const std::string& name)
{
    std::ifstream file(std::string(ESCEQ_FIXTURES_DIR) + "/state/" + name, std::ios::binary);
    REQUIRE(file.is_open());
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

} // namespace

TEST_CASE("Estado: ida y vuelta conserva los valores")
{
    ParameterValues values = defaultValues();
    values[kOutputGain] = -7.3125f;
    values[kBypass] = 1.0f;

    const auto restored = deserializeState(serializeState(values));
    REQUIRE(restored.has_value());
    CHECK(*restored == values);
}

// Los fixtures son estados tal y como los escribió cada versión publicada.
// Nunca se modifican ni se borran: deben cargar siempre.
TEST_CASE("Estado: fixture v1 por defecto")
{
    const auto text = readFixture("v1_default.txt");

    const auto values = deserializeState(text);
    REQUIRE(values.has_value());
    CHECK(*values == defaultValues());

    // Mientras kStateVersion sea 1, las líneas que ya existían se siguen
    // escribiendo igual. (Puede haber líneas nuevas: parámetros añadidos.)
    if (kStateVersion == 1)
    {
        const std::string written = serializeState(defaultValues());
        std::istringstream lines(text);
        for (std::string line; std::getline(lines, line);)
        {
            INFO(line);
            CHECK(written.find(line + '\n') != std::string::npos);
        }
    }
}

TEST_CASE("Estado: fixture v1 con valores propios")
{
    const auto values = deserializeState(readFixture("v1_custom.txt"));
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == -6.5f);
    CHECK((*values)[kBypass] == 1.0f);
}

TEST_CASE("Estado: tolera finales de linea de Windows")
{
    const auto values = deserializeState("esceQ-state 1\r\noutput_gain 3\r\nbypass 1\r\n");
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == 3.0f);
    CHECK((*values)[kBypass] == 1.0f);
}

TEST_CASE("Estado: parametros ausentes toman el valor por defecto")
{
    const auto values = deserializeState("esceQ-state 1\nbypass 1\n");
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == parameters()[kOutputGain].defaultValue);
    CHECK((*values)[kBypass] == 1.0f);
}

TEST_CASE("Estado: un estado de una version futura carga lo que se conoce")
{
    const auto values = deserializeState("esceQ-state 99\nparametro_futuro 12\noutput_gain 2\n");
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == 2.0f);
}

TEST_CASE("Estado: valores fuera de rango o ilegibles no se propagan")
{
    const auto values = deserializeState("esceQ-state 1\noutput_gain 9999\nbypass basura\n");
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == parameters()[kOutputGain].max);
    CHECK((*values)[kBypass] == parameters()[kBypass].defaultValue);
}

TEST_CASE("Estado: se rechaza lo que no es un estado de esceQ")
{
    CHECK_FALSE(deserializeState("").has_value());
    CHECK_FALSE(deserializeState("otra cosa").has_value());
    CHECK_FALSE(deserializeState("esceQ-state").has_value());
    CHECK_FALSE(deserializeState("esceQ-state 0\n").has_value());
    CHECK_FALSE(deserializeState("esceQ-state x\noutput_gain 1\n").has_value());
    CHECK_FALSE(deserializeState(std::string("\0\x01\x02\xff", 4)).has_value());
}

TEST_CASE("Estado: fixture v1 con bandas")
{
    const auto values = deserializeState(readFixture("v1_bands.txt"));
    REQUIRE(values.has_value());
    CHECK((*values)[bandParameter(0, BandParam::Enabled)] == 1.0f);
    CHECK((*values)[bandParameter(0, BandParam::Type)] == 3.0f);
    CHECK((*values)[bandParameter(0, BandParam::Frequency)] == 80.0f);
    CHECK((*values)[bandParameter(0, BandParam::Slope)] == 3.0f);
    CHECK((*values)[bandParameter(4, BandParam::Enabled)] == 1.0f);
    CHECK((*values)[bandParameter(4, BandParam::Gain)] == -4.5f);
    CHECK((*values)[bandParameter(4, BandParam::Q)] == 2.5f);
    CHECK((*values)[bandParameter(23, BandParam::Enabled)] == 0.0f);
}

TEST_CASE("Estado: ida y vuelta con todas las bandas y valores aleatorios")
{
    ParameterValues values = defaultValues();
    std::uint32_t rng = 7u;
    for (std::size_t i = 0; i < kNumParameters; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        const float normalized = static_cast<float>(rng >> 8) / 16777216.0f;
        values[i] = fromNormalized(parameters()[i], normalized);
    }

    const auto restored = deserializeState(serializeState(values));
    REQUIRE(restored.has_value());
    CHECK(*restored == values);
}

TEST_CASE("Estado: fixture v1 con dinamica")
{
    const auto values = deserializeState(readFixture("v1_dynamics.txt"));
    REQUIRE(values.has_value());
    CHECK((*values)[bandParameter(1, BandParam::Frequency)] == 6500.0f);
    CHECK((*values)[dynamicParameter(1, DynParam::Enabled)] == 1.0f);
    CHECK((*values)[dynamicParameter(1, DynParam::Threshold)] == -30.0f);
    CHECK((*values)[dynamicParameter(1, DynParam::Range)] == -9.0f);
    CHECK((*values)[dynamicParameter(1, DynParam::Attack)] == 0.5f);
    CHECK((*values)[dynamicParameter(1, DynParam::Release)] == 60.0f);
    CHECK((*values)[dynamicParameter(1, DynParam::Knee)] == 3.0f);
    CHECK((*values)[dynamicParameter(1, DynParam::Source)] == 1.0f);
    CHECK((*values)[dynamicParameter(0, DynParam::Enabled)] == 0.0f);
}

TEST_CASE("Estado: un estado anterior a la dinamica la deja desactivada")
{
    // v1_bands.txt se escribió antes de que existieran los parámetros de dinámica.
    const auto values = deserializeState(readFixture("v1_bands.txt"));
    REQUIRE(values.has_value());
    for (int band = 0; band < kNumBands; ++band)
    {
        INFO("banda " << band + 1);
        CHECK((*values)[dynamicParameter(band, DynParam::Enabled)] == 0.0f);
        CHECK((*values)[dynamicParameter(band, DynParam::Threshold)] ==
              parameters()[dynamicParameter(band, DynParam::Threshold)].defaultValue);
    }
}

TEST_CASE("Estado: un estado anterior al canal por banda carga en Stereo")
{
    // Estos fixtures se escribieron antes de que existiera bNN_chan: deben
    // seguir sonando igual, es decir, con todas las bandas en Stereo.
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v1_bands.txt", "v1_dynamics.txt"})
    {
        const auto values = deserializeState(readFixture(name));
        REQUIRE(values.has_value());
        for (int band = 0; band < kNumBands; ++band)
        {
            INFO(name << ", banda " << band + 1);
            CHECK((*values)[channelParameter(band)] == 0.0f);
            CHECK(bandSettings(*values, band).channel == esceq::dsp::ChannelMode::Stereo);
        }
    }
}

TEST_CASE("Estado: fixture v1 con canales")
{
    const auto values = deserializeState(readFixture("v1_channels.txt"));
    REQUIRE(values.has_value());
    CHECK(bandSettings(*values, 0).channel == esceq::dsp::ChannelMode::Mid);
    CHECK(bandSettings(*values, 1).channel == esceq::dsp::ChannelMode::Side);
    CHECK(bandSettings(*values, 2).channel == esceq::dsp::ChannelMode::Left);
    CHECK(bandSettings(*values, 3).channel == esceq::dsp::ChannelMode::Right);
    CHECK(bandSettings(*values, 4).channel == esceq::dsp::ChannelMode::Stereo);
    CHECK((*values)[bandParameter(1, BandParam::Frequency)] == 8000.0f);
}

// ---------------------------------------------------------------------------
// Version 2: preferencias de la interfaz

TEST_CASE("Estado v2: ida y vuelta con las preferencias de la interfaz")
{
    PluginState state;
    state.parameters[kOutputGain] = -3.5f;
    state.parameters[channelParameter(4)] = 2.0f;
    state.ui.analyzerMode = 2;
    state.ui.displayRangeDb = 30;
    state.ui.windowWidth = 1480;
    state.ui.windowHeight = 860;
    state.ui.presetName = "Vocal de Ana";

    const auto text = serializePluginState(state);
    CHECK(text.starts_with("esceQ-state 2\n"));
    const auto restored = deserializePluginState(text);
    REQUIRE(restored.has_value());
    CHECK(*restored == state);

    // Las preferencias por defecto tambien dan la vuelta.
    const auto defaults = deserializePluginState(serializePluginState(PluginState{}));
    REQUIRE(defaults.has_value());
    CHECK(*defaults == PluginState{});
}

TEST_CASE("Estado v2: fixture con preferencias de la interfaz")
{
    const auto state = deserializePluginState(readFixture("v2_ui.txt"));
    REQUIRE(state.has_value());
    CHECK(state->parameters[kOutputGain] == -2.0f);
    CHECK(state->parameters[bandParameter(0, BandParam::Enabled)] == 1.0f);
    CHECK(state->parameters[bandParameter(0, BandParam::Frequency)] == 120.0f);
    CHECK(state->parameters[channelParameter(0)] == 4.0f);
    CHECK(state->ui.analyzerMode == 2);
    CHECK(state->ui.displayRangeDb == 12);
    CHECK(state->ui.windowWidth == 1480);
    CHECK(state->ui.windowHeight == 860);
    CHECK(state->ui.presetName == "Low-End Cleanup");

    // Mientras kStateVersion sea 2, lo que se escribe a partir de el es identico.
    if (kStateVersion == 2)
        CHECK(serializePluginState(*state) == readFixture("v2_ui.txt"));
}

TEST_CASE("Estado v2: migracion desde la version 1")
{
    // Un estado de la version 1 no tiene preferencias: toman el valor por
    // defecto, y los parametros se conservan.
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v1_bands.txt", "v1_dynamics.txt", "v1_channels.txt"})
    {
        INFO(name);
        const auto text = readFixture(name);
        const auto state = deserializePluginState(text);
        REQUIRE(state.has_value());
        CHECK(state->ui == UiSettings{});
        CHECK(state->parameters == *deserializeState(text));
    }

    // Lineas ui.* dentro de un estado que dice ser de la version 1 no las
    // escribio esceQ (la version 1 no las tenia): la migracion las descarta.
    const auto v1 = deserializePluginState("esceQ-state 1\noutput_gain 4\nui.range 6\nui.analyzer 0\nui.preset X\n");
    REQUIRE(v1.has_value());
    CHECK(v1->parameters[kOutputGain] == 4.0f);
    CHECK(v1->ui == UiSettings{});

    // Las mismas lineas en la version 2 si cuentan.
    const auto v2 = deserializePluginState("esceQ-state 2\noutput_gain 4\nui.range 6\nui.analyzer 0\nui.preset X\n");
    REQUIRE(v2.has_value());
    CHECK(v2->ui.displayRangeDb == 6);
    CHECK(v2->ui.analyzerMode == 0);
    CHECK(v2->ui.presetName == "X");
}

TEST_CASE("Estado v2: una version anterior de esceQ lee los parametros e ignora lo demas")
{
    // Lo que hace el lector de la version 1 con un estado de la 2: las claves
    // que no conoce (ui.*) se ignoran. Aqui se comprueba la otra mitad: que
    // esas claves no chocan con ningun parametro.
    for (const auto& spec : parameters())
        CHECK_FALSE(spec.id.starts_with("ui."));

    PluginState state;
    state.parameters[kOutputGain] = 5.0f;
    state.ui.presetName = "output_gain 9"; // un nombre con mala idea
    const auto restored = deserializePluginState(serializePluginState(state));
    REQUIRE(restored.has_value());
    CHECK(restored->parameters[kOutputGain] == 5.0f);
    CHECK(restored->ui.presetName == "output_gain 9");
}

TEST_CASE("Estado v2: preferencias absurdas toman el valor por defecto")
{
    const auto state = deserializePluginState("esceQ-state 2\n"
                                              "ui.analyzer 7\n"
                                              "ui.range 17\n"
                                              "ui.width 1200\n"
                                              "ui.height -5\n");
    REQUIRE(state.has_value());
    CHECK(state->ui == UiSettings{}); // el tamano es un par: si falla una medida, se descartan las dos

    const auto garbage = deserializePluginState("esceQ-state 2\nui.analyzer abc\nui.range\nui.width 99999999\nui.height 700\n");
    REQUIRE(garbage.has_value());
    CHECK(garbage->ui == UiSettings{});

    // Nombre de preset: sin caracteres de control y con longitud maxima.
    UiSettings ui;
    ui.presetName = std::string("  A\tB\x01") + std::string(200, 'x');
    const auto clean = sanitize(ui);
    CHECK(clean.presetName.starts_with("ABx"));
    CHECK(clean.presetName.size() == static_cast<std::size_t>(kMaxPresetNameLength));

    // Un nombre con saltos de linea no puede colar lineas en el estado.
    PluginState evil;
    evil.ui.presetName = "x\noutput_gain 24\nbypass 1";
    const auto restored = deserializePluginState(serializePluginState(evil));
    REQUIRE(restored.has_value());
    CHECK(restored->parameters == defaultValues());
}
