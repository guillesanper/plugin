// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/state.h"
#include "state_test_utils.h"

#include "esceq/core/band_model.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <limits>
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
    CHECK(text.starts_with("esceQ-state " + std::to_string(kStateVersion) + "\n"));
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

    // Mientras kStateVersion sea 2, lo que se escribe a partir de el es
    // identico, salvo las lineas de los parametros que se han añadido despues
    // (el fixture tiene los 338 de la fase 4).
    if (kStateVersion == 2)
    {
        const auto written = serializePluginState(*state);
        CHECK(esceq::test::withoutParametersAddedAfter(written, esceq::test::kParametersInV2Fixtures) ==
              readFixture("v2_ui.txt"));
    }
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

TEST_CASE("Estado: preferencias de la escala de notas")
{
    // Por defecto no se escriben: un estado sin tocarlas es identico al de antes.
    const auto defaults = serializePluginState(PluginState{});
    CHECK(defaults.find("ui.notes") == std::string::npos);
    CHECK(defaults.find("ui.a4") == std::string::npos);

    PluginState state;
    state.ui.pianoKeyboard = true;
    state.ui.a4Hz = 432.5;
    const auto text = serializePluginState(state);
    CHECK(text.find("\nui.notes 1\n") != std::string::npos);
    CHECK(text.find("\nui.a4 432.5\n") != std::string::npos);
    const auto restored = deserializePluginState(text);
    REQUIRE(restored.has_value());
    CHECK(*restored == state);

    // Cada una por separado.
    state.ui.a4Hz = kDefaultA4Hz;
    CHECK(serializePluginState(state).find("ui.a4") == std::string::npos);
    state.ui.pianoKeyboard = false;
    state.ui.a4Hz = 415.0;
    const auto onlyTuning = serializePluginState(state);
    CHECK(onlyTuning.find("ui.notes") == std::string::npos);
    CHECK(deserializePluginState(onlyTuning)->ui.a4Hz == 415.0);

    // Los estados anteriores (que no las tienen) cargan con la franja oculta y A4 = 440 Hz.
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v2_ui.txt", "v3_classic.txt", "v3_matched.txt"})
    {
        INFO(name);
        const auto old = deserializePluginState(readFixture(name));
        REQUIRE(old.has_value());
        CHECK_FALSE(old->ui.pianoKeyboard);
        CHECK(old->ui.a4Hz == kDefaultA4Hz);
    }

    // Son preferencias de la version 2 en adelante, sin subir de version (ADR 0009).
    const auto v2 = deserializePluginState("esceQ-state 2\nui.notes 1\nui.a4 442\n");
    REQUIRE(v2.has_value());
    CHECK(v2->ui.pianoKeyboard);
    CHECK(v2->ui.a4Hz == 442.0);
}

TEST_CASE("Estado: el cajon de la etapa de salida, abierto salvo que se pliegue")
{
    // Abierto por defecto, y entonces no se escribe: el estado es el de antes del cajon.
    CHECK(UiSettings{}.outputStageOpen);
    CHECK(serializePluginState(PluginState{}).find("ui.output_stage") == std::string::npos);

    PluginState state;
    state.ui.outputStageOpen = false;
    const auto text = serializePluginState(state);
    CHECK(text.find("\nui.output_stage 0\n") != std::string::npos);
    const auto restored = deserializePluginState(text);
    REQUIRE(restored.has_value());
    CHECK(*restored == state);

    // Los estados anteriores lo abren; lo que no se entiende, tambien.
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v2_ui.txt", "v3_classic.txt", "v3_matched.txt"})
    {
        INFO(name);
        const auto old = deserializePluginState(readFixture(name));
        REQUIRE(old.has_value());
        CHECK(old->ui.outputStageOpen);
    }
    for (const char* bad : {"esceQ-state 3\nui.output_stage 2\n", "esceQ-state 3\nui.output_stage -1\n",
                            "esceQ-state 3\nui.output_stage abc\n", "esceQ-state 3\nui.output_stage\n"})
    {
        INFO(bad);
        const auto parsed = deserializePluginState(bad);
        REQUIRE(parsed.has_value());
        CHECK(parsed->ui == UiSettings{});
    }
    const auto open = deserializePluginState("esceQ-state 3\nui.output_stage 1\n");
    REQUIRE(open.has_value());
    CHECK(open->ui.outputStageOpen);
}

