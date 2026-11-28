// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Controls.h"

#include "esceq/core/macros.h"
#include "esceq/core/parameters.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cstddef>
#include <vector>

namespace esceq {
class PluginProcessor;
}

namespace esceq::ui {

/// Las macros en los controles (PLAN.md 5.9, bloque 7.3, ADR 0029): el menú
/// Link to Macro de los potenciómetros y de los nodos, el de los
/// potenciómetros de las macros, y el anillo de modulación.
///
/// Cada elección del menú es una acción de deshacer
/// (PluginProcessor::changeMacroAssignments). Las asignaciones y lo que suena
/// se leen del procesador en refresh(), que llama el temporizador del
/// editor, así que también se ven las que cambian al deshacer o al cargar un
/// preset. Lo que sale de cada elección lo decide esceq/ui/macros.h.
///
/// Vive en el editor, que la crea antes que los controles que la usan.
class MacroLinks
{
public:
    /// `owner`: el editor. Da el aspecto a los menús y al diálogo, y si
    /// desaparece, lo que quede abierto no hace nada.
    MacroLinks(PluginProcessor& processor, juce::Component& owner);
    ~MacroLinks();

    /// Un potenciómetro de un parámetro del host: el clic derecho abre Link to
    /// Macro si es un destino válido, y se le pone el anillo. El control debe
    /// vivir menos que esto (los controles son del editor).
    void add(ParamControl& control);

    /// El potenciómetro de la macro `macro`: el clic derecho abre su menú (sus
    /// destinos, con la profundidad, y quitarlos).
    void addMacro(ParamControl& control, int macro);

    /// Lee las asignaciones y lo que suena, y pone al día los anillos.
    void refresh();

    const core::MacroAssignments& assignments() const noexcept { return macros_; }

    /// Añade a `menu` las ocho macros como submenús para `parameter` (con las
    /// profundidades, "Other..." y "Remove link"). Nada si no es un destino válido.
    void addLinkMenu(juce::PopupMenu& menu, std::size_t parameter);

    /// El menú del nodo de la banda `band`: un submenú por destino del nodo
    /// (frecuencia, y ganancia y Q si el tipo los usa), tras una cabecera.
    void addNodeMenu(juce::PopupMenu& menu, const core::ParameterValues& values, int band);

private:
    void showLinkMenu(ParamControl& control);
    void showMacroMenu(ParamControl& control, int macro);
    juce::PopupMenu depthMenu(int macro, std::size_t parameter, std::optional<float> depth);
    void askDepth(int macro, std::size_t parameter, std::optional<float> depth);
    void apply(const core::MacroAssignments& macros);
    void updateRings();

    PluginProcessor& processor_;
    juce::Component& owner_;
    std::vector<ParamControl*> controls_;
    std::array<ParamControl*, core::kNumMacros> macroControls_{};
    core::MacroAssignments macros_;
    core::ParameterValues sounding_{};
    juce::Component::SafePointer<juce::AlertWindow> depthDialog_; ///< Abierto con "Other...".

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroLinks)
};

} // namespace esceq::ui
