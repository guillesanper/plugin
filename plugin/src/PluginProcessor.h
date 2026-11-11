// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/dsp/equalizer.h"
#include "esceq/dsp/gain_ramp.h"
#include "esceq/dsp/spsc_fifo.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>

namespace esceq {

/// Adaptador entre el host (vía JUCE) y los módulos core/dsp.
/// No contiene lógica de audio propia: traduce parámetros y estado, y
/// expone a la interfaz lo que necesita para dibujar.
class PluginProcessor final : public juce::AudioProcessor
{
public:
    PluginProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "esceQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    juce::AudioProcessorParameter* getBypassParameter() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- Para la interfaz (hilo de mensajes) ---

    juce::AudioProcessorValueTreeState& state() noexcept { return apvts_; }

    /// Parámetro del host por su índice en core::parameters().
    juce::RangedAudioParameter& parameter(std::size_t index) const noexcept { return *params_[index]; }

    /// Valores actuales de todos los parámetros.
    core::ParameterValues currentValues() const noexcept;

    /// Audio mono antes y después del ecualizador, para el analizador.
    /// Un único lector: el editor abierto.
    dsp::SpscFifo& preAnalyzerFifo() noexcept { return preFifo_; }
    dsp::SpscFifo& postAnalyzerFifo() noexcept { return postFifo_; }

    /// Frecuencia de muestreo actual, o 48 kHz si aún no se conoce.
    double currentSampleRate() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pushParametersToEngine() noexcept;

    juce::AudioProcessorValueTreeState apvts_;
    std::array<std::atomic<float>*, core::kNumParameters> raw_{};
    std::array<juce::RangedAudioParameter*, core::kNumParameters> params_{};

    dsp::Equalizer equalizer_;
    dsp::GainRamp outputGain_;
    dsp::SpscFifo preFifo_{1 << 15};
    dsp::SpscFifo postFifo_{1 << 15};
    std::atomic<double> sampleRate_{0.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace esceq