TEST_CASE("Estado: la pestana del cajon, Output salvo que se elija Macros")
{
    // Output por defecto, y entonces no se escribe: el estado es el de antes de las macros en la interfaz.
    CHECK_FALSE(UiSettings{}.outputStageMacros);
    CHECK(serializePluginState(PluginState{}).find("ui.output_tab") == std::string::npos);

    PluginState state;
    state.ui.outputStageMacros = true;
    state.ui.outputStageOpen = false; // independientes: plegado recuerda la pestana
    const auto text = serializePluginState(state);
    CHECK(text.find("\nui.output_tab 1\n") != std::string::npos);
    const auto restored = deserializePluginState(text);
    REQUIRE(restored.has_value());
    CHECK(*restored == state);

    // Los estados anteriores, y lo que no se entiende, eligen Output.
    for (const char* name : {"v1_default.txt", "v2_ui.txt", "v3_classic.txt", "v4_macros.txt"})
    {
        INFO(name);
        const auto old = deserializePluginState(readFixture(name));
        REQUIRE(old.has_value());
        CHECK_FALSE(old->ui.outputStageMacros);
    }
    for (const char* bad : {"esceQ-state 4\nui.output_tab 2\n", "esceQ-state 4\nui.output_tab -1\n",
                            "esceQ-state 4\nui.output_tab abc\n", "esceQ-state 4\nui.output_tab\n"})
    {
        INFO(bad);
        const auto parsed = deserializePluginState(bad);
        REQUIRE(parsed.has_value());
        CHECK(parsed->ui == UiSettings{});
    }
    const auto output = deserializePluginState("esceQ-state 4\nui.output_tab 0\n");
    REQUIRE(output.has_value());
    CHECK_FALSE(output->ui.outputStageMacros);
}

TEST_CASE("Estado: preferencias de la escala de notas absurdas toman el valor por defecto")
{
    for (const char* text : {"esceQ-state 3\nui.notes 7\nui.a4 399.9\n", "esceQ-state 3\nui.notes -1\nui.a4 480.1\n",
                             "esceQ-state 3\nui.notes abc\nui.a4 abc\n", "esceQ-state 3\nui.notes\nui.a4 nan\n",
                             "esceQ-state 3\nui.notes 2\nui.a4 inf\n", "esceQ-state 3\nui.a4 -440\n"})
    {
        INFO(text);
        const auto state = deserializePluginState(text);
        REQUIRE(state.has_value());
        CHECK(state->ui == UiSettings{});
    }

    // La afinacion va en pasos de 0,1 Hz.
    UiSettings ui;
    ui.a4Hz = 432.25;
    CHECK(sanitize(ui).a4Hz == 432.3);
    ui.a4Hz = 479.99;
    CHECK(sanitize(ui).a4Hz == 480.0);
    ui.a4Hz = std::numeric_limits<double>::quiet_NaN();
    CHECK(sanitize(ui).a4Hz == kDefaultA4Hz);
    const auto stored = deserializePluginState("esceQ-state 3\nui.a4 441.26\n");
    REQUIRE(stored.has_value());
    CHECK(stored->ui.a4Hz == 441.3);
}

