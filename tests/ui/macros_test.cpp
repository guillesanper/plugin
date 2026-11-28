// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/macros.h"

#include "esceq/core/band_model.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>
#include <vector>

using namespace esceq;
using namespace esceq::ui;

namespace {

bool contains(const std::string& text, const std::string& part)
{
    return text.find(part) != std::string::npos;
}

std::size_t gainOf(int band)
{
    return core::bandParameter(band, core::BandParam::Gain);
}

std::size_t frequencyOf(int band)
{
    return core::bandParameter(band, core::BandParam::Frequency);
}

std::size_t qOf(int band)
{
    return core::bandParameter(band, core::BandParam::Q);
}

} // namespace

TEST_CASE("Macros en la interfaz: nombres y profundidades escritas")
{
    CHECK(macroName(0) == "Macro 1");
    CHECK(macroName(7) == "Macro 8");
    // El nombre es el del parametro, el que ve el host.
    for (int m = 0; m < core::kNumMacros; ++m)
        CHECK(macroName(m) == core::parameters()[core::macroParameter(m)].name);

    CHECK(depthText(25.0f) == "+25%");
    CHECK(depthText(-10.0f) == "-10%");
    CHECK(depthText(100.0f) == "+100%");
    CHECK(depthText(-12.5f) == "-12.5%");
    CHECK(depthText(0.0f) == "0%");
    CHECK(depthText(33.33f) == "+33.3%");

    CHECK(parseDepth("25") == 25.0f);
    CHECK(parseDepth("+25%") == 25.0f);
    CHECK(parseDepth(" -12.5 % ") == -12.5f);
    CHECK(parseDepth("150") == 100.0f); // limitada al rango
    CHECK(parseDepth("-300%") == -100.0f);
    CHECK(parseDepth("0") == 0.0f);
    for (const char* bad : {"", "abc", "%", "nan", "inf", "12abc", "--3", "1e999"})
    {
        INFO(bad);
        CHECK_FALSE(parseDepth(bad).has_value());
    }
    // Lo que se escribe se vuelve a leer igual.
    for (const float depth : kMacroDepthChoices)
        CHECK(parseDepth(depthText(depth)) == depth);
}

TEST_CASE("Macros en la interfaz: Link to Macro solo en los destinos validos")
{
    const core::MacroAssignments none;
    // Los continuos de las bandas, de la saturacion y la salida: ocho entradas, ninguna marcada.
    const std::initializer_list<std::size_t> targets{gainOf(2),
                                                     frequencyOf(0),
                                                     qOf(23),
                                                     core::dynamicParameter(4, core::DynParam::Range),
                                                     core::kSaturationDrive,
                                                     core::kOutputGain};
    for (const std::size_t index : targets)
    {
        INFO(core::parameters()[index].id);
        const auto entries = macroLinkEntries(none, index);
        REQUIRE(entries.size() == static_cast<std::size_t>(core::kNumMacros));
        for (int m = 0; m < core::kNumMacros; ++m)
        {
            CHECK(entries[static_cast<std::size_t>(m)].macro == m);
            CHECK_FALSE(entries[static_cast<std::size_t>(m)].depth.has_value());
            CHECK_FALSE(entries[static_cast<std::size_t>(m)].full);
            CHECK(entries[static_cast<std::size_t>(m)].text == macroName(m));
        }
    }
    // Los interruptores, las listas y las macros, ninguna.
    const std::initializer_list<std::size_t> others{core::bandParameter(0, core::BandParam::Type),
                                                    core::bandParameter(0, core::BandParam::Enabled),
                                                    core::bandParameter(3, core::BandParam::Slope),
                                                    core::dynamicParameter(0, core::DynParam::Enabled),
                                                    core::kPhaseMode,
                                                    core::kSaturationMode,
                                                    core::kBypass,
                                                    core::kAutoGain,
                                                    core::macroParameter(0),
                                                    core::kNumParameters};
    for (const std::size_t index : others)
    {
        INFO(index);
        CHECK(macroLinkEntries(none, index).empty());
    }
}

