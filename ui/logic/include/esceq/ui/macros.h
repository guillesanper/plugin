// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/macros.h"
#include "esceq/core/parameters.h"
#include "esceq/ui/control_logic.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Las macros en la interfaz (PLAN.md 5.9, bloque 7.3), sin dependencias
/// gráficas: los textos de la pestaña Macros del cajón, el menú Link to Macro
/// de los potenciómetros y de los nodos, y el anillo de modulación. Las
/// asignaciones y la suma son de core/macros.h; aquí solo se decide qué se
/// ve y qué asignaciones salen de cada elección del menú.
namespace esceq::ui {

/// Título de la pestaña del cajón y del menú.
inline constexpr std::string_view kMacrosTitle = "Macros";
inline constexpr std::string_view kLinkToMacroTitle = "Link to Macro";

/// Nombre de una macro (0..kNumMacros-1): "Macro 1"… Es el del parámetro.
std::string macroName(int macro);

/// Profundidades que ofrece el menú, en %, de más a menos. "Other..." deja
/// escribir cualquiera de −100 a +100.
inline constexpr std::array<float, 8> kMacroDepthChoices{100.0f, 50.0f, 25.0f, 10.0f, -10.0f, -25.0f, -50.0f, -100.0f};

/// Una profundidad como se escribe en los menús y las ayudas: "+25%",
/// "-12.5%", "0%" (sin decimales si es entera, con uno si no).
std::string depthText(float depth);

/// Lo que se escribe en "Other...": "25", "+25%", "-12.5 %". Limitada a
/// ±kMaxMacroDepth; nullopt si no es un número.
std::optional<float> parseDepth(std::string_view text);

/// Lo que se puede hacer con un destino desde el menú de una macro: lo que
/// se ve y si se puede elegir.
struct MacroLinkEntry
{
    int macro = 0;
    std::optional<float> depth; ///< Si la macro ya mueve el destino, con qué profundidad.
    bool full = false;          ///< La macro ya tiene kMaxMacroTargets destinos y no mueve este.
    std::string text;           ///< "Macro 2", "Macro 2 (+25%)", "Macro 3 (full)".
};

/// Las ocho entradas del menú Link to Macro de `parameter`. Vacío si no es un
/// destino válido (core::isMacroTarget): los interruptores, las listas y las
/// macros no ofrecen el menú.
std::vector<MacroLinkEntry> macroLinkEntries(const core::MacroAssignments& macros, std::size_t parameter);

/// Texto del submenú de un destino en el menú del nodo: "Gain", y si alguna
/// macro lo mueve, "Gain (Macro 2 +25%, Macro 5 -10%)".
std::string linkedTargetText(const core::MacroAssignments& macros, std::size_t parameter, std::string_view label);

/// Las asignaciones tras elegir una profundidad para `parameter` en la macro
/// `macro` (lo asigna o le cambia la profundidad). nullopt si no se puede:
/// el destino no es válido o la macro está llena.
std::optional<core::MacroAssignments> withLink(core::MacroAssignments macros, int macro, std::size_t parameter,
                                               float depth) noexcept;

/// Las asignaciones sin `parameter` en la macro `macro`, o sin ningún
/// destino en ella con `clearMacro`.
core::MacroAssignments withoutLink(core::MacroAssignments macros, int macro, std::size_t parameter) noexcept;
core::MacroAssignments clearMacro(core::MacroAssignments macros, int macro) noexcept;

/// Los destinos del menú de un nodo de la curva, en este orden: la
/// frecuencia siempre, la ganancia si el tipo de filtro la usa y el Q si lo
/// usa (core::usesGain, core::usesQ). Los tres son destinos válidos.
std::vector<std::size_t> nodeMacroTargets(const core::ParameterValues& values, int band);

/// Si alguna macro mueve el parámetro.
bool isLinked(const core::MacroAssignments& macros, std::size_t parameter) noexcept;

/// El anillo de modulación de un potenciómetro: de la posición de su valor
/// (el del host) a la de lo que suena, en los ángulos de ValueArc. Si las dos
/// coinciden (la macro en 0), el anillo es un punto en el valor.
struct ModulationArc
{
    float from = 0.0f; ///< Ángulo del valor de base.
    float to = 0.0f;   ///< Ángulo de lo que suena.
};
ModulationArc modulationArc(const core::ParameterSpec& spec, float base, float sounding) noexcept;

/// Si lo que suena de la banda difiere de su ajuste (frecuencia, ganancia o
/// Q): la curva la dibuja donde suena y marca el nodo, que se queda en el de
/// base.
bool bandModulated(const core::ParameterValues& base, const core::ParameterValues& sounding, int band) noexcept;

/// Si una macro tiene destinos: sin ellos, su potenciómetro se atenúa (no
/// influye, pero se puede mover).
bool macroHasTargets(const core::MacroAssignments& macros, int macro) noexcept;

/// Ayuda del potenciómetro de una macro: qué mueve y con qué profundidad
/// ("Macro 1 · moves Band 3 Gain +25%, Band 2 Freq -10% · ..."), o cómo
/// se asigna si no mueve nada.
std::string macroTooltip(const core::MacroAssignments& macros, int macro);

/// Si las macros cambian ahora el sonido: alguna con destinos y un valor
/// distinto de 0. Plegado el cajón, la pestaña lo marca con el punto.
bool macrosActive(const core::MacroAssignments& macros, const core::ParameterValues& values) noexcept;

/// Lo que añade la ayuda de la pestaña plegada si las macros cambian el
/// sonido: "Macros: 3 links moving". Vacío si no.
std::string macrosSummary(const core::MacroAssignments& macros, const core::ParameterValues& values);

} // namespace esceq::ui