TEST_CASE("Estado: preferencias del analizador ampliado")
{
    // Por defecto no se escriben: un estado sin tocarlas es identico al de antes.
    const auto defaults = serializePluginState(PluginState{});
    CHECK(defaults.find("ui.analyzer_") == std::string::npos);

    // Los valores por defecto son los del analizador de antes de existir los ajustes.
    const UiSettings ui;
    CHECK(ui.analyzerFftSize == 4096);
    CHECK(ui.analyzerFallMs == 350);
    CHECK(ui.analyzerTiltDbPerOctave == 4.5);
    CHECK_FALSE(ui.analyzerPeakHold);

    PluginState state;
    state.ui.analyzerFftSize = 16384;
    state.ui.analyzerFallMs = 1000;
    state.ui.analyzerTiltDbPerOctave = 0.0;
    state.ui.analyzerPeakHold = true;
    const auto text = serializePluginState(state);
    CHECK(text.find("\nui.analyzer_fft 16384\n") != std::string::npos);
    CHECK(text.find("\nui.analyzer_fall 1000\n") != std::string::npos);
    CHECK(text.find("\nui.analyzer_tilt 0\n") != std::string::npos);
    CHECK(text.find("\nui.analyzer_peaks 1\n") != std::string::npos);
    const auto restored = deserializePluginState(text);
    REQUIRE(restored.has_value());
    CHECK(*restored == state);

    // Cada una por separado, con todos sus valores.
    for (const int fft : kAnalyzerFftSizes)
    {
        PluginState one;
        one.ui.analyzerFftSize = fft;
        const auto written = serializePluginState(one);
        CHECK((written.find("ui.analyzer_fft") != std::string::npos) == (fft != 4096));
        CHECK(written.find("ui.analyzer_fall") == std::string::npos);
        CHECK(deserializePluginState(written)->ui == one.ui);
    }
    for (const int fall : kAnalyzerFallTimesMs)
    {
        PluginState one;
        one.ui.analyzerFallMs = fall;
        const auto written = serializePluginState(one);
        CHECK((written.find("ui.analyzer_fall") != std::string::npos) == (fall != 350));
        CHECK(deserializePluginState(written)->ui == one.ui);
    }
    for (const double tilt : kAnalyzerTilts)
    {
        PluginState one;
        one.ui.analyzerTiltDbPerOctave = tilt;
        const auto written = serializePluginState(one);
        // NOLINTNEXTLINE(clang-diagnostic-float-equal)
        CHECK((written.find("ui.analyzer_tilt") != std::string::npos) == (tilt != 4.5));
        CHECK(deserializePluginState(written)->ui == one.ui);
    }

    // Los estados anteriores (que no las tienen) cargan con el analizador de siempre.
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v2_ui.txt", "v3_classic.txt", "v3_matched.txt"})
    {
        INFO(name);
        const auto old = deserializePluginState(readFixture(name));
        REQUIRE(old.has_value());
        CHECK(old->ui.analyzerFftSize == 4096);
        CHECK(old->ui.analyzerFallMs == 350);
        CHECK(old->ui.analyzerTiltDbPerOctave == 4.5);
        CHECK_FALSE(old->ui.analyzerPeakHold);
    }

    // Son preferencias de la version 2 en adelante, sin subir de version (ADR 0009).
    const auto v2 = deserializePluginState("esceQ-state 2\nui.analyzer_fft 8192\nui.analyzer_tilt 3\n");
    REQUIRE(v2.has_value());
    CHECK(v2->ui.analyzerFftSize == 8192);
    CHECK(v2->ui.analyzerTiltDbPerOctave == 3.0);
}

TEST_CASE("Estado: preferencias del analizador absurdas toman el valor por defecto")
{
    for (const char* text :
         {"esceQ-state 3\nui.analyzer_fft 4095\nui.analyzer_fall 351\nui.analyzer_tilt 4.4\nui.analyzer_peaks 2\n",
          "esceQ-state 3\nui.analyzer_fft 32768\nui.analyzer_fall 0\nui.analyzer_tilt -3\nui.analyzer_peaks -1\n",
          "esceQ-state 3\nui.analyzer_fft abc\nui.analyzer_fall abc\nui.analyzer_tilt nan\nui.analyzer_peaks abc\n",
          "esceQ-state 3\nui.analyzer_fft\nui.analyzer_fall -350\nui.analyzer_tilt inf\nui.analyzer_peaks\n",
          "esceQ-state 3\nui.analyzer_fft 0\nui.analyzer_tilt 4.50001\n"})
    {
        INFO(text);
        const auto state = deserializePluginState(text);
        REQUIRE(state.has_value());
        CHECK(state->ui == UiSettings{});
    }

    UiSettings ui;
    ui.analyzerFftSize = 1000;
    ui.analyzerFallMs = 7;
    ui.analyzerTiltDbPerOctave = std::numeric_limits<double>::quiet_NaN();
    CHECK(sanitize(ui) == UiSettings{});
}

TEST_CASE("Estado: los estados anteriores al modo de fase abren en IIR")
{
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v2_ui.txt"})
    {
        INFO(name);
        const auto state = deserializePluginState(readFixture(name));
        REQUIRE(state.has_value());
        CHECK(state->parameters[kPhaseMode] == 0.0f);
    }

    // Y el modo se guarda y se lee como cualquier otro parametro.
    auto values = defaultValues();
    values[kPhaseMode] = 2.0f;
    const auto restored = deserializeState(serializeState(values));
    REQUIRE(restored.has_value());
    CHECK((*restored)[kPhaseMode] == 2.0f);
}

