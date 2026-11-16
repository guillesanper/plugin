// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/dsp/filter_design.h"
#include "esceq/ui/layout.h"

#include <optional>
#include <string>
#include <vector>

/// Menú de opciones de la barra superior, sin dependencias gráficas: qué
/// ofrece, qué marca y qué hace cada entrada. ui/src solo lo pinta.
namespace esceq::ui {

/// Título de la sección del algoritmo de filtro.
inline constexpr const char* kHfAccuracyHeading = "HF accuracy";

/// Una entrada de la sección "HF accuracy": un algoritmo de filtro.
struct FilterAlgorithmItem
{
    int menuId = 0; ///< Identificador de la entrada en el menú (> 0).
    dsp::FilterAlgorithm algorithm = dsp::FilterAlgorithm::Classic;
    std::string label;
    bool ticked = false; ///< El que suena.

    friend bool operator==(const FilterAlgorithmItem&, const FilterAlgorithmItem&) = default;
};

/// Las entradas de la sección "HF accuracy", una por algoritmo y en el orden
/// del enum, con `current` marcado.
std::vector<FilterAlgorithmItem> filterAlgorithmItems(dsp::FilterAlgorithm current);

/// Algoritmo de la entrada `menuId`, o nullopt si no es una de esas entradas
/// (otra sección, o el menú se cerró sin elegir: 0).
std::optional<dsp::FilterAlgorithm> filterAlgorithmForMenuId(int menuId) noexcept;

/// Título de la sección del tamaño de la ventana.
inline constexpr const char* kWindowSizeHeading = "Window size";

/// Una entrada de la sección "Window size": un tamaño predefinido.
struct WindowSizeItem
{
    int menuId = 0; ///< Identificador de la entrada en el menú (> 0).
    SizePreset preset = SizePreset::Medium;
    std::string label;   ///< "Medium (1180 × 700)".
    bool ticked = false; ///< El de la ventana, si coincide exactamente.

    friend bool operator==(const WindowSizeItem&, const WindowSizeItem&) = default;
};

/// Las entradas de la sección "Window size", una por tamaño y en el orden de
/// SizePreset, con `current` marcado (ninguno si la ventana tiene otro tamaño).
std::vector<WindowSizeItem> windowSizeItems(std::optional<SizePreset> current);

/// Tamaño de la entrada `menuId`, o nullopt si no es una de esas entradas.
std::optional<SizePreset> windowSizeForMenuId(int menuId) noexcept;

/// Ayuda del botón de opciones, con el algoritmo que suena:
/// "Options · HF accuracy: Matched".
std::string optionsTooltip(dsp::FilterAlgorithm current);

} // namespace esceq::ui
