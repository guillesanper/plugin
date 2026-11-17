// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/options_menu.h"

#include "esceq/core/state.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace esceq;
using namespace esceq::ui;

TEST_CASE("Opciones: una entrada por algoritmo de filtro, con el que suena marcado")
{
    for (const auto current : {dsp::FilterAlgorithm::Classic, dsp::FilterAlgorithm::Matched})
    {
        const auto items = filterAlgorithmItems(current);
        REQUIRE(items.size() == static_cast<std::size_t>(dsp::kNumFilterAlgorithms));
        CHECK(items[0].algorithm == dsp::FilterAlgorithm::Classic);
        CHECK(items[0].label == "Classic (as in esceQ 0.1)");
        CHECK(items[1].algorithm == dsp::FilterAlgorithm::Matched);
        CHECK(items[1].label == "Matched (analog-accurate highs)");

        std::set<int> ids;
        int ticked = 0;
        for (const auto& item : items)
        {
            CHECK(item.menuId > 0);
            ids.insert(item.menuId);
            CHECK(item.ticked == (item.algorithm == current));
            ticked += item.ticked ? 1 : 0;
            // Elegir la entrada lleva a su algoritmo.
            CHECK(filterAlgorithmForMenuId(item.menuId) == item.algorithm);
        }
        CHECK(ids.size() == items.size());
        CHECK(ticked == 1);
    }
}

TEST_CASE("Opciones: lo que no es una entrada de algoritmo no cambia nada")
{
    // 0: el menu se cerro sin elegir. Los demas: otras entradas o basura.
    for (const int id : {0, -1, 1, 99, 102, 1000})
        CHECK_FALSE(filterAlgorithmForMenuId(id).has_value());
}

TEST_CASE("Opciones: la ayuda dice el algoritmo que suena")
{
    CHECK(optionsTooltip(dsp::FilterAlgorithm::Matched) == "Options \xC2\xB7 HF accuracy: Matched");
    CHECK(optionsTooltip(dsp::FilterAlgorithm::Classic) == "Options \xC2\xB7 HF accuracy: Classic");
}

TEST_CASE("Opciones: una entrada por tamano de ventana, con el de la ventana marcado")
{
    for (const auto current : {std::optional<SizePreset>{}, std::optional{SizePreset::Small},
                               std::optional{SizePreset::Medium}, std::optional{SizePreset::Large}})
    {
        const auto items = windowSizeItems(current);
        REQUIRE(items.size() == static_cast<std::size_t>(kNumSizePresets));
        CHECK(items[0].label == "Small (1120 \xC3\x97 580)");
        CHECK(items[1].label == "Medium (1180 \xC3\x97 700)");
        CHECK(items[2].label == "Large (1480 \xC3\x97 860)");

        std::set<int> ids;
        int ticked = 0;
        for (const auto& item : items)
        {
            CHECK(item.menuId > 0);
            ids.insert(item.menuId);
            CHECK(item.ticked == (current == item.preset));
            ticked += item.ticked ? 1 : 0;
            CHECK(windowSizeForMenuId(item.menuId) == item.preset);
            // No se confunden con las de los algoritmos.
            CHECK_FALSE(filterAlgorithmForMenuId(item.menuId).has_value());
        }
        for (const auto& item : filterAlgorithmItems(dsp::FilterAlgorithm::Matched))
            CHECK_FALSE(windowSizeForMenuId(item.menuId).has_value());
        CHECK(ids.size() == items.size());
        CHECK(ticked == (current ? 1 : 0));
    }
    for (const int id : {0, -1, 1, 199, 203, 1000})
        CHECK_FALSE(windowSizeForMenuId(id).has_value());
}

TEST_CASE("Opciones: el teclado de notas se muestra y se oculta")
{
    for (const bool shown : {false, true})
    {
        const auto item = pianoKeyboardItem(shown);
        CHECK(item.label == "Piano keyboard");
        CHECK(item.ticked == shown);
        CHECK(item.menuId > 0);
        CHECK(isPianoKeyboardMenuId(item.menuId));
        // No se confunde con las demas entradas.
        CHECK_FALSE(filterAlgorithmForMenuId(item.menuId).has_value());
        CHECK_FALSE(windowSizeForMenuId(item.menuId).has_value());
        CHECK_FALSE(tuningForMenuId(item.menuId).has_value());
        CHECK_FALSE(isCustomTuningMenuId(item.menuId));
    }
    for (const int id : {0, -1, 100, 200, 1000})
        CHECK_FALSE(isPianoKeyboardMenuId(id));
}