TEST_CASE("Estado: los estados anteriores a la compensacion la cargan apagada")
{
    // Si se encendiera sola, un proyecto anterior sonaria con otro volumen.
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v2_ui.txt", "v3_classic.txt", "v3_matched.txt"})
    {
        INFO(name);
        const auto state = deserializePluginState(readFixture(name));
        REQUIRE(state.has_value());
        CHECK(state->parameters[kAutoGain] == 0.0f);
    }

    auto values = defaultValues();
    values[kAutoGain] = 1.0f;
    const auto restored = deserializeState(serializeState(values));
    REQUIRE(restored.has_value());
    CHECK((*restored)[kAutoGain] == 1.0f);
}

TEST_CASE("Estado: los estados anteriores a los tiempos automaticos los cargan apagados")
{
    // Si se encendieran solos, la dinamica de un proyecto anterior cambiaria.
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v2_ui.txt", "v3_classic.txt", "v3_matched.txt"})
    {
        INFO(name);
        const auto state = deserializePluginState(readFixture(name));
        REQUIRE(state.has_value());
        for (int band = 0; band < kNumBands; ++band)
            CHECK(state->parameters[autoTimingParameter(band)] == 0.0f);
    }

    auto values = defaultValues();
    values[autoTimingParameter(23)] = 1.0f;
    const auto restored = deserializeState(serializeState(values));
    REQUIRE(restored.has_value());
    CHECK((*restored)[autoTimingParameter(23)] == 1.0f);
}

TEST_CASE("Estado: los estados anteriores a la dinamica espectral la cargan apagada")
{
    // Si se encendiera sola, el sonido y la latencia de un proyecto anterior cambiarian.
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v1_dynamics.txt", "v2_ui.txt", "v3_classic.txt",
                             "v3_matched.txt", "release_0.1_full.txt"})
    {
        INFO(name);
        const auto state = deserializePluginState(readFixture(name));
        REQUIRE(state.has_value());
        for (int band = 0; band < kNumBands; ++band)
            CHECK(state->parameters[spectralParameter(band)] == 0.0f);
    }

    auto values = defaultValues();
    values[spectralParameter(0)] = 1.0f;
    values[spectralParameter(23)] = 1.0f;
    const auto restored = deserializeState(serializeState(values));
    REQUIRE(restored.has_value());
    CHECK((*restored)[spectralParameter(0)] == 1.0f);
    CHECK((*restored)[spectralParameter(23)] == 1.0f);
    CHECK((*restored)[spectralParameter(12)] == 0.0f);
}

TEST_CASE("Estado: los estados anteriores a la saturacion la cargan en Off")
{
    // PLAN.md 5.8: si se encendiera sola, el sonido y la latencia de un
    // proyecto anterior cambiarian. La saturacion no subio la version del
    // estado (siguio en la 3; la 4 es de las macros): son parametros.
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v1_dynamics.txt", "v2_ui.txt", "v3_classic.txt",
                             "v3_matched.txt", "release_0.1_full.txt"})
    {
        INFO(name);
        const auto state = deserializePluginState(readFixture(name));
        REQUIRE(state.has_value());
        CHECK(state->parameters[kSaturationMode] == 0.0f);
        CHECK(state->parameters[kSaturationDrive] == 6.0f);
        CHECK(state->parameters[kSaturationMix] == 100.0f);
    }

    auto values = defaultValues();
    values[kSaturationMode] = 1.0f;
    values[kSaturationDrive] = 9.5f;
    values[kSaturationMix] = 60.0f;
    const std::string text = serializePluginState({values, {}, {}});
    CHECK(text.rfind("esceQ-state " + std::to_string(kStateVersion) + "\n", 0) == 0);
    const auto restored = deserializePluginState(text);
    REQUIRE(restored.has_value());
    CHECK(restored->parameters[kSaturationMode] == 1.0f);
    CHECK(restored->parameters[kSaturationDrive] == 9.5f);
    CHECK(restored->parameters[kSaturationMix] == 60.0f);
}

// ---------------------------------------------------------------------------
// Version 3: algoritmo de filtro de la instancia (ADR 0013 y 0014)

