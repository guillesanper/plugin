// SPDX-License-Identifier: AGPL-3.0-or-later
//
// CONTRATOS DE LAS VERSIONES 0.1.0 Y 0.5.0.
//
// Los archivos de tests/fixtures/*/release_0.1* y release_0.5* son lo que
// cada version declaro, escribio y sono. No se editan ni se borran, y estos
// tests no se "arreglan" cambiando lo que esperan: si uno falla, el cambio
// rompe proyectos guardados por usuarios. Ver docs/arquitectura.md, "Contratos".
// (La 0.1.0 no llego a publicarse, pero sus contratos se mantienen: los
// proyectos de las compilaciones de entonces deben seguir abriendo igual.)
#include "esceq/core/band_model.h"
#include "esceq/core/presets.h"
#include "esceq/core/state.h"
#include "state_test_utils.h"
#include "esceq/dsp/equalizer.h"
#include "esceq/dsp/phase_equalizer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq;
using namespace esceq::core;

namespace {

const std::string kFixtures = ESCEQ_FIXTURES_DIR;

std::string readFile(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        out.push_back(line);
    }
    return out;
}

/// Audio de prueba determinista: ruido de banda ancha con ráfagas, distinto
/// en cada canal, para que actúen la dinámica y las bandas Mid, Side e izquierda.
void testSignal(std::vector<float>& left, std::vector<float>& right)
{
    std::uint32_t state = 20261112u;
    auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    };
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        // Ráfagas de medio segundo cada segundo: la dinámica entra y sale.
        const float envelope = (i / 12000) % 2 == 0 ? 0.6f : 0.05f;
        const float centre = next();
        const float side = next();
        left[i] = envelope * (0.7f * centre + 0.3f * side);
        right[i] = envelope * (0.7f * centre - 0.3f * side);
    }
}

} // namespace

TEST_CASE("Contrato 0.1: los parametros publicados no cambian")
{
    // release_0.1.txt es la tabla tal y como se publico. v1.txt (la tabla
    // viva, que solo crece) debe seguir empezando exactamente igual, y el
    // test "Parametros: contrato congelado" comprueba que el codigo coincide
    // con v1.txt.
    const auto released = lines(readFile(kFixtures + "/parameters/release_0.1.txt"));
    const auto current = lines(readFile(kFixtures + "/parameters/v1.txt"));

    REQUIRE(released.size() == 339); // cabecera + 338 parametros
    REQUIRE(current.size() >= released.size());
    for (std::size_t i = 0; i < released.size(); ++i)
    {
        INFO("linea " << i + 1);
        CHECK(current[i] == released[i]);
    }
    CHECK(kNumParameters >= 338);
}

