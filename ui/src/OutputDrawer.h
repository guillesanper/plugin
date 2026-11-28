// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Controls.h"

#include "esceq/core/macros.h"
#include "esceq/ui/layout.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace esceq {
class PluginProcessor;
}

namespace esceq::ui {

/// Cajón de la etapa de salida (PLAN.md 5.8, ADR 0027), entre la curva y el
/// medidor, con dos pestañas en la cabecera (ADR 0029). Output, en el orden
/// de la señal: la saturación (Off/Soft/Warm, Drive y Mix) arriba, y la
/// ganancia de salida y la compensación automática (Auto) abajo, junto al
/// medidor. Macros: los ocho potenciómetros de las macros (PLAN.md 5.9). Se
/// pliega en una pestaña, que marca con un punto si algo de dentro cambia el
/// sonido.
///
/// Todos los controles se enlazan a parámetros del host. La disposición sale
/// de outputDrawerLayout y los textos de esceq/ui/output_stage.h y
/// esceq/ui/macros.h (testeados sin ventana); aquí solo se pinta.
class OutputDrawer final : public juce::Component
{
public:
    explicit OutputDrawer(PluginProcessor& processor);
    ~OutputDrawer() override;

    /// El usuario pide abrirlo o plegarlo; quien lo contiene decide (y lo guarda).
    std::function<void()> onToggle;
    /// El usuario elige una pestaña (true: Macros); quien lo contiene la guarda.
    std::function<void(bool)> onPageChanged;

    void setOpen(bool open);
    bool isOpen() const noexcept { return open_; }

    /// La pestaña que se ve: Output (false) o Macros (true). No avisa.
    void setMacrosPage(bool macros);
    bool isMacrosPage() const noexcept { return macrosPage_; }

    /// Los potenciómetros de la saturación y la salida (Link to Macro) y los
    /// de las macros, para MacroLinks.
    std::vector<ParamControl*> targetKnobs() noexcept { return {&drive_, &mix_, &gain_}; }
    ParamControl& macroKnob(int macro) noexcept { return *macros_[static_cast<std::size_t>(macro)]; }

    /// Atenúa drive y mezcla con Off, y las macros sin destinos, y pone al
    /// día las ayudas (la latencia depende del modo de fase y de la
    /// frecuencia de muestreo) y la pestaña. La llama el temporizador del
    /// editor, con las asignaciones de las macros.
    void refresh(const core::MacroAssignments& macros);

    /// Texto y ayuda del botón Auto: los calcula el editor (dsp::autoGainDb).
    void setAutoGainText(const juce::String& label, const juce::String& tooltip);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Toggle;
    class Tab;
    void updateVisibility();

    PluginProcessor& processor_;
    bool open_ = true;
    bool macrosPage_ = false;
    OutputDrawerLayout layout_{};
    core::MacroAssignments macroAssignments_;

    std::unique_ptr<Toggle> toggle_;
    std::unique_ptr<Tab> outputTab_;
    std::unique_ptr<Tab> macrosTab_;
    std::array<std::unique_ptr<ParamControl>, core::kNumMacros> macros_;
    Segmented mode_;
    ParamControl drive_{ParamControl::Style::Knob, "Drive"};
    ParamControl mix_{ParamControl::Style::Knob, "Mix"};
    ParamControl gain_{ParamControl::Style::Knob, "Gain"};
    ToggleSwitch autoGain_{ToggleSwitch::Style::Pill, "Auto", "Auto gain"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputDrawer)
};

} // namespace esceq::ui