TEST_CASE("Estado v3: los estados anteriores cargan en Classic")
{
    // Todos los fixtures de las versiones 1 y 2, y el proyecto de la 0.1: se
    // escribieron cuando solo existia Classic, y con el suenan igual.
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v1_bands.txt", "v1_dynamics.txt", "v1_channels.txt",
                             "v2_ui.txt", "release_0.1_full.txt"})
    {
        INFO(name);
        const auto state = deserializePluginState(readFixture(name));
        REQUIRE(state.has_value());
        CHECK(state->filterAlgorithm == esceq::dsp::FilterAlgorithm::Classic);
        // Las bandas que salen de el son exactamente las de la 0.1, que el
        // contrato "un proyecto guardado con la 0.1 suena igual" compara con
        // su audio de referencia.
        for (int band = 0; band < kNumBands; ++band)
            CHECK(bandSettings(state->parameters, band, state->filterAlgorithm).filter ==
                  bandSettings(state->parameters, band).filter);
    }
}

TEST_CASE("Estado v3: una instancia nueva usa Matched")
{
    CHECK(PluginState{}.filterAlgorithm == esceq::dsp::FilterAlgorithm::Matched);
    CHECK(kNewInstanceFilterAlgorithm == esceq::dsp::FilterAlgorithm::Matched);
    CHECK(kLegacyFilterAlgorithm == esceq::dsp::FilterAlgorithm::Classic);
}

TEST_CASE("Estado v3: ida y vuelta del algoritmo de filtro")
{
    for (const auto algorithm : {esceq::dsp::FilterAlgorithm::Classic, esceq::dsp::FilterAlgorithm::Matched})
    {
        PluginState state;
        state.parameters[kOutputGain] = 1.5f;
        state.filterAlgorithm = algorithm;
        const auto text = serializePluginState(state);
        CHECK(text.find("\nfilter_algorithm " + std::to_string(static_cast<int>(algorithm)) + "\n") !=
              std::string::npos);
        const auto restored = deserializePluginState(text);
        REQUIRE(restored.has_value());
        CHECK(*restored == state);
    }
}

TEST_CASE("Estado v3: fixtures con el algoritmo de filtro")
{
    // Un proyecto nuevo, en Matched y en fase lineal.
    const auto matched = deserializePluginState(readFixture("v3_matched.txt"));
    REQUIRE(matched.has_value());
    CHECK(matched->filterAlgorithm == esceq::dsp::FilterAlgorithm::Matched);
    CHECK(matched->parameters[kPhaseMode] == 1.0f);
    CHECK(matched->parameters[kOutputGain] == -2.0f);
    CHECK(matched->parameters[bandParameter(0, BandParam::Frequency)] == 120.0f);
    CHECK(matched->ui.presetName == "Low-End Cleanup");

    // El proyecto de la 0.1, guardado otra vez con la version 3: sigue en
    // Classic, con los mismos parametros y preferencias.
    const auto classic = deserializePluginState(readFixture("v3_classic.txt"));
    const auto original = deserializePluginState(readFixture("release_0.1_full.txt"));
    REQUIRE(classic.has_value());
    REQUIRE(original.has_value());
    CHECK(classic->filterAlgorithm == esceq::dsp::FilterAlgorithm::Classic);
    CHECK(*classic == *original);

    // Mientras kStateVersion sea 3, lo que se escribe a partir de ellos es
    // identico, salvo las lineas de los parametros que se añadan despues.
    if (kStateVersion == 3)
    {
        for (const char* name : {"v3_matched.txt", "v3_classic.txt"})
        {
            INFO(name);
            const auto text = readFixture(name);
            const auto written = serializePluginState(*deserializePluginState(text));
            CHECK(esceq::test::withoutParametersAddedAfter(written, esceq::test::kParametersInV3Fixtures) == text);
        }
    }
}

