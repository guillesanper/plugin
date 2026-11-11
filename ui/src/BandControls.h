// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace esceq {
class PluginProcessor;
}

namespace esceq::ui {

/// Controles de la banda seleccionada: activación, tipo, pendiente,
/// frecuencia, ganancia y Q. Se enlazan directamente a los parámetros del
/// host, así que deshacer, automatizar y guardar funcionan igual que desde
/// la curva.
class BandControls final : public juce::Component
{
public:
    explicit BandControls(PluginProcessor& processor);
    ~BandControls() override;

    /// Banda a mostrar; -1 muestra el panel vacío.
    void setBand(int band);

    /// Activa o atenúa los controles que no influyen con el tipo actual.
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    using Apvts = juce::AudioProcessorValueTreeState;

    PluginProcessor& processor_;
    int band_ = -1;

    juce::Label title_;
    juce::Label hint_;
    juce::ToggleButton enabled_{"On"};
    juce::ComboBox type_;
    juce::ComboBox slope_;
    juce::Slider frequency_;
    juce::Slider gain_;
    juce::Slider q_;
    juce::Label frequencyLabel_;
    juce::Label gainLabel_;
    juce::Label qLabel_;

    std::unique_ptr<Apvts::ButtonAttachment> enabledAttachment_;
    std::unique_ptr<Apvts::ComboBoxAttachment> typeAttachment_;
    std::unique_ptr<Apvts::ComboBoxAttachment> slopeAttachment_;
    std::unique_ptr<Apvts::SliderAttachment> frequencyAttachment_;
    std::unique_ptr<Apvts::SliderAttachment> gainAttachment_;
    std::unique_ptr<Apvts::SliderAttachment> qAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandControls)
};

} // namespace esceq::ui
