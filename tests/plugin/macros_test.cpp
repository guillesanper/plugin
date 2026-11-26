// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Macros vistas desde el host (PLAN.md 5.9, bloque 7.2): lo que suena es el
// parámetro más la modulación, el valor del host no cambia, las asignaciones
// viajan con el estado y los presets, y cambiarlas se deshace.
#include "test_support.h"

#include "esceq/core/macros.h"
#include "esceq/core/presets.h"
#include "esceq/core/state.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

using namespace esceq;
using namespace esceq::test;

namespace {

constexpr double kRate = 48000.0;
constexpr int kMaxBlock = 4096;

const std::size_t kGain1 = core::bandParameter(0, core::BandParam::Gain);
const std::size_t kGain2 = core::bandParameter(1, core::BandParam::Gain);
const std::size_t kMacro1 = core::macroParameter(0);

void prepare(PluginProcessor& p, int blockSize = 256)
{
    p.setRateAndBufferSizeDetails(kRate, std::max(blockSize, kMaxBlock));
    p.prepareToPlay(kRate, std::max(blockSize, kMaxBlock));
}

std::vector<float> noise(int length, std::uint32_t seed = 7u)
{
    std::vector<float> x(static_cast<std::size_t>(length));
    for (auto& v : x)
    {
        seed = seed * 1664525u + 1013904223u;
        v = 0.4f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
    return x;
}

/// Lo que hace el host: pone un parámetro sin gestos (un automation clip).
void automate(PluginProcessor& p, std::size_t index, float value)
{
    auto& parameter = p.parameter(index);
    parameter.setValueNotifyingHost(parameter.convertTo0to1(value));
}

/// Pasa `input` (los dos canales iguales) en bloques de `blockSize`, cortados
/// en cada múltiplo de `changeEvery` muestras; en esos puntos, y antes del
/// primer bloque, llama a `change(n)` con el número de cambio. Así los
/// cambios caen en las mismas muestras con cualquier tamaño de bloque, como
/// los puntos de un automation clip.
std::vector<float> render(PluginProcessor& p, const std::vector<float>& input, int blockSize, int changeEvery,
                          const std::function<void(int)>& change)
{
    juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()),
                                    std::max(blockSize, 1));
    juce::MidiBuffer midi;
    std::vector<float> out;
    out.reserve(input.size());
    std::size_t pos = 0;
    while (pos < input.size())
    {
        if (pos % static_cast<std::size_t>(changeEvery) == 0)
            change(static_cast<int>(pos / static_cast<std::size_t>(changeEvery)));
        const auto toChange = static_cast<std::size_t>(changeEvery) - pos % static_cast<std::size_t>(changeEvery);
        const int n = static_cast<int>(std::min({static_cast<std::size_t>(blockSize), toChange, input.size() - pos}));
        buffer.setSize(buffer.getNumChannels(), n, false, false, true);
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, i, input[pos + static_cast<std::size_t>(i)]);
        p.processBlock(buffer, midi);
        for (int i = 0; i < n; ++i)
            out.push_back(buffer.getSample(0, i));
        pos += static_cast<std::size_t>(n);
    }
    return out;
}

/// Dos campanas: la 1 a 1 kHz y la 2 a 4 kHz, con las ganancias dadas.
void twoBells(PluginProcessor& p, float gain1, float gain2)
{
    using namespace core;
    drag(p, bandParameter(0, BandParam::Frequency), {1000.0f});
    drag(p, kGain1, {gain1});
    drag(p, bandParameter(0, BandParam::Enabled), {1.0f});
    drag(p, bandParameter(1, BandParam::Frequency), {4000.0f});
    drag(p, kGain2, {gain2});
    drag(p, bandParameter(1, BandParam::Enabled), {1.0f});
}

/// La macro 1 mueve las ganancias de las dos campanas con `depth1` y `depth2` %.
core::MacroAssignments macro1On(float depth1, float depth2)
{
    core::MacroAssignments macros;
    macros.assign(0, kGain1, depth1);
    macros.assign(0, kGain2, depth2);
    return macros;
}

/// Una forma que no se repite: el valor del cambio `n` de una macro o un clip.
float wave(int n, double hz, double scale, double offset)
{
    return static_cast<float>(offset + scale * std::sin(2.0 * std::numbers::pi * hz * n * 0.01));
}

} // namespace