TEST_CASE("Opciones: afinaciones habituales y otra a mano, con la actual marcada")
{
    CHECK(tuningMenuTitle(440.0) == "Tuning: A4 = 440.0 Hz");
    CHECK(tuningMenuTitle(438.5) == "Tuning: A4 = 438.5 Hz");

    const auto standard = tuningItems(440.0);
    REQUIRE(standard.size() == 7);
    CHECK(standard[0].label == "415 Hz (baroque)");
    CHECK(standard[1].label == "432 Hz");
    CHECK(standard[2].label == "440 Hz (standard)");
    CHECK(standard[3].label == "442 Hz");
    CHECK(standard[6].label == "Other...");
    CHECK_FALSE(standard[6].a4Hz.has_value());

    std::set<int> ids{pianoKeyboardItem(false).menuId};
    int ticked = 0;
    for (const auto& item : standard)
    {
        CHECK(item.menuId > 0);
        ids.insert(item.menuId);
        ticked += item.ticked ? 1 : 0;
        if (item.a4Hz)
        {
            CHECK(tuningForMenuId(item.menuId) == item.a4Hz);
            CHECK_FALSE(isCustomTuningMenuId(item.menuId));
            CHECK(item.ticked == (*item.a4Hz == 440.0));
            CHECK(*item.a4Hz >= esceq::core::kMinA4Hz);
            CHECK(*item.a4Hz <= esceq::core::kMaxA4Hz);
        }
        else
        {
            CHECK(isCustomTuningMenuId(item.menuId));
            CHECK_FALSE(tuningForMenuId(item.menuId).has_value());
        }
        CHECK_FALSE(filterAlgorithmForMenuId(item.menuId).has_value());
        CHECK_FALSE(windowSizeForMenuId(item.menuId).has_value());
    }
    CHECK(ids.size() == standard.size() + 1);
    CHECK(ticked == 1);

    // Una afinacion que no es de la lista: se marca "Other" y dice cual es.
    const auto custom = tuningItems(438.5);
    CHECK(custom.back().label == "Other (438.5 Hz)...");
    CHECK(custom.back().ticked);
    for (std::size_t i = 0; i + 1 < custom.size(); ++i)
        CHECK_FALSE(custom[i].ticked);

    for (const int id : {0, -1, 1, 100, 200, 1000})
    {
        CHECK_FALSE(tuningForMenuId(id).has_value());
        CHECK_FALSE(isCustomTuningMenuId(id));
    }
}

