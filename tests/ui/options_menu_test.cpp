// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/options_menu.h"

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