TEST_CASE("Macros en el plugin: ocho parametros del host, en 0")
{
    auto p = makeProcessor();
    for (int macro = 0; macro < core::kNumMacros; ++macro)
    {
        auto& parameter = p->parameter(core::macroParameter(macro));
        CHECK(parameter.getName(64) == "Macro " + juce::String(macro + 1));
        CHECK(parameter.getValue() == 0.0f);
        CHECK(parameter.getText(parameter.getValue(), 16) == "0.0%");
    }
    CHECK(p->macroAssignments().empty());
}

TEST_CASE("Macros en el plugin: al 100 % con +20 % una ganancia de -12 dB suena a 0 dB, y el host no cambia")
{
    // Criterio de PLAN.md 5.9 (con la profundidad corregida en el 7.2): suena
    // bit a bit como la misma banda en 0 dB; el host sigue viendo -12 dB.
    const auto input = noise(48000);
    auto modulated = makeProcessor();
    twoBells(*modulated, -12.0f, 0.0f);
    core::MacroAssignments macros;
    REQUIRE(macros.assign(0, kGain1, 20.0f));
    modulated->setMacroAssignments(macros);
    drag(*modulated, kMacro1, {100.0f});

    auto direct = makeProcessor();
    twoBells(*direct, 0.0f, 0.0f);

    prepare(*modulated);
    prepare(*direct);
    const auto a = render(*modulated, input, 256, 4096, [](int) {});
    const auto b = render(*direct, input, 256, 4096, [](int) {});
    CHECK(a == b);
    CHECK(modulated->currentValue(kGain1) == -12.0f);
    CHECK(modulated->parameter(kGain1).getText(modulated->parameter(kGain1).getValue(), 16) == "-12.0 dB");
    CHECK(modulated->soundingValues()[kGain1] == 0.0f);
    CHECK(modulated->currentValues()[kGain1] == -12.0f);
}

TEST_CASE("Macros en el plugin: dos destinos y un automation clip se suman y se limitan")
{
    // Criterio de PLAN.md 5.9: la macro 1 mueve las dos ganancias, y un clip
    // mueve la base de la primera, las dos sin gestos y en cada cambio. Suena
    // bit a bit como poner directamente lo que suena (core::applyMacros), con
    // la primera ganancia limitada a +30 dB en las crestas.
    const auto input = noise(96000);
    const auto macros = macro1On(40.0f, -30.0f);
    auto modulated = makeProcessor();
    twoBells(*modulated, 0.0f, 0.0f);
    modulated->setMacroAssignments(macros);
    auto direct = makeProcessor();
    twoBells(*direct, 0.0f, 0.0f);
    prepare(*modulated);
    prepare(*direct);

    bool clamped = false;
    const auto a = render(*modulated, input, 256, 1024,
                          [&](int n)
                          {
                              automate(*modulated, kMacro1, wave(n, 3.0, 50.0, 50.0));
                              automate(*modulated, kGain1, wave(n, 1.3, 18.0, 6.0));
                          });
    const auto b = render(*direct, input, 256, 1024,
                          [&](int n)
                          {
                              auto values = core::defaultValues();
                              values[kMacro1] = modulated->parameter(kMacro1).convertFrom0to1(
                                  modulated->parameter(kMacro1).convertTo0to1(wave(n, 3.0, 50.0, 50.0)));
                              values[kGain1] = modulated->parameter(kGain1).convertFrom0to1(
                                  modulated->parameter(kGain1).convertTo0to1(wave(n, 1.3, 18.0, 6.0)));
                              values[kGain2] = 0.0f;
                              core::applyMacros(macros, values);
                              clamped = clamped || values[kGain1] == 30.0f;
                              automate(*direct, kGain1, values[kGain1]);
                              automate(*direct, kGain2, values[kGain2]);
                          });
    CHECK(clamped);
    CHECK(a == b);
}

TEST_CASE("Macros en el plugin: sin asignaciones, mover las macros no cambia nada, bit a bit")
{
    const auto input = noise(48000);
    auto moving = makeProcessor();
    twoBells(*moving, -6.0f, 3.0f);
    auto still = makeProcessor();
    twoBells(*still, -6.0f, 3.0f);
    prepare(*moving);
    prepare(*still);
    const auto a = render(*moving, input, 256, 512,
                          [&](int n)
                          {
                              for (int macro = 0; macro < core::kNumMacros; ++macro)
                                  automate(*moving, core::macroParameter(macro), wave(n + macro, 2.0, 50.0, 50.0));
                          });
    const auto b = render(*still, input, 256, 512, [](int) {});
    CHECK(a == b);
}