TEST_CASE("Opciones: ajustes del analizador, con los actuales marcados")
{
    // Por defecto, el analizador de siempre, el de las preferencias por defecto.
    const AnalyzerView view;
    const core::UiSettings ui;
    CHECK(view.fftSize == ui.analyzerFftSize);
    CHECK(view.fallMs == ui.analyzerFallMs);
    CHECK(view.tiltDbPerOctave == ui.analyzerTiltDbPerOctave);
    CHECK(view.peakHold == ui.analyzerPeakHold);
    CHECK_FALSE(view.frozen);

    const auto submenus = analyzerSubmenus(view);
    REQUIRE(submenus.size() == 3);
    CHECK(submenus[0].title == "Resolution: 4096 points");
    CHECK(submenus[1].title == "Fall: Medium (0.35 s)");
    CHECK(submenus[2].title == "Tilt: 4.5 dB/oct");
    REQUIRE(submenus[0].items.size() == 3);
    CHECK(submenus[0].items[0].label == "4096 points");
    CHECK(submenus[0].items[1].label == "8192 points");
    CHECK(submenus[0].items[2].label == "16384 points");
    REQUIRE(submenus[1].items.size() == 3);
    CHECK(submenus[1].items[0].label == "Slow (1 s)");
    CHECK(submenus[1].items[1].label == "Medium (0.35 s)");
    CHECK(submenus[1].items[2].label == "Fast (0.12 s)");
    REQUIRE(submenus[2].items.size() == 4);
    CHECK(submenus[2].items[0].label == "0 dB/oct (flat)");
    CHECK(submenus[2].items[1].label == "3 dB/oct");
    CHECK(submenus[2].items[2].label == "4.5 dB/oct");
    CHECK(submenus[2].items[3].label == "6 dB/oct");
    CHECK(peakHoldItem(false).label == "Peak hold");
    CHECK(freezeItem(false).label == "Freeze");
    CHECK(peakHoldItem(true).ticked);
    CHECK(freezeItem(true).ticked);

    // Una marcada en cada submenu, la actual; cada entrada lleva a su valor, y
    // ninguna se confunde con las de las demas secciones.
    std::set<int> ids{pianoKeyboardItem(false).menuId, peakHoldItem(false).menuId, freezeItem(false).menuId};
    for (const auto& item : tuningItems(440.0))
        ids.insert(item.menuId);
    std::size_t entries = ids.size();
    for (std::size_t s = 0; s < submenus.size(); ++s)
    {
        int ticked = 0;
        for (const auto& item : submenus[s].items)
        {
            INFO(item.label);
            CHECK(item.menuId > 0);
            ids.insert(item.menuId);
            ++entries;
            ticked += item.ticked ? 1 : 0;
            const auto chosen = analyzerViewForMenuId(view, item.menuId);
            REQUIRE(chosen.has_value());
            CHECK_FALSE(filterAlgorithmForMenuId(item.menuId).has_value());
            CHECK_FALSE(windowSizeForMenuId(item.menuId).has_value());
            CHECK_FALSE(tuningForMenuId(item.menuId).has_value());
            CHECK_FALSE(isPianoKeyboardMenuId(item.menuId));
            // Cambia solo su ajuste, y el resultado se marca en el submenu.
            const auto again = analyzerSubmenus(*chosen);
            for (std::size_t other = 0; other < submenus.size(); ++other)
                for (std::size_t i = 0; i < again[other].items.size(); ++i)
                    CHECK(again[other].items[i].ticked ==
                          (other == s ? again[other].items[i].menuId == item.menuId : submenus[other].items[i].ticked));
            CHECK(chosen->peakHold == view.peakHold);
            CHECK(chosen->frozen == view.frozen);
        }
        CHECK(ticked == 1);
    }
    CHECK(ids.size() == entries); // sin identificadores repetidos

    // Todos los valores que se ofrecen son los que el estado acepta.
    for (const int fft : core::kAnalyzerFftSizes)
    {
        AnalyzerView v;
        v.fftSize = fft;
        CHECK(analyzerSubmenus(v)[0].title == "Resolution: " + std::to_string(fft) + " points");
    }
    AnalyzerView chosen = view;
    chosen = *analyzerViewForMenuId(chosen, analyzerSubmenus(chosen)[0].items[2].menuId);
    CHECK(chosen.fftSize == 16384);
    chosen = *analyzerViewForMenuId(chosen, analyzerSubmenus(chosen)[1].items[0].menuId);
    CHECK(chosen.fallMs == 1000);
    chosen = *analyzerViewForMenuId(chosen, analyzerSubmenus(chosen)[2].items[0].menuId);
    CHECK(chosen.tiltDbPerOctave == 0.0);
    CHECK(analyzerSubmenus(chosen)[1].title == "Fall: Slow (1 s)");
    CHECK(analyzerSubmenus(chosen)[2].title == "Tilt: 0 dB/oct (flat)");

    // Los interruptores cambian de estado.
    chosen = *analyzerViewForMenuId(chosen, peakHoldItem(chosen.peakHold).menuId);
    CHECK(chosen.peakHold);
    chosen = *analyzerViewForMenuId(chosen, freezeItem(chosen.frozen).menuId);
    CHECK(chosen.frozen);
    chosen = *analyzerViewForMenuId(chosen, freezeItem(chosen.frozen).menuId);
    CHECK_FALSE(chosen.frozen);
    CHECK(chosen.fftSize == 16384);

    // Lo que no es una entrada del analizador no cambia nada.
    for (const int id : {0, -1, 100, 200, 300, 310, 330, 1000})
        CHECK_FALSE(analyzerViewForMenuId(view, id).has_value());
}