TEST_CASE("Macros en la interfaz: el menu marca las macros que ya lo mueven y las llenas")
{
    core::MacroAssignments macros;
    REQUIRE(macros.assign(1, gainOf(2), 25.0f));
    REQUIRE(macros.assign(4, gainOf(2), -10.0f));
    for (int b = 0; b < core::kMaxMacroTargets; ++b)
        REQUIRE(macros.assign(6, frequencyOf(b), 50.0f)); // la macro 7, llena

    const auto entries = macroLinkEntries(macros, gainOf(2));
    REQUIRE(entries.size() == 8u);
    CHECK(entries[1].depth == 25.0f);
    CHECK(entries[1].text == "Macro 2 (+25%)");
    CHECK(entries[4].depth == -10.0f);
    CHECK(entries[4].text == "Macro 5 (-10%)");
    CHECK(entries[6].full);
    CHECK(entries[6].text == "Macro 7 (full)");
    CHECK_FALSE(entries[0].depth.has_value());
    // Un destino de la macro llena no la ve llena: puede cambiar su profundidad o quitarse.
    const auto inFull = macroLinkEntries(macros, frequencyOf(3));
    CHECK_FALSE(inFull[6].full);
    CHECK(inFull[6].depth == 50.0f);

    CHECK(isLinked(macros, gainOf(2)));
    CHECK(isLinked(macros, frequencyOf(7)));
    CHECK_FALSE(isLinked(macros, gainOf(3)));

    CHECK(linkedTargetText(macros, gainOf(2), "Gain") == "Gain (Macro 2 +25%, Macro 5 -10%)");
    CHECK(linkedTargetText(macros, gainOf(3), "Gain") == "Gain");
}

TEST_CASE("Macros en la interfaz: elegir en el menu asigna, cambia la profundidad y quita")
{
    core::MacroAssignments macros;
    const auto linked = withLink(macros, 2, gainOf(5), 50.0f);
    REQUIRE(linked.has_value());
    CHECK(linked->depth(2, gainOf(5)) == 50.0f);

    // Otra profundidad: la cambia en el mismo hueco.
    const auto changed = withLink(*linked, 2, gainOf(5), -25.0f);
    REQUIRE(changed.has_value());
    CHECK(changed->depth(2, gainOf(5)) == -25.0f);
    CHECK(changed->targets(2)[0].parameter == gainOf(5));
    CHECK_FALSE(changed->targets(2)[1].used());

    // Quitar deja libre el hueco y no toca los demas.
    auto two = *changed;
    REQUIRE(two.assign(2, qOf(5), 10.0f));
    REQUIRE(two.assign(3, gainOf(5), 10.0f));
    const auto removed = withoutLink(two, 2, gainOf(5));
    CHECK_FALSE(removed.depth(2, gainOf(5)).has_value());
    CHECK(removed.depth(2, qOf(5)) == 10.0f);
    CHECK(removed.depth(3, gainOf(5)) == 10.0f);
    CHECK(withoutLink(removed, 2, gainOf(5)) == removed); // no lo movia: igual

    const auto cleared = clearMacro(two, 2);
    for (const auto& target : cleared.targets(2))
        CHECK_FALSE(target.used());
    CHECK(cleared.depth(3, gainOf(5)) == 10.0f);

    // Lo que no se puede: un destino no valido, una macro que no existe o llena.
    CHECK_FALSE(withLink(macros, 0, core::kBypass, 50.0f).has_value());
    CHECK_FALSE(withLink(macros, 8, gainOf(0), 50.0f).has_value());
    CHECK_FALSE(withLink(macros, -1, gainOf(0), 50.0f).has_value());
    core::MacroAssignments full;
    for (int b = 0; b < core::kMaxMacroTargets; ++b)
        REQUIRE(full.assign(0, gainOf(b), 50.0f));
    CHECK_FALSE(withLink(full, 0, gainOf(8), 50.0f).has_value());
    CHECK(withLink(full, 0, gainOf(7), 20.0f).has_value()); // ya estaba: cambia la profundidad
}

TEST_CASE("Macros en la interfaz: el nodo ofrece frecuencia, y ganancia y Q si el tipo los usa")
{
    auto values = core::defaultValues();
    auto setType = [&](int band, dsp::FilterType type, float slope) {
        values[core::bandParameter(band, core::BandParam::Type)] = static_cast<float>(type);
        values[core::bandParameter(band, core::BandParam::Slope)] = slope;
    };

    for (int type = 0; type < static_cast<int>(core::kFilterTypeChoices.size()); ++type)
        for (const float slope : {0.0f, 3.0f})
        {
            setType(4, static_cast<dsp::FilterType>(type), slope);
            const auto settings = core::bandSettings(values, 4);
            INFO("tipo " << type << ", pendiente " << slope);
            std::vector<std::size_t> expected{frequencyOf(4)};
            if (core::usesGain(settings.filter.type))
                expected.push_back(gainOf(4));
            if (core::usesQ(settings.filter.type, settings.filter.slopeDbPerOct))
                expected.push_back(qOf(4));
            CHECK(nodeMacroTargets(values, 4) == expected);
            for (const auto index : expected)
                CHECK(core::isMacroTarget(index));
        }

    setType(2, dsp::FilterType::Bell, 0.0f);
    CHECK(nodeMacroTargets(values, 2) == std::vector<std::size_t>{frequencyOf(2), gainOf(2), qOf(2)});
    setType(2, dsp::FilterType::Tilt, 0.0f);
    CHECK(nodeMacroTargets(values, 2) == std::vector<std::size_t>{frequencyOf(2), gainOf(2)});
}