TEST_CASE("Estado v3: migracion desde la version 2")
{
    // Una linea filter_algorithm en un estado que dice ser de la version 2 no
    // la escribio esceQ (la 2 no la tenia): se descarta y queda Classic.
    for (const char* text : {"esceQ-state 2\nfilter_algorithm 1\n", "esceQ-state 1\nfilter_algorithm 1\n"})
    {
        INFO(text);
        const auto state = deserializePluginState(text);
        REQUIRE(state.has_value());
        CHECK(state->filterAlgorithm == esceq::dsp::FilterAlgorithm::Classic);
    }

    // En la version 3 si cuenta.
    const auto v3 = deserializePluginState("esceQ-state 3\nfilter_algorithm 0\n");
    REQUIRE(v3.has_value());
    CHECK(v3->filterAlgorithm == esceq::dsp::FilterAlgorithm::Classic);

    // Un estado de la 3 sin la linea (escrito a mano): el valor por defecto,
    // como cualquier dato ausente.
    const auto missing = deserializePluginState("esceQ-state 3\noutput_gain 2\n");
    REQUIRE(missing.has_value());
    CHECK(missing->filterAlgorithm == kNewInstanceFilterAlgorithm);
}

TEST_CASE("Estado v3: un algoritmo desconocido o ilegible toma el de las instancias nuevas")
{
    // 7: un algoritmo de una version mas nueva que esta no sabe hacer sonar.
    for (const char* value : {"7", "-1", "abc", ""})
    {
        INFO(value);
        const auto state = deserializePluginState(std::string("esceQ-state 3\nfilter_algorithm ") + value + "\n");
        REQUIRE(state.has_value());
        CHECK(state->filterAlgorithm == kNewInstanceFilterAlgorithm);
    }

    // Tampoco se escribe un valor que no existe (fuera del enum a propósito).
    PluginState state;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    state.filterAlgorithm = static_cast<esceq::dsp::FilterAlgorithm>(9);
    const auto restored = deserializePluginState(serializePluginState(state));
    REQUIRE(restored.has_value());
    CHECK(restored->filterAlgorithm == kNewInstanceFilterAlgorithm);
}

