// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/state.h"
#include "esceq/ui/notes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

namespace esceq::ui {

/// Teclado de la escala de notas (PLAN.md 5.1, ADR 0017): una franja de
/// teclas de piano bajo la curva, en su mismo eje de frecuencias. Cada do
/// lleva su octava. Al pasar el ratón por una tecla se ilumina y se avisa a
/// onHoveredNoteChanged, que la marca en la curva.
///
/// Tiene la misma x y el mismo ancho que la curva (esceq/ui/layout.h). Las
/// teclas y la tecla bajo el ratón salen de esceq/ui/notes.h; aquí solo se
/// pinta. Solo se repinta si cambia la afinación, el tamaño o la tecla señalada.
class PianoKeyboard final : public juce::Component
{
public:
    PianoKeyboard();

    /// Afinación: frecuencia del la central (A4), en Hz.
    void setA4(double a4Hz);

    /// Se llama cuando cambia la tecla bajo el ratón (nullopt: ninguna).
    std::function<void(std::optional<int>)> onHoveredNoteChanged;

    /// Tecla señalada (nullopt: ninguna). La fija el propio ratón; es pública
    /// para la herramienta de capturas.
    void setHoveredNote(std::optional<int> midi);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    void updateKeys();

    double a4Hz_ = core::kDefaultA4Hz;
    std::vector<PianoKey> keys_;
    std::optional<int> hovered_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoKeyboard)
};

} // namespace esceq::ui