TEST_CASE("Macros en la interfaz: el anillo va del valor de base a lo que suena")
{
    const auto& gain = core::parameters()[gainOf(0)];
    // -30..+30 dB: 0 dB en el centro (angulo 0), +30 en el final del recorrido.
    const auto up = modulationArc(gain, 0.0f, 30.0f);
    CHECK(up.from == Catch::Approx(0.0f).margin(1e-6));
    CHECK(up.to == Catch::Approx(kArcHalfSpan));
    const auto down = modulationArc(gain, 0.0f, -15.0f);
    CHECK(down.to == Catch::Approx(-0.5f * kArcHalfSpan));
    // Los angulos son los del indicador del potenciometro (ValueArc).
    CHECK(down.from == Catch::Approx(valueArc(gain, 0.0f).indicator));
    CHECK(down.to == Catch::Approx(valueArc(gain, -15.0f).indicator));
    // Sin modulacion, un punto en el valor.
    const auto still = modulationArc(gain, 6.0f, 6.0f);
    CHECK(still.from == still.to);

    // En la frecuencia, la misma escala logaritmica del potenciometro.
    const auto& frequency = core::parameters()[frequencyOf(0)];
    const auto f = modulationArc(frequency, 1000.0f, 4000.0f);
    CHECK(f.from == Catch::Approx(valueArc(frequency, 1000.0f).indicator));
    CHECK(f.to == Catch::Approx(valueArc(frequency, 4000.0f).indicator));
    CHECK(f.to > f.from);
}

TEST_CASE("Macros en la interfaz: la banda modulada, si suena distinta de su ajuste")
{
    const auto base = core::defaultValues();
    auto sounding = base;
    CHECK_FALSE(bandModulated(base, sounding, 3));
    sounding[gainOf(3)] = 4.0f;
    CHECK(bandModulated(base, sounding, 3));
    CHECK_FALSE(bandModulated(base, sounding, 2));
    sounding = base;
    sounding[frequencyOf(3)] = 2000.0f;
    CHECK(bandModulated(base, sounding, 3));
    sounding = base;
    sounding[qOf(3)] = 3.0f;
    CHECK(bandModulated(base, sounding, 3));
    // La dinamica no mueve el nodo.
    sounding = base;
    sounding[core::dynamicParameter(3, core::DynParam::Threshold)] = -40.0f;
    CHECK_FALSE(bandModulated(base, sounding, 3));
}

TEST_CASE("Macros en la interfaz: ayuda de cada macro y aviso de la pestana plegada")
{
    core::MacroAssignments macros;
    CHECK_FALSE(macroHasTargets(macros, 0));
    CHECK(contains(macroTooltip(macros, 0), "Macro 1"));
    CHECK(contains(macroTooltip(macros, 0), "Link to Macro")); // dice como se asigna

    REQUIRE(macros.assign(0, gainOf(2), 25.0f));
    REQUIRE(macros.assign(0, frequencyOf(1), -10.0f));
    CHECK(macroHasTargets(macros, 0));
    CHECK_FALSE(macroHasTargets(macros, 1));
    const auto tooltip = macroTooltip(macros, 0);
    CHECK(tooltip.rfind("Macro 1", 0) == 0);
    CHECK(contains(tooltip, "Band 3 Gain +25%"));
    CHECK(contains(tooltip, "Band 2 Freq -10%"));
    CHECK(tooltip.find("Band 3 Gain") < tooltip.find("Band 2 Freq")); // en el orden de los huecos

    // Solo cambian el sonido con destinos y un valor distinto de 0.
    auto values = core::defaultValues();
    CHECK_FALSE(macrosActive(macros, values));
    CHECK(macrosSummary(macros, values).empty());
    values[core::macroParameter(1)] = 50.0f; // la macro 2 no tiene destinos
    CHECK_FALSE(macrosActive(macros, values));
    values[core::macroParameter(0)] = 40.0f;
    CHECK(macrosActive(macros, values));
    CHECK(macrosSummary(macros, values) == "Macros: 2 links moving");
    REQUIRE(macros.assign(1, qOf(0), 50.0f));
    CHECK(macrosSummary(macros, values) == "Macros: 3 links moving");
    CHECK_FALSE(macrosActive(core::MacroAssignments{}, values));
}