TEST_CASE("Macros en el plugin: bloques de 1 a 4096 muestras, bit a bit, con las macros moviendose")
{
    // Criterio de PLAN.md 5.9: con dos macros moviéndose (y una asignación a
    // la ganancia de salida), la salida no depende del tamaño de bloque si los
    // cambios caen en las mismas muestras.
    const auto input = noise(3 * 8192);
    auto macros = macro1On(30.0f, -50.0f);
    REQUIRE(macros.assign(1, core::kOutputGain, 25.0f));
    REQUIRE(macros.assign(1, core::bandParameter(0, core::BandParam::Frequency), 10.0f));
    std::vector<float> reference;
    for (const int block : {4096, 1, 2, 3, 64, 100, 256, 1000, 2048})
    {
        INFO("bloque " << block);
        auto p = makeProcessor();
        twoBells(*p, -3.0f, 2.0f);
        p->setMacroAssignments(macros);
        prepare(*p, block);
        const auto out = render(*p, input, block, 4096,
                                [&](int n)
                                {
                                    automate(*p, kMacro1, wave(n, 7.0, 50.0, 50.0));
                                    automate(*p, core::macroParameter(1), wave(n, 11.0, 40.0, 50.0));
                                });
        if (reference.empty())
            reference = out;
        else
            CHECK(out == reference);
    }
}

TEST_CASE("Macros en el plugin: en fase lineal, la modulacion sigue el ritmo del control al renderizar")
{
    // PLAN.md 5.9 y ADR 0028: la modulación de una macro llega al diseño del
    // FIR como la de un automation clip, al ritmo del control (como mucho un
    // diseño por periodo, contado en muestras): suena bit a bit como mover el
    // parámetro directamente, y dos renders son iguales.
    const auto input = noise(48000);
    const auto macros = macro1On(25.0f, 0.0f);
    for (const float mode : {1.0f, 3.0f})
    {
        INFO("modo " << mode);
        std::vector<float> outputs[3];
        for (int run = 0; run < 3; ++run)
        {
            const bool viaMacro = run < 2;
            auto p = makeProcessor();
            p->setNonRealtime(true);
            twoBells(*p, 0.0f, 0.0f);
            drag(*p, core::kPhaseMode, {mode});
            if (viaMacro)
                p->setMacroAssignments(macros);
            prepare(*p);
            outputs[run] = render(*p, input, 256, 256,
                                  [&](int n)
                                  {
                                      const float macro = wave(n, 2.0 / 1.875, 50.0, 50.0);
                                      if (viaMacro)
                                      {
                                          automate(*p, kMacro1, macro);
                                          return;
                                      }
                                      auto values = core::defaultValues();
                                      auto& knob = p->parameter(kMacro1);
                                      values[kMacro1] = knob.convertFrom0to1(knob.convertTo0to1(macro));
                                      values[kGain1] = 0.0f;
                                      core::applyMacros(macros, values);
                                      automate(*p, kGain1, values[kGain1]);
                                  });
        }
        CHECK(outputs[0] == outputs[1]);
        CHECK(outputs[0] == outputs[2]);
    }
}

TEST_CASE("Macros en el plugin: las asignaciones viajan con el estado del proyecto")
{
    auto a = makeProcessor();
    twoBells(*a, -12.0f, 0.0f);
    const auto macros = macro1On(20.0f, -10.0f);
    a->setMacroAssignments(macros);
    drag(*a, kMacro1, {100.0f});
    juce::MemoryBlock state;
    a->getStateInformation(state);
    const auto text = state.toString();
    CHECK(text.startsWith("esceQ-state 4\n"));
    CHECK(text.contains("\nmacro.1.1 b01_gain 20\n"));
    CHECK(text.contains("\nmacro.1.2 b02_gain -10\n"));

    auto b = makeProcessor();
    b->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(b->macroAssignments() == macros);
    CHECK(b->soundingValues()[kGain1] == 0.0f);
    CHECK(b->soundingValues()[kGain2] == -6.0f);
    CHECK_FALSE(b->history().canUndo());

    // Suena como el original.
    const auto input = noise(24000);
    prepare(*a);
    prepare(*b);
    CHECK(render(*a, input, 512, 4096, [](int) {}) == render(*b, input, 512, 4096, [](int) {}));

    // Un estado v3 (de la 0.5) quita las asignaciones.
    const std::string v3 = "esceQ-state 3\nb01_on 1\n";
    b->setStateInformation(v3.data(), static_cast<int>(v3.size()));
    CHECK(b->macroAssignments().empty());
}

