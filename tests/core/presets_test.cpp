// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/presets.h"

#include "esceq/core/band_model.h"
#include "esceq/core/state.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>
#include <sstream>
#include <string>

using namespace esceq::core;

TEST_CASE("Presets de fabrica: los cinco que se anuncian, con nombres unicos")
{
    const auto presets = factoryPresets();
    REQUIRE(presets.size() == 5);

    // CONTRATO: nombre y posicion de los presets publicados.
    CHECK(presets[0].name == "De-esser");
    CHECK(presets[1].name == "Resonance Control");
    CHECK(presets[2].name == "Low-End Cleanup");
    CHECK(presets[3].name == "Vocal");
    CHECK(presets[4].name == "Gentle Master");

    std::set<std::string_view> names;
    for (const auto& preset : presets)
    {
        CHECK(names.insert(preset.name).second);
        // El nombre sirve tal cual como nombre de archivo y cabe en el estado.
        CHECK(sanitizePresetName(preset.name) == preset.name);
    }
}

TEST_CASE("Presets de fabrica: estan en el formato de estado y solo usan parametros que existen")
{
    for (const auto& preset : factoryPresets())
    {
        INFO(preset.name);
        const auto values = deserializeState(preset.state);
        REQUIRE(values.has_value());
        CHECK(loadFactoryPreset(preset.name) == values);

        // Cada linea es un parametro conocido con un valor dentro de su rango:
        // un error de escritura en un preset no pasa desapercibido.
        std::istringstream lines{std::string(preset.state)};
        std::string line;
        std::getline(lines, line);
        CHECK(line == "esceQ-state 2");
        int count = 0;
        while (std::getline(lines, line))
        {
            INFO(line);
            std::istringstream fields(line);
            std::string id;
            float value = 0.0f;
            REQUIRE(static_cast<bool>(fields >> id >> value));
            const auto index = findParameter(id);
            REQUIRE(index.has_value());
            CHECK(sanitize(parameters()[*index], value) == value);
            CHECK((*values)[*index] == value);
            ++count;
        }
        CHECK(count > 0);

        // Hace algo: al menos una banda activa, y no llega con el bypass puesto.
        int enabled = 0;
        for (int band = 0; band < kNumBands; ++band)
            enabled += bandSettings(*values, band).enabled ? 1 : 0;
        CHECK(enabled > 0);
        CHECK((*values)[kBypass] == 0.0f);
        CHECK((*values)[kOutputGain] == 0.0f);
    }

    CHECK_FALSE(loadFactoryPreset("No existe").has_value());
}

TEST_CASE("Presets de fabrica: lo que promete cada uno")
{
    using esceq::dsp::ChannelMode;
    using esceq::dsp::FilterType;

    const auto deEsser = *loadFactoryPreset("De-esser");
    const auto band = bandSettings(deEsser, 0);
    CHECK(band.filter.type == FilterType::Bell);
    CHECK(band.filter.frequencyHz > 4000.0);
    CHECK(band.dynamics.enabled);
    CHECK(band.dynamics.rangeDb < 0.0); // reduce, no realza

    const auto resonances = *loadFactoryPreset("Resonance Control");
    for (int i = 0; i < 3; ++i)
    {
        const auto b = bandSettings(resonances, i);
        CHECK(b.enabled);
        CHECK(b.dynamics.enabled);
        CHECK(b.filter.q >= 4.0);
        CHECK(b.dynamics.rangeDb < 0.0);
    }

    const auto lowEnd = *loadFactoryPreset("Low-End Cleanup");
    CHECK(bandSettings(lowEnd, 0).filter.type == FilterType::LowCut);
    CHECK(bandSettings(lowEnd, 0).filter.slopeDbPerOct == 24);
    CHECK(bandSettings(lowEnd, 2).filter.type == FilterType::LowCut);
    CHECK(bandSettings(lowEnd, 2).channel == ChannelMode::Side);

    const auto vocal = *loadFactoryPreset("Vocal");
    CHECK(bandSettings(vocal, 0).filter.type == FilterType::LowCut);
    CHECK(bandSettings(vocal, 3).filter.type == FilterType::HighShelf);
    CHECK(bandSettings(vocal, 4).dynamics.enabled);

    // Master suave: ningun retoque pasa de 2 dB y no hay dinamica.
    const auto master = *loadFactoryPreset("Gentle Master");
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto b = bandSettings(master, i);
        if (!b.enabled)
            continue;
        CHECK(b.filter.gainDb <= 2.0);
        CHECK(b.filter.gainDb >= -2.0);
        CHECK_FALSE(b.dynamics.enabled);
    }
}