TEST_CASE("Contrato 0.1: un proyecto guardado con la 0.1 carga con todos sus valores")
{
    const auto text = readFile(kFixtures + "/state/release_0.1_full.txt");
    const auto state = deserializePluginState(text);
    REQUIRE(state.has_value());

    const auto& p = state->parameters;
    CHECK(p[kOutputGain] == -1.5f);
    CHECK(p[kBypass] == 0.0f);

    // Bandas del preset Vocal...
    CHECK(bandSettings(p, 0).filter.type == dsp::FilterType::LowCut);
    CHECK(bandSettings(p, 0).filter.frequencyHz == 90.0);
    CHECK(bandSettings(p, 0).filter.slopeDbPerOct == 18);
    CHECK(bandSettings(p, 3).filter.type == dsp::FilterType::HighShelf);
    CHECK(bandSettings(p, 3).filter.gainDb == 3.0);
    CHECK(bandSettings(p, 4).dynamics.enabled);
    CHECK(bandSettings(p, 4).dynamics.rangeDb == -6.0);

    // ...y lo añadido encima: dinamica con sidechain en Mid, tilt, corte en Side, notch en Left.
    const auto dynamic = bandSettings(p, 2);
    CHECK(dynamic.channel == dsp::ChannelMode::Mid);
    CHECK(dynamic.dynamics.enabled);
    CHECK(dynamic.dynamics.external);
    CHECK(dynamic.dynamics.thresholdDb == -18.0);
    CHECK(dynamic.dynamics.rangeDb == 4.0);
    CHECK(dynamic.dynamics.attackMs == 12.5);
    CHECK(dynamic.dynamics.releaseMs == 250.0);
    CHECK(dynamic.dynamics.kneeDb == 0.0);

    CHECK(bandSettings(p, 5).filter.type == dsp::FilterType::Tilt);
    CHECK(bandSettings(p, 5).filter.gainDb == 1.5);
    CHECK(bandSettings(p, 6).filter.type == dsp::FilterType::HighCut);
    CHECK(bandSettings(p, 6).filter.slopeDbPerOct == 48);
    CHECK(bandSettings(p, 6).channel == dsp::ChannelMode::Side);
    CHECK(bandSettings(p, 23).filter.type == dsp::FilterType::Notch);
    CHECK(bandSettings(p, 23).filter.q == 12.0);
    CHECK(bandSettings(p, 23).channel == dsp::ChannelMode::Left);

    int enabled = 0;
    for (int band = 0; band < kNumBands; ++band)
        enabled += bandSettings(p, band).enabled ? 1 : 0;
    CHECK(enabled == 8);

    CHECK(state->ui.analyzerMode == 2);
    CHECK(state->ui.displayRangeDb == 30);
    CHECK(state->ui.windowWidth == 960);
    CHECK(state->ui.windowHeight == 580);
    CHECK(state->ui.presetName == "Vocal");
    // La 0.1 no tenia modo de fase: el proyecto abre en IIR, sin latencia.
    CHECK(state->parameters[kPhaseMode] == 0.0f);

    // Mientras el formato siga en la version 2, se vuelve a escribir identico
    // (sin las lineas de los parametros añadidos despues de la 0.1, que tenia 338).
    if (kStateVersion == 2)
        CHECK(test::withoutParametersAddedAfter(serializePluginState(*state), test::kParametersInV2Fixtures) == text);
}

TEST_CASE("Contrato 0.1: los presets de fabrica publicados no cambian")
{
    for (const char* name : {"De-esser", "Resonance Control", "Low-End Cleanup", "Vocal", "Gentle Master"})
    {
        INFO(name);
        const auto released = deserializeState(readFile(kFixtures + "/presets/release_0.1/" + name + ".txt"));
        REQUIRE(released.has_value());
        const auto current = loadFactoryPreset(name);
        REQUIRE(current.has_value());
        // Los 338 parametros de la 0.1, uno a uno. (Los que se añadan despues
        // deben tener un valor por defecto que no cambie el sonido.)
        for (std::size_t i = 0; i < 338; ++i)
        {
            INFO(parameters()[i].id);
            CHECK((*current)[i] == (*released)[i]);
        }
    }
}