TEST_CASE("Macros en el plugin: cambiar las asignaciones es una accion que se deshace")
{
    auto p = makeProcessor();
    drag(*p, kGain1, {-3.0f});
    settle();
    const auto macros = macro1On(50.0f, 0.0f);
    p->changeMacroAssignments(macros);
    CHECK(p->macroAssignments() == macros);
    REQUIRE(p->history().canUndo());

    p->history().undo();
    CHECK(p->macroAssignments().empty());
    CHECK(p->currentValue(kGain1) == -3.0f); // solo deshace la asignación
    p->history().redo();
    CHECK(p->macroAssignments() == macros);

    // Las mismas que ya hay no son una acción.
    p->changeMacroAssignments(macros);
    p->history().undo();
    CHECK(p->macroAssignments().empty());
    p->history().undo();
    CHECK(p->currentValue(kGain1) == 0.0f);
    CHECK_FALSE(p->history().canUndo());
}

TEST_CASE("Macros en el plugin: el host moviendo una macro no crea acciones ni se deshace")
{
    auto p = makeProcessor();
    p->changeMacroAssignments(macro1On(50.0f, 0.0f));
    automate(*p, kMacro1, 80.0f);
    pump(EditHistory::kCoalesceMs + 150);
    p->history().undo(); // deshace la asignación, no la macro
    CHECK(p->macroAssignments().empty());
    CHECK(p->currentValue(kMacro1) == 80.0f);
    CHECK_FALSE(p->history().canUndo());
}

TEST_CASE("Macros en el plugin: los presets de usuario guardan las asignaciones, y cargar un preset las cambia")
{
    // Decidido con el usuario en el bloque 7.2: cargar un preset sustituye las
    // asignaciones por las suyas, también si no trae ninguna.
    PresetManager::userDirectory().deleteRecursively();
    auto p = makeProcessor();
    twoBells(*p, -12.0f, 0.0f);
    const auto macros = macro1On(20.0f, -10.0f);
    p->changeMacroAssignments(macros);
    drag(*p, kMacro1, {60.0f});
    settle();
    REQUIRE(p->presets().saveUser("Con macros"));
    const auto file = PresetManager::userDirectory().getChildFile("Con macros.esceq");
    CHECK(file.loadFileAsString().contains("\nmacro.1.1 b01_gain 20\n"));
    CHECK(file.loadFileAsString().contains("\nmacro_1 60\n"));
    CHECK_FALSE(p->presets().isModified());

    // Cambiar una asignación lo marca como modificado.
    auto other = macros;
    REQUIRE(other.assign(0, kGain1, 30.0f));
    p->changeMacroAssignments(other);
    CHECK(p->presets().isModified());
    p->history().undo();
    CHECK_FALSE(p->presets().isModified());

    // Uno de fábrica las quita, en la misma acción que sus parámetros.
    REQUIRE(p->presets().load({"Vocal", true}));
    CHECK(p->macroAssignments().empty());
    CHECK(p->currentValue(kMacro1) == 0.0f);
    p->history().undo();
    CHECK(p->macroAssignments() == macros);
    CHECK(p->currentValue(kMacro1) == 60.0f);
    p->history().redo();
    CHECK(p->macroAssignments().empty());

    // El de usuario las devuelve.
    REQUIRE(p->presets().load({"Con macros", false}));
    CHECK(p->macroAssignments() == macros);
    CHECK(p->currentValue(kMacro1) == 60.0f);
    CHECK(std::abs(p->soundingValues()[kGain1] - (-12.0f + 0.6f * 12.0f)) < 1e-4f);

    // Y volver a los valores por defecto, también.
    p->presets().loadDefault();
    CHECK(p->macroAssignments().empty());
    p->history().undo();
    CHECK(p->macroAssignments() == macros);
    PresetManager::userDirectory().deleteRecursively();
}
