// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "EditHistory.h"
#include "PresetManager.h"

#include "esceq/core/parameters.h"
#include "esceq/core/phase_design_thread.h"
#include "esceq/core/spectrum_session.h"
#include "esceq/core/state.h"
#include "esceq/dsp/gain_ramp.h"
#include "esceq/dsp/phase_equalizer.h"
#include "esceq/dsp/spsc_fifo.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <memory>

namespace esceq {

/// Adaptador entre el host (vía JUCE) y los módulos core/dsp.
/// No contiene lógica de audio propia: traduce parámetros y estado, y
/// expone a la interfaz lo que necesita para dibujar.
class PluginProcessor final : public juce::AudioProcessor
{
public:
    PluginProcessor();
    /// Con una sesión de espectros concreta en lugar de la del proceso (tests).
    explicit PluginProcessor(std::shared_ptr<core::SpectrumSession> session);
    ~PluginProcessor() override;

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
    /// En fase lineal, la latencia más la mitad del FIR; en Natural, 0.
    double getTailLengthSeconds() const override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    /// El host dice el nombre y el color de la pista: se publican en la sesión.
    void updateTrackProperties(const TrackProperties& properties) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- Para la interfaz (hilo de mensajes) ---

    juce::AudioProcessorValueTreeState& state() noexcept { return apvts_; }

    /// Parámetro del host por su índice en core::parameters().
    juce::RangedAudioParameter& parameter(std::size_t index) const noexcept { return *params_[index]; }

    /// Valores actuales de todos los parámetros.
    core::ParameterValues currentValues() const noexcept;

    /// Pone todos los parámetros a `values`, avisando al host. Cada parámetro
    /// que cambia va en su propio gesto, para que el host lo registre.
    void applyValues(const core::ParameterValues& values);

    /// Deshacer y rehacer, y presets.
    EditHistory& history() noexcept { return *history_; }
    PresetManager& presets() noexcept { return presets_; }

    /// Preferencias de la interfaz que se guardan con el proyecto.
    core::UiSettings uiSettings() const;
    void setUiSettings(const core::UiSettings& settings);

    /// Cambia cada vez que el host carga un estado: la interfaz abierta
    /// vuelve a leer las preferencias.
    int stateRevision() const noexcept { return stateRevision_.load(std::memory_order_relaxed); }

    /// Audio mono antes y después del ecualizador, para el analizador.
    /// Un único lector: el editor abierto.
    dsp::SpscFifo& preAnalyzerFifo() noexcept { return preFifo_; }
    dsp::SpscFifo& postAnalyzerFifo() noexcept { return postFifo_; }

    /// Frecuencia de muestreo actual, o 48 kHz si aún no se conoce.
    double currentSampleRate() const noexcept;

    /// Ganancia dinámica que se está aplicando a una banda, en dB (0 si no es
    /// dinámica). La escribe el hilo de audio tras cada bloque; se puede leer
    /// desde cualquier hilo.
    float dynamicGainDb(int band) const noexcept;

    /// Mayor valor absoluto de la salida del canal `channel` desde la última
    /// llamada, y lo pone a cero. Un único lector: el medidor del editor.
    float takeOutputPeak(int channel) noexcept;

    /// Canales de la salida principal (1 o 2).
    int outputChannelCount() const noexcept { return outputChannels_.load(std::memory_order_relaxed); }

    /// Espectros de las instancias de esceQ de la sesión, para compararlos.
    /// Ver esceq/core/spectrum_session.h.
    core::SpectrumSession& spectrumSession() noexcept { return *session_; }
    /// Identificador de esta instancia en la sesión.
    core::InstanceId instanceId() const noexcept { return sessionMember_->id(); }

    /// Qué instancias superpone la interfaz y si marca las colisiones. Vive
    /// aquí y no en el editor para que sobreviva a cerrar y abrir la ventana;
    /// no se guarda con el proyecto. Solo desde el hilo de mensajes.
    core::CompareSelection& compareSelection() noexcept { return compare_; }

    /// true si el host tiene conectado el bus de sidechain.
    bool sidechainConnected() const noexcept { return sidechainConnected_.load(std::memory_order_relaxed); }

    /// Latencia del modo de fase que suena, en muestras (cualquier hilo). El
    /// host la conoce por getLatencySamples(), que se pone al día en
    /// prepareToPlay() y, si cambia con el audio en marcha, desde el hilo de
    /// mensajes, como mucho 1/kLatencyPollHz segundos después.
    int engineLatencySamples() const noexcept { return equalizer_.latencySamples(); }

    static constexpr int kLatencyPollHz = 20;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    /// Pasa los parámetros al ecualizador si han cambiado desde la última vez (o siempre, con `force`).
    void pushParametersToEngine(bool force = false) noexcept;

    /// Avisa al host, desde el hilo de mensajes, cuando cambia la latencia del
    /// motor (al cambiar de modo de fase con el audio en marcha).
    class LatencyReporter final : private juce::Timer
    {
    public:
        explicit LatencyReporter(PluginProcessor& owner);
        ~LatencyReporter() override;

    private:
        void timerCallback() override;
        PluginProcessor& owner_;
    };

    juce::AudioProcessorValueTreeState apvts_;
    std::array<std::atomic<float>*, core::kNumParameters> raw_{};
    std::array<juce::RangedAudioParameter*, core::kNumParameters> params_{};

    dsp::PhaseEqualizer equalizer_;
    /// Diseña los FIR de fase lineal fuera del hilo de audio. Va después de
    /// equalizer_ para destruirse antes.
    core::PhaseDesignThread designer_{equalizer_};
    core::ParameterValues pushedValues_{}; ///< Lo último que se pasó al ecualizador. Solo el hilo de audio.
    dsp::GainRamp outputGain_;
    dsp::SpscFifo preFifo_{1 << 15};
    dsp::SpscFifo postFifo_{1 << 15};
    std::atomic<double> sampleRate_{0.0};
    std::array<std::atomic<float>, core::kNumBands> dynamicGains_{};
    std::atomic<bool> sidechainConnected_{false};
    std::array<std::atomic<float>, 2> outputPeaks_{};
    std::atomic<int> outputChannels_{2};

    core::UiSettings uiSettings_;
    juce::CriticalSection uiSettingsLock_;
    std::atomic<int> stateRevision_{0};
    std::unique_ptr<EditHistory> history_;
    PresetManager presets_{*this};

    std::shared_ptr<core::SpectrumSession> session_;
    std::unique_ptr<core::SpectrumSession::Member> sessionMember_;
    core::CompareSelection compare_;

    /// El último: su temporizador se para antes de destruir nada de lo que lee.
    LatencyReporter latencyReporter_{*this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace esceq