// Para generar el archivo la primera vez (y SOLO si no existe):
//   ESCEQ_WRITE_GOLDEN=1 ./esceq_tests "Contrato 0.1: un proyecto*suena*"
TEST_CASE("Contrato 0.1: un proyecto guardado con la 0.1 suena igual")
{
    const std::string path = kFixtures + "/dsp/release_0.1_audio.txt";
    const auto state = deserializePluginState(readFile(kFixtures + "/state/release_0.1_full.txt"));
    REQUIRE(state.has_value());

    constexpr double kSampleRate = 48000.0;
    constexpr int kLength = 48000;
    constexpr int kStep = 61; // se guarda una muestra de cada 61 (primo: no se alinea con nada)

    dsp::Equalizer equalizer;
    for (int band = 0; band < kNumBands; ++band)
        equalizer.setBand(band, bandSettings(state->parameters, band));
    equalizer.prepare(kSampleRate);

    std::vector<float> left(kLength), right(kLength), scLeft(kLength), scRight(kLength);
    testSignal(left, right);
    // El sidechain: la misma señal, mas fuerte y con los canales cambiados.
    for (int i = 0; i < kLength; ++i)
    {
        scLeft[static_cast<std::size_t>(i)] = 1.5f * right[static_cast<std::size_t>(i)];
        scRight[static_cast<std::size_t>(i)] = 1.5f * left[static_cast<std::size_t>(i)];
    }

    // En bloques de tamaño irregular, como los entrega FL Studio.
    double deepestReduction = 0.0; // banda 5: de-esser
    double highestBoost = 0.0;     // banda 3: realce con sidechain
    for (int position = 0, block = 0; position < kLength; ++block)
    {
        const int size = std::min(kLength - position, 1 + (block * 37) % 509);
        float* channels[2] = {left.data() + position, right.data() + position};
        const float* sidechain[2] = {scLeft.data() + position, scRight.data() + position};
        equalizer.process(channels, 2, size, sidechain, 2);
        position += size;
        deepestReduction = std::min(deepestReduction, equalizer.dynamicGainDb(4));
        highestBoost = std::max(highestBoost, equalizer.dynamicGainDb(2));
    }

    if (std::getenv("ESCEQ_WRITE_GOLDEN") != nullptr && !std::ifstream(path).is_open())
    {
        std::ofstream out(path, std::ios::binary);
        out.imbue(std::locale::classic());
        out.precision(9);
        out << "# Salida de dsp::Equalizer con el estado release_0.1_full.txt (esceQ 0.1.0).\n"
            << "# 48 kHz, una muestra de cada " << kStep << ": izquierda derecha\n";
        for (int i = 0; i < kLength; i += kStep)
            out << left[static_cast<std::size_t>(i)] << ' ' << right[static_cast<std::size_t>(i)] << '\n';
    }

    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.is_open());
    in.imbue(std::locale::classic());
    int compared = 0;
    double worst = 0.0;
    for (std::string line; std::getline(in, line);)
    {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream fields(line);
        fields.imbue(std::locale::classic());
        double expectedLeft = 0.0, expectedRight = 0.0;
        REQUIRE(static_cast<bool>(fields >> expectedLeft >> expectedRight));
        const auto index = static_cast<std::size_t>(compared * kStep);
        REQUIRE(index < left.size());
        worst = std::max({worst, std::abs(left[index] - expectedLeft), std::abs(right[index] - expectedRight)});
        ++compared;
    }
    CHECK(compared == (kLength + kStep - 1) / kStep);
    // -80 dB respecto a fondo de escala: muy por debajo de lo audible, y con
    // margen para las diferencias de redondeo entre compiladores y plataformas.
    INFO("mayor diferencia: " << worst);
    CHECK(worst < 1.0e-4);

    // La señal de prueba hace trabajar de verdad a la dinamica.
    CHECK(deepestReduction < -1.0);
    CHECK(highestBoost > 1.0);
}

// --- 0.5.0 ---

namespace {

/// release_0.5_full.txt: el proyecto de la 0.1 (release_0.1_full.txt) con lo
/// que trae la 0.5 encendido: Matched, Linear Low, compensacion automatica,
/// tiempos automaticos en la banda 3 (dinamica con sidechain en Mid),
/// dinamica espectral en la banda 5 (de-esser), saturacion Warm (9 dB, 80 %)
/// y las preferencias nuevas de la interfaz.
std::optional<PluginState> release05State()
{
    return deserializePluginState(readFile(kFixtures + "/state/release_0.5_full.txt"));
}

struct Release05Render
{
    std::vector<float> left, right;
    int latency = 0;
    double deepestReduction = 0.0; // banda 5: de-esser espectral
    double highestBoost = 0.0;     // banda 3: realce con sidechain y tiempos automaticos
};

/// Salida de dsp::PhaseEqualizer con el estado de release_0.5_full.txt en el
/// modo de fase `mode`, preparada como en el plugin al arrancar.
Release05Render renderRelease05(const PluginState& state, dsp::PhaseMode mode)
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kLength = 48000;

    dsp::PhaseEqualizer equalizer;
    const auto bands = allBandSettings(state.parameters, state.filterAlgorithm);
    for (int band = 0; band < kNumBands; ++band)
        equalizer.setBand(band, bands[static_cast<std::size_t>(band)]);
    equalizer.setBypassed(false);
    equalizer.setPhaseMode(mode);
    equalizer.setSaturation(saturationSettings(state.parameters));
    equalizer.prepare(kSampleRate);
    while (equalizer.serviceDesigns())
    {
    }
    equalizer.update();
    // Suena ya lo pedido, sin transicion (lo que hace el plugin en prepareToPlay).
    REQUIRE(equalizer.activePhaseMode() == mode);
    REQUIRE(equalizer.spectralActive());
    REQUIRE(equalizer.saturationActive());

