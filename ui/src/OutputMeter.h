// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/ui/meter_logic.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace esceq {
class PluginProcessor;
}

namespace esceq::ui {

/// Medidor de nivel de salida: una barra por canal, pico retenido y aviso de
/// recorte (se borra con un clic). La escala y la balística están en
/// esceq/ui/meter_logic.h.
class OutputMeter final : public juce::Component
{
public:
    explicit OutputMeter(PluginProcessor& processor);

    /// Lee los picos que ha publicado el hilo de audio. La llama el
    /// temporizador del editor.
    void refresh();

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    PluginProcessor& processor_;
    std::array<PeakMeter, 2> meters_{};
    int channels_ = 2;
    double lastUpdateMs_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputMeter)
};

} // namespace esceq::ui