TEST_CASE("Presets: el texto compacto se lee como los mismos valores")
{
    // Por defecto: solo la cabecera.
    CHECK(serializePreset(defaultValues()) == "esceQ-state 2\n");

    ParameterValues values = defaultValues();
    std::uint32_t rng = 99u;
    for (std::size_t i = 0; i < kNumParameters; i += 3)
    {
        rng = rng * 1664525u + 1013904223u;
        values[i] = fromNormalized(parameters()[i], static_cast<float>(rng >> 8) / 16777216.0f);
    }

    const auto text = serializePreset(values);
    CHECK(deserializeState(text) == values);
    CHECK(text.size() < serializeState(values).size());

    // Un preset no lleva preferencias de la interfaz.
    CHECK(text.find("ui.") == std::string::npos);
    const auto state = deserializePluginState(text);
    REQUIRE(state.has_value());
    CHECK(state->ui == UiSettings{});
}

TEST_CASE("Presets: nombres que sirven como nombre de archivo")
{
    CHECK(sanitizePresetName("My preset") == "My preset");
    CHECK(sanitizePresetName("  Vocal 2  ") == "Vocal 2");
    CHECK(sanitizePresetName("a/b\\c:d*e?f\"g<h>i|j") == "abcdefghij");
    CHECK(sanitizePresetName("..hidden") == "hidden");
    CHECK(sanitizePresetName("trailing. . ") == "trailing");
    CHECK(sanitizePresetName(std::string("tab\there\nnew", 12)) == "tabherenew");
    CHECK(sanitizePresetName("Voz en espa\xC3\xB1ol") == "Voz en espa\xC3\xB1ol"); // UTF-8 se conserva

    // Nada utilizable.
    CHECK(sanitizePresetName("").empty());
    CHECK(sanitizePresetName("   ").empty());
    CHECK(sanitizePresetName("...").empty());
    CHECK(sanitizePresetName("///").empty());

    // Nombres reservados de Windows, con o sin extension y en cualquier caja.
    for (const char* reserved : {"CON", "con", "NUL", "nul.txt", "COM1", "lpt9", "Aux", "PRN"})
    {
        INFO(reserved);
        CHECK(sanitizePresetName(reserved).empty());
    }
    CHECK(sanitizePresetName("CONSOLE") == "CONSOLE");
    CHECK(sanitizePresetName("COM0") == "COM0");
    CHECK(sanitizePresetName("Common") == "Common");

    // Longitud maxima, sin partir un caracter de varios bytes.
    const auto longName = sanitizePresetName(std::string(300, 'a'));
    CHECK(longName.size() == 64);
    std::string accents;
    for (int i = 0; i < 100; ++i)
        accents += "\xC3\xB1";
    const auto cut = sanitizePresetName(accents);
    CHECK(cut.size() == 64);
    CHECK(cut.size() % 2 == 0);
    CHECK(sanitizePresetName(std::string(63, 'a') + "\xC3\xB1").size() == 63);
}

TEST_CASE("Presets: cargar un preset no cambia el modo de fase")
{
    // Cambiarlo cambiaria la latencia de la pista al elegir un preset.
    auto current = defaultValues();
    current[kPhaseMode] = 3.0f;
    auto preset = *loadFactoryPreset("Gentle Master");
    const auto result = applyPreset(preset, current);
    CHECK(result[kPhaseMode] == 3.0f);
    preset[kPhaseMode] = current[kPhaseMode];
    CHECK(result == preset); // el resto, el del preset
}

TEST_CASE("Presets: el modo de fase no se guarda en el preset")
{
    auto values = defaultValues();
    values[kPhaseMode] = 2.0f;
    values[kOutputGain] = 3.0f;
    const auto text = serializePreset(values);
    CHECK(text.find("phase_mode") == std::string::npos);
    CHECK(text.find("output_gain 3") != std::string::npos);
}