    Release05Render render;
    render.left.resize(kLength);
    render.right.resize(kLength);
    render.latency = equalizer.latencySamples();
    testSignal(render.left, render.right);
    std::vector<float> scLeft(kLength), scRight(kLength);
    for (std::size_t i = 0; i < scLeft.size(); ++i)
    {
        scLeft[i] = 1.5f * render.right[i];
        scRight[i] = 1.5f * render.left[i];
    }

    // En bloques de tamaño irregular, como los entrega FL Studio.
    for (int position = 0, block = 0; position < kLength; ++block)
    {
        const int size = std::min(kLength - position, 1 + (block * 37) % 509);
        float* channels[2] = {render.left.data() + position, render.right.data() + position};
        const float* sidechain[2] = {scLeft.data() + position, scRight.data() + position};
        equalizer.process(channels, 2, size, sidechain, 2);
        position += size;
        render.deepestReduction = std::min(render.deepestReduction, equalizer.dynamicGainDb(4));
        render.highestBoost = std::max(render.highestBoost, equalizer.dynamicGainDb(2));
    }
    return render;
}

/// Compara `render` con el archivo `name` de tests/fixtures/dsp/. Con
/// ESCEQ_WRITE_GOLDEN=1, lo escribe antes si no existe.
void checkAgainstGolden(const Release05Render& render, const std::string& name, const std::string& description)
{
    constexpr int kStep = 61; // se guarda una muestra de cada 61 (primo: no se alinea con nada)
    const std::string path = kFixtures + "/dsp/" + name;
    const auto length = static_cast<int>(render.left.size());

    if (std::getenv("ESCEQ_WRITE_GOLDEN") != nullptr && !std::ifstream(path).is_open())
    {
        std::ofstream out(path, std::ios::binary);
        out.imbue(std::locale::classic());
        out.precision(9);
        out << "# Salida de dsp::PhaseEqualizer con el estado release_0.5_full.txt (esceQ 0.5.0), " << description
            << ".\n# 48 kHz, una muestra de cada " << kStep << ": izquierda derecha\n";
        for (int i = 0; i < length; i += kStep)
            out << render.left[static_cast<std::size_t>(i)] << ' ' << render.right[static_cast<std::size_t>(i)]
                << '\n';
    }

    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.is_open());
    in.imbue(std::locale::classic());
    int compared = 0;
    double worst = 0.0;
    for (std::string line; std::getline(in, line);)
    {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream fields(line);
        fields.imbue(std::locale::classic());
        double expectedLeft = 0.0, expectedRight = 0.0;
        REQUIRE(static_cast<bool>(fields >> expectedLeft >> expectedRight));
        const auto index = static_cast<std::size_t>(compared * kStep);
        REQUIRE(index < render.left.size());
        worst = std::max({worst, std::abs(render.left[index] - expectedLeft),
                          std::abs(render.right[index] - expectedRight)});
        ++compared;
    }
    CHECK(compared == (length + kStep - 1) / kStep);
    // -80 dB respecto a fondo de escala, como en la 0.1.
    INFO("mayor diferencia: " << worst);
    CHECK(worst < 1.0e-4);
}

} // namespace

TEST_CASE("Contrato 0.5: los parametros publicados no cambian")
{
    // Como en la 0.1: v1.txt (la tabla viva) debe seguir empezando por la de la 0.5.
    const auto released = lines(readFile(kFixtures + "/parameters/release_0.5.txt"));
    const auto current = lines(readFile(kFixtures + "/parameters/v1.txt"));

    REQUIRE(released.size() == 392); // cabecera + 391 parametros
    REQUIRE(current.size() >= released.size());
    for (std::size_t i = 0; i < released.size(); ++i)
    {
        INFO("linea " << i + 1);
        CHECK(current[i] == released[i]);
    }
    CHECK(kNumParameters >= 391);
}

