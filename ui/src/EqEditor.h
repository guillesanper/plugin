// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "BandControls.h"
#include "CurveView.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace esceq {
class PluginProcessor;
}

namespace esceq::ui {

/// Ventana del plugin: barra superior (analizador, rango, salida, bypass),
/// vista de la curva y panel de la banda seleccionada.
class EqEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EqEditor(PluginProcessor& processor);
    ~EqEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    PluginProcessor& processor_;
    juce::LookAndFeel_V4 lookAndFeel_;

    CurveView curve_;
    BandControls controls_;

    juce::Label title_;
    juce::ComboBox analyzer_;
    juce::ComboBox range_;
    juce::Slider output_;
    juce::Label outputLabel_;
    juce::ToggleButton bypass_{"Bypass"};

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqEditor)
};

} // namespace esceq::ui
