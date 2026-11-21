// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Controls.h"

#include "esceq/core/match.h"
#include "esceq/core/parameters.h"
#include "esceq/ui/layout.h"
#include "esceq/ui/match.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace esceq::ui {

/// Panel Match (igualar a una referencia, PLAN.md 5.6): una tarjeta sobre la
/// esquina superior derecha de la curva, con la referencia, Learn/Stop y los
/// segundos útiles, Amount, Smoothing, Max bands, Range, Apply y su estado.
///
/// Solo pinta y traduce gestos: el estado sale de ui::MatchController (lo
/// lee el editor en cada refresco y llama a update()), los textos de
/// esceq/ui/match.h y la disposición de matchPanelLayout. Lo que el usuario
/// cambia sale por los avisos on...; nada de esto son parámetros del host,
/// así que no se deshace (salvo Apply, que es una acción).
///
/// Teclado: Tab recorre los controles; Esc, con el foco en el panel, lo cierra.
class MatchPanel final : public juce::Component
{
public:
    MatchPanel();
    ~MatchPanel() override;

    std::function<void()> onClose;
    std::function<void(const core::MatchReference&)> onReference;
    /// Learn o Stop, según se esté aprendiendo.
    std::function<void()> onLearn;
    std::function<void(const core::MatchSettings&)> onSettings;
    std::function<void()> onApply;

    /// Pone el panel en `layout` (coordenadas del editor): la tarjeta es su tamaño.
    void setPanelLayout(const MatchPanelLayout& layout);

    /// Pone el panel al día. `proposedBands`: las de la propuesta actual (0 sin ella).
    void update(const MatchStatus& status, const core::MatchSettings& settings, int freeBands, int proposedBands);

    /// Afinación con la que se entiende una nota escrita en el rango.
    void setA4(double a4Hz);

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    /// `layout_` en coordenadas de la tarjeta.
    Rect local(const Rect& r) const noexcept;
    void sendSettings();

    MatchPanelLayout layout_{};

    IconButton close_{draw::Icon::Close, "Close Match"};
    Dropdown reference_{"Match reference"};
    ToggleSwitch learn_{ToggleSwitch::Style::Pill, "Learn", "Learn"};
    ParamControl amount_{ParamControl::Style::Bar, "Amount"};
    Segmented smoothing_;
    Dropdown bands_{"Max bands"};
    ParamControl low_{ParamControl::Style::Field, "Range low"};
    ParamControl high_{ParamControl::Style::Field, "Range high"};
    ToggleSwitch apply_{ToggleSwitch::Style::Pill, "Apply", "Apply"};

    std::vector<MatchReferenceOption> options_; ///< Las del desplegable, en su orden.
    int bandsLimit_ = 0;                        ///< Entradas de bands_.
    core::MatchSettings settings_{};
    MatchStatus status_{};
    MatchStatusLine statusLine_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatchPanel)
};

} // namespace esceq::ui