TEST_CASE("Estado v3: el algoritmo no es un parametro ni una preferencia de la interfaz")
{
    // Ninguna version de esceQ lo confunde con un parametro, y una anterior
    // (que no lo conoce) lo ignora y carga los parametros.
    CHECK_FALSE(findParameter("filter_algorithm").has_value());
    PluginState state;
    state.parameters[kOutputGain] = -4.0f;
    const auto values = deserializeState(serializePluginState(state));
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == -4.0f);

    // Lo que guarda un preset (serializeState) no lo lleva.
    CHECK(serializeState(defaultValues()).find("filter_algorithm") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Version 4: asignaciones de las macros (PLAN.md 5.9, ADR 0028)

namespace {

/// Las asignaciones de tests/fixtures/state/v4_macros.txt.
MacroAssignments fixtureMacros()
{
    MacroAssignments macros;
    macros.setSlot(0, 0, bandParameter(0, BandParam::Gain), 20.0f);
    macros.setSlot(0, 1, bandParameter(1, BandParam::Gain), -20.0f);
    macros.setSlot(0, 3, bandParameter(0, BandParam::Frequency), 12.5f);
    macros.setSlot(3, 0, kSaturationDrive, 100.0f);
    macros.setSlot(3, 1, dynamicParameter(0, DynParam::Threshold), -33.3f);
    macros.setSlot(7, 7, kOutputGain, -100.0f);
    return macros;
}

/// `text` con la cabecera de la versión `version`.
std::string withHeaderVersion(const std::string& text, int version)
{
    return "esceQ-state " + std::to_string(version) + text.substr(text.find('\n'));
}

} // namespace

TEST_CASE("Estado v4: ida y vuelta de las asignaciones de las macros")
{
    PluginState state;
    state.parameters[macroParameter(0)] = 50.0f;
    state.macros = fixtureMacros();
    const auto text = serializePluginState(state);
    CHECK(text.starts_with("esceQ-state 4\n"));
    CHECK(text.find("\nmacro.1.1 b01_gain 20\n") != std::string::npos);
    CHECK(text.find("\nmacro.1.4 b01_freq 12.5\n") != std::string::npos);
    CHECK(text.find("\nmacro.8.8 output_gain -100\n") != std::string::npos);
    CHECK(text.find("macro.1.3") == std::string::npos); // un hueco libre no se escribe
    const auto restored = deserializePluginState(text);
    REQUIRE(restored.has_value());
    CHECK(restored->macros == state.macros);
    CHECK(*restored == state);

    // Sin asignaciones, ninguna línea.
    CHECK(serializePluginState(PluginState{}).find("macro.") == std::string::npos);
    CHECK(serializeMacroAssignments(MacroAssignments{}).empty());
}

TEST_CASE("Estado v4: fixture con macros")
{
    const auto text = readFixture("v4_macros.txt");
    const auto state = deserializePluginState(text);
    REQUIRE(state.has_value());
    CHECK(state->macros == fixtureMacros());
    CHECK(state->parameters[macroParameter(0)] == 50.0f);
    CHECK(state->parameters[macroParameter(3)] == 12.5f);
    CHECK(state->parameters[bandParameter(0, BandParam::Gain)] == -12.0f);
    CHECK(state->filterAlgorithm == esceq::dsp::FilterAlgorithm::Matched);
    CHECK(state->ui.presetName == "Macros");

    // Mientras kStateVersion sea 4, se vuelve a escribir idéntico, salvo las
    // líneas de los parámetros que se añadan después.
    if (kStateVersion == 4)
        CHECK(esceq::test::withoutParametersAddedAfter(serializePluginState(*state), kNumParameters) == text);
}

TEST_CASE("Estado v4: los estados anteriores cargan sin asignaciones")
{
    for (const char* name : {"v1_default.txt", "v1_custom.txt", "v1_bands.txt", "v1_dynamics.txt", "v1_channels.txt",
                             "v2_ui.txt", "v3_classic.txt", "v3_matched.txt", "release_0.1_full.txt",
                             "release_0.5_full.txt"})
    {
        INFO(name);
        const auto state = deserializePluginState(readFixture(name));
        REQUIRE(state.has_value());
        CHECK(state->macros.empty());
        for (int macro = 0; macro < kNumMacros; ++macro)
            CHECK(state->parameters[macroParameter(macro)] == 0.0f);
    }
}

TEST_CASE("Estado v4: un estado v3 se vuelve a escribir igual, salvo la cabecera y los parametros nuevos")
{
    // Lo único que cambia al guardar otra vez un proyecto de la 0.5 (o un v3)
    // es la versión de la cabecera y las líneas de los parámetros añadidos
    // después: ni las macros (en 0) ni las asignaciones (ninguna) añaden nada más.
    for (const auto& [name, count] : {std::pair{"v3_matched.txt", esceq::test::kParametersInV3Fixtures},
                                      std::pair{"v3_classic.txt", esceq::test::kParametersInV3Fixtures},
                                      std::pair{"release_0.5_full.txt", std::size_t{391}}})
    {
        INFO(name);
        const auto text = readFixture(name);
        const auto written = serializePluginState(*deserializePluginState(text));
        CHECK(written.starts_with("esceQ-state " + std::to_string(kStateVersion) + "\n"));
        CHECK(esceq::test::withoutParametersAddedAfter(withHeaderVersion(written, 3), count) == text);
    }
}

TEST_CASE("Estado v4: migracion desde la version 3")
{
    // Una línea macro.* en un estado que dice ser anterior a la 4 no la
    // escribió esceQ: se descarta.
    for (int version : {1, 2, 3})
    {
        INFO(version);
        const auto state = deserializePluginState("esceQ-state " + std::to_string(version) +
                                                  "\nmacro_1 40\nmacro.1.1 b01_gain 50\n");
        REQUIRE(state.has_value());
        CHECK(state->macros.empty());
        CHECK(state->parameters[macroParameter(0)] == 40.0f); // el parámetro sí: es un parámetro más
    }

    // En la 4 sí cuenta.
    const auto v4 = deserializePluginState("esceQ-state 4\nmacro.1.1 b01_gain 50\n");
    REQUIRE(v4.has_value());
    CHECK(v4->macros.depth(0, bandParameter(0, BandParam::Gain)) == 50.0f);

    // Y en una versión más nueva, que conserva el formato, también.
    const auto v9 = deserializePluginState("esceQ-state 9\nmacro.2.3 b05_q -10\n");
    REQUIRE(v9.has_value());
    CHECK(v9->macros.targets(1)[2] == MacroTarget{bandParameter(4, BandParam::Q), -10.0f});
}

TEST_CASE("Estado v4: las asignaciones que no se entienden se descartan")
{
    const auto gain1 = bandParameter(0, BandParam::Gain);
    for (const char* line : {"macro.0.1 b01_gain 10", "macro.9.1 b01_gain 10", "macro.1.0 b01_gain 10",
                             "macro.1.9 b01_gain 10", "macro.10.1 b01_gain 10", "macro.1 b01_gain 10",
                             "macro.1.1", "macro.1.1 b01_gain", "macro.1.1 b01_gain abc", "macro.1.1 nada 10",
                             "macro.1.1 bypass 10", "macro.1.1 b01_type 10", "macro.1.1 macro_2 10",
                             "macro.1.1 b01_gain nan", "macro.1.1 b01_gain inf", "macro.a.1 b01_gain 10",
                             "macro.+1.1 b01_gain 10", "macro..1 b01_gain 10"})
    {
        INFO(line);
        const auto state = deserializePluginState(std::string("esceQ-state 4\noutput_gain 2\n") + line + "\n");
        REQUIRE(state.has_value());
        CHECK(state->macros.empty());
        CHECK(state->parameters[kOutputGain] == 2.0f); // lo demás carga
    }

    // Fuera de rango, se limita.
    const auto big = deserializePluginState("esceQ-state 4\nmacro.1.1 b01_gain 250\nmacro.1.2 b02_gain -1e9\n");
    REQUIRE(big.has_value());
    CHECK(big->macros.depth(0, gain1) == 100.0f);
    CHECK(big->macros.depth(0, bandParameter(1, BandParam::Gain)) == -100.0f);

    // Un destino repetido en la misma macro: el primero.
    const auto repeated = deserializePluginState("esceQ-state 4\nmacro.1.1 b01_gain 10\nmacro.1.2 b01_gain 20\n");
    REQUIRE(repeated.has_value());
    CHECK(repeated->macros.depth(0, gain1) == 10.0f);
    CHECK_FALSE(repeated->macros.targets(0)[1].used());

    // Un hueco repetido: la última línea; y si no se entiende, el hueco queda libre.
    const auto twice = deserializePluginState("esceQ-state 4\nmacro.1.1 b01_gain 10\nmacro.1.1 b02_gain 20\n");
    REQUIRE(twice.has_value());
    CHECK(twice->macros.targets(0)[0] == MacroTarget{bandParameter(1, BandParam::Gain), 20.0f});
    const auto cleared = deserializePluginState("esceQ-state 4\nmacro.1.1 b01_gain 10\nmacro.1.1 bypass 20\n");
    REQUIRE(cleared.has_value());
    CHECK(cleared->macros.empty());
}

TEST_CASE("Estado v4: una version anterior lo abre con todos sus parametros")
{
    // Una versión anterior de esceQ (la 0.5, con el estado v3) lee los
    // parámetros que conoce e ignora las claves que no conoce ("Estado: un
    // estado de una version futura carga lo que se conoce"). Las líneas de un
    // estado v4 que no son parámetros de la 0.5 son los parámetros nuevos
    // (macro_N, que no conoce), las asignaciones, filter_algorithm y ui.*:
    // ninguna se confunde con un parámetro, y los de la 0.5 están todos.
    const auto text = readFixture("v4_macros.txt");
    std::istringstream in(text);
    std::string line;
    std::getline(in, line);
    CHECK(line == "esceQ-state 4");
    std::size_t parametersSeen = 0;
    while (std::getline(in, line))
    {
        const auto key = line.substr(0, line.find(' '));
        INFO(key);
        if (const auto index = findParameter(key))
        {
            ++parametersSeen;
            continue;
        }
        CHECK((key == "filter_algorithm" || key.starts_with("ui.") || key.starts_with("macro.")));
    }
    CHECK(parametersSeen == kNumParameters);

    // Y lo que lee de los 391 de la 0.5, sin las macros, es lo guardado.
    const auto values = deserializeState(text);
    REQUIRE(values.has_value());
    CHECK((*values)[bandParameter(0, BandParam::Gain)] == -12.0f);
    CHECK((*values)[kSaturationMode] == 1.0f);
}

TEST_CASE("Estado v4: las asignaciones no son parametros")
{
    for (int macro = 1; macro <= kNumMacros; ++macro)
        CHECK_FALSE(findParameter("macro." + std::to_string(macro) + ".1").has_value());
    // Lo que guarda serializeState (solo parámetros) no las lleva.
    CHECK(serializeState(defaultValues()).find("macro.") == std::string::npos);
}