TEST_CASE("Contrato 0.5: un proyecto guardado con la 0.5 carga con todos sus valores")
{
    const auto text = readFile(kFixtures + "/state/release_0.5_full.txt");
    const auto state = release05State();
    REQUIRE(state.has_value());
    const auto& p = state->parameters;

    // Lo de la 0.1 sigue ahi.
    CHECK(p[kOutputGain] == -1.5f);
    CHECK(bandSettings(p, 0).filter.type == dsp::FilterType::LowCut);
    CHECK(bandSettings(p, 2).dynamics.external);
    CHECK(bandSettings(p, 2).channel == dsp::ChannelMode::Mid);
    CHECK(bandSettings(p, 23).filter.type == dsp::FilterType::Notch);

    // Lo nuevo de la 0.5.
    CHECK(state->filterAlgorithm == dsp::FilterAlgorithm::Matched);
    CHECK(bandSettings(p, 0, state->filterAlgorithm).filter.algorithm == dsp::FilterAlgorithm::Matched);
    CHECK(phaseModeFromChoice(p[kPhaseMode]) == dsp::PhaseMode::LinearLow);
    CHECK(p[kAutoGain] == 1.0f);
    CHECK(bandSettings(p, 2).dynamics.autoTiming);
    CHECK(bandSettings(p, 4).dynamics.spectral);
    int spectral = 0, autoTiming = 0;
    for (int band = 0; band < kNumBands; ++band)
    {
        spectral += bandSettings(p, band).dynamics.spectral ? 1 : 0;
        autoTiming += bandSettings(p, band).dynamics.autoTiming ? 1 : 0;
    }
    CHECK(spectral == 1);
    CHECK(autoTiming == 1);
    const auto saturation = saturationSettings(p);
    CHECK(saturation.mode == dsp::SaturationMode::Warm);
    CHECK(saturation.driveDb == 9.0);
    CHECK_THAT(saturation.mix, WithinAbs(0.8, 1e-6));

    CHECK(state->ui.analyzerMode == 2);
    CHECK(state->ui.displayRangeDb == 30);
    CHECK(state->ui.windowWidth == 1480);
    CHECK(state->ui.windowHeight == 860);
    CHECK(state->ui.presetName == "Vocal");
    CHECK(state->ui.pianoKeyboard);
    CHECK(state->ui.a4Hz == 432.0);
    CHECK(state->ui.analyzerFftSize == 16384);
    CHECK(state->ui.analyzerFallMs == 120);
    CHECK(state->ui.analyzerTiltDbPerOctave == 3.0);
    CHECK(state->ui.analyzerPeakHold);
    CHECK_FALSE(state->ui.outputStageOpen);

    // Mientras el formato siga en la version 3, se vuelve a escribir identico
    // (sin las lineas de los parametros añadidos despues de la 0.5, que tenia 391).
    if (kStateVersion == 3)
        CHECK(test::withoutParametersAddedAfter(serializePluginState(*state), 391) == text);
}

TEST_CASE("Contrato 0.5: los presets de fabrica publicados no cambian")
{
    for (const char* name : {"De-esser", "Resonance Control", "Low-End Cleanup", "Vocal", "Gentle Master"})
    {
        INFO(name);
        const auto released = deserializeState(readFile(kFixtures + "/presets/release_0.5/" + name + ".txt"));
        REQUIRE(released.has_value());
        const auto current = loadFactoryPreset(name);
        REQUIRE(current.has_value());
        for (std::size_t i = 0; i < 391; ++i)
        {
            INFO(parameters()[i].id);
            CHECK((*current)[i] == (*released)[i]);
        }
    }
}

// Para generar los archivos la primera vez (y SOLO si no existen):
//   ESCEQ_WRITE_GOLDEN=1 ./esceq_tests "Contrato 0.5: un proyecto*suena*"
TEST_CASE("Contrato 0.5: un proyecto guardado con la 0.5 suena igual")
{
    const auto state = release05State();
    REQUIRE(state.has_value());

    SECTION("en su modo, Linear Low")
    {
        const auto render = renderRelease05(*state, phaseModeFromChoice(state->parameters[kPhaseMode]));
        // La latencia que se comunica al host: Linear Low, la dinamica
        // espectral y la saturacion en fase lineal.
        CHECK(render.latency == 1280 + 2560 + 47);
        checkAgainstGolden(render, "release_0.5_audio.txt", "en Linear Low");
        CHECK(render.deepestReduction < -1.0);
        CHECK(render.highestBoost > 1.0);
    }
    SECTION("el mismo proyecto en IIR")
    {
        const auto render = renderRelease05(*state, dsp::PhaseMode::Iir);
        CHECK(render.latency == 2560); // solo la dinamica espectral
        checkAgainstGolden(render, "release_0.5_audio_iir.txt", "en IIR");
        CHECK(render.deepestReduction < -1.0);
        CHECK(render.highestBoost > 1.0);
    }
}
