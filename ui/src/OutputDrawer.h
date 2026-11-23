// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Controls.h"

#include "esceq/ui/layout.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace esceq {
class PluginProcessor;
}

namespace esceq::ui {

/// Cajón de la etapa de salida (PLAN.md 5.8, ADR 0027), entre la curva y el
/// medidor, en el orden de la señal: la saturación (Off/Soft/Warm, Drive y
/// Mix) arriba, y la ganancia de salida y la compensación automática (Auto)
/// abajo, junto al medidor. Se pliega en una pestaña, que marca con un punto
/// si algo de dentro cambia el sonido.
///
/// Todos los controles se enlazan a parámetros del host. La disposición sale
/// de outputDrawerLayout y los textos de esceq/ui/output_stage.h (testeados
/// sin ventana); aquí solo se pinta.
class OutputDrawer final : public juce::Component
{
public:
    explicit OutputDrawer(PluginProcessor& processor);
    ~OutputDrawer() override;

    /// El usuario pide abrirlo o plegarlo; quien lo contiene decide (y lo guarda).
    std::function<void()> onToggle;

    void setOpen(bool open);
    bool isOpen() const noexcept { return open_; }

    /// Atenúa drive y mezcla con Off y pone al día las ayudas (la latencia
    /// depende del modo de fase y de la frecuencia de muestreo) y la pestaña.
    /// La llama el temporizador del editor.
    void refresh();

    /// Texto y ayuda del botón Auto: los calcula el editor (dsp::autoGainDb).
    void setAutoGainText(const juce::String& label, const juce::String& tooltip);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Toggle;

    PluginProcessor& processor_;
    bool open_ = true;
    OutputDrawerLayout layout_{};

    std::unique_ptr<Toggle> toggle_;
    Segmented mode_;
    ParamControl drive_{ParamControl::Style::Knob, "Drive"};
    ParamControl mix_{ParamControl::Style::Knob, "Mix"};
    ParamControl gain_{ParamControl::Style::Knob, "Gain"};
    ToggleSwitch autoGain_{ToggleSwitch::Style::Pill, "Auto", "Auto gain"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputDrawer)
};

} // namespace esceq::ui
