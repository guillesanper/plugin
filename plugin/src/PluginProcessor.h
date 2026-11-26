// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "EditHistory.h"
#include "PresetManager.h"

#include "esceq/core/history.h"
#include "esceq/core/macros.h"
#include "esceq/core/parameters.h"
#include "esceq/core/control_thread.h"
#include "esceq/core/match.h"
#include "esceq/core/spectral_mask.h"
#include "esceq/core/spectrum_session.h"
#include "esceq/core/state.h"
#include "esceq/dsp/auto_gain.h"
#include "esceq/dsp/gain_ramp.h"
#include "esceq/dsp/handoff.h"
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
    /// Con una sesión de espectros concreta en lugar de la del proceso, y el
    /// hilo del ajuste de la igualación en manual si se pide (tests).
    explicit PluginProcessor(std::shared_ptr<core::SpectrumSession> session,
                             core::MatchWorker::Threading matchThreading = core::MatchWorker::Threading::Background);
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
    /// En fase lineal, la latencia más la mitad del FIR; en IIR, 0.
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
    /// Valor actual de un parámetro (el de currentValues()). Cualquier hilo.
    float currentValue(std::size_t index) const noexcept;

    /// Pone todos los parámetros a `values`, avisando al host. Cada parámetro
    /// que cambia va en su propio gesto, para que el host lo registre.
    void applyValues(const core::ParameterValues& values);

    /// Pone todos los parámetros a `values` (applyValues) como una sola
    /// acción de deshacer, registrada en el momento, como al cargar un preset:
    /// lo que esperara su registro se registra antes, por separado. Es el
    /// Apply de la igualación. Hilo de mensajes.
    void applyAction(const core::ParameterValues& values);

    /// Algoritmo de filtro de todas las bandas (ADR 0013 y 0014). No es un
    /// parámetro: se guarda en el estado y los presets no lo cambian. Una
    /// instancia nueva empieza en core::kNewInstanceFilterAlgorithm. Se puede
    /// leer desde cualquier hilo.
    dsp::FilterAlgorithm filterAlgorithm() const noexcept { return filterAlgorithm_.load(std::memory_order_relaxed); }

    /// Cambia el algoritmo y, si cambia, avisa al host de que el estado ha
    /// cambiado (para que marque el proyecto como modificado). No crea una
    /// acción de deshacer: para eso, changeFilterAlgorithm(). El audio pasa al
    /// algoritmo nuevo en el bloque siguiente, con el fundido de un cambio
    /// estructural de banda. Hilo de mensajes.
    void setFilterAlgorithm(dsp::FilterAlgorithm algorithm);

    /// Lo que hace el usuario al elegir el algoritmo en la interfaz: lo
    /// cambia como una acción propia, que se puede deshacer. Hilo de mensajes.
    void changeFilterAlgorithm(dsp::FilterAlgorithm algorithm);

    /// Asignaciones de las macros (core/macros.h, ADR 0028). No son
    /// parámetros: se guardan en el estado (versión 4) y en los presets de
    /// usuario. Una instancia nueva no tiene ninguna. Cualquier hilo.
    core::MacroAssignments macroAssignments() const;

    /// Cambia las asignaciones y, si cambian, avisa al host de que el estado ha
    /// cambiado. El audio las usa desde el bloque siguiente. No crea una
    /// acción de deshacer: para eso, changeMacroAssignments(). Hilo de mensajes.
    void setMacroAssignments(const core::MacroAssignments& macros);

    /// Lo que hará el usuario al asignar o quitar un destino en la interfaz:
    /// las cambia como una acción propia, que se puede deshacer. Hilo de mensajes.
    void changeMacroAssignments(const core::MacroAssignments& macros);

    /// Lo que suena: los valores de los parámetros con la modulación de las
    /// macros (core::applyMacros()). Los del host (currentValues()) no
    /// cambian. Hilo de mensajes.
    core::ParameterValues soundingValues() const;

    /// Lo que deshace el historial: los parámetros, el algoritmo y las
    /// asignaciones de las macros actuales.
    core::EditState currentEditState() const;

    /// Aplica lo que devuelve el historial: cada parámetro de la acción a su
    /// valor `after`, en su gesto si cambia (como applyValues()), y el
    /// algoritmo y las asignaciones (setFilterAlgorithm() y
    /// setMacroAssignments()) si la acción los cambia. Los demás parámetros no
    /// se tocan. Hilo de mensajes.
    void applyEdit(const core::EditAction& action);

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

    /// Forma de la máscara de cada banda con dinámica espectral, para el
    /// sombreado de la curva (core/spectral_mask.h): el hilo de audio la
    /// publica unas 30 veces por segundo mientras suena. Un único lector: la
    /// curva del editor abierto, con read().
    core::SpectralMaskPublisher& spectralMask() noexcept { return spectralMask_; }

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

    /// Igualar a una referencia (core/match.h, ADR 0023): la referencia, los
    /// ajustes y lo aprendido, que viven aquí mientras el plugin está abierto
    /// y no se guardan; el hilo del ajuste, que se crea con la primera
    /// petición; y la cola del sidechain, que el hilo de audio solo escribe
    /// mientras se aprende de él. Lo usa ui::MatchController, en el hilo de
    /// mensajes (la cola, su único lector).
    core::MatchState& matchState() noexcept { return match_; }
    core::MatchWorker& matchWorker() noexcept { return matchWorker_; }
    core::MatchSidechainTap& matchSidechain() noexcept { return matchSidechain_; }

    /// true si el host tiene conectado el bus de sidechain.
    bool sidechainConnected() const noexcept { return sidechainConnected_.load(std::memory_order_relaxed); }

    /// Latencia del modo de fase que suena, en muestras (cualquier hilo). El
    /// host la conoce por getLatencySamples(), que se pone al día en
    /// prepareToPlay() y, si cambia con el audio en marcha, desde el hilo de
    /// mensajes: el hilo de control ve el cambio (como mucho
    /// core::ControlThread::kPeriod después) y le manda un aviso.
    int engineLatencySamples() const noexcept { return equalizer_.latencySamples(); }

    /// Hace ya, en el hilo que llama, el trabajo pendiente del hilo de
    /// control: diseños de fase lineal, compensación y aviso de latencia
    /// (core::ControlThread::flush()). Bloquea: nunca desde el hilo de audio
    /// en tiempo real.
    void flushControlThread() { control_.flush(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    /// Pasa los parámetros al ecualizador si han cambiado desde la última vez
    /// (o siempre, con `force`) y, con la compensación encendida, pide la suya.
    void pushParametersToEngine(bool force = false) noexcept;
    /// Pone un parámetro a `value` (saneado) en su propio gesto, si cambia.
    void setWithGesture(std::size_t index, float value);
    /// Compensación que se aplica ahora, en dB: la última calculada para lo
    /// pasado con la compensación encendida, o 0. Solo el hilo de audio.
    float autoGainDb() const noexcept;

    /// Avisa al host, desde el hilo de mensajes, cuando cambia la latencia del
    /// motor (al cambiar de modo de fase con el audio en marcha). Lo pide el
    /// hilo de control al ver el cambio; sin cambios no hay mensajes (ADR 0021).
    class LatencyReporter final : public juce::AsyncUpdater
    {
    public:
        explicit LatencyReporter(PluginProcessor& owner) : owner_(owner) {}

    private:
        void handleAsyncUpdate() override;
        PluginProcessor& owner_;
    };

    juce::AudioProcessorValueTreeState apvts_;
    std::array<std::atomic<float>*, core::kNumParameters> raw_{};
    std::array<juce::RangedAudioParameter*, core::kNumParameters> params_{};

    dsp::PhaseEqualizer equalizer_;
    dsp::AutoGain autoGain_;
    /// Antes que control_, que lo usa desde su hilo: se destruye después.
    LatencyReporter latencyReporter_{*this};
    /// Diseña los FIR de fase lineal, calcula la compensación y ve los cambios
    /// de latencia fuera del hilo de audio. Va después de equalizer_,
    /// autoGain_ y latencyReporter_ para destruirse antes.
    core::ControlThread control_{equalizer_, autoGain_, core::ControlThread::Threading::Background,
                                 [this](int) { latencyReporter_.triggerAsyncUpdate(); }};
    core::ParameterValues pushedValues_{}; ///< Lo último que se pasó al ecualizador, sin modular. Solo audio.
    dsp::FilterAlgorithm pushedAlgorithm_ = core::kNewInstanceFilterAlgorithm; ///< Ídem.
    bool autoGainPushed_ = false; ///< Con la compensación encendida en lo último pasado. Ídem.
    int engineChannels_ = 2;      ///< Canales con los que se preparó el ecualizador. Ídem.
    std::atomic<dsp::FilterAlgorithm> filterAlgorithm_{core::kNewInstanceFilterAlgorithm};
    static_assert(std::atomic<dsp::FilterAlgorithm>::is_always_lock_free);

    /// Asignaciones de las macros: la copia del hilo de mensajes, con su
    /// cerrojo (que también hace de un solo escritor a macroBuffer_), y la del
    /// hilo de audio, que llega por macroBuffer_ sin bloqueos.
    core::MacroAssignments macros_;
    mutable juce::CriticalSection macrosLock_;
    dsp::TripleBuffer<core::MacroAssignments> macroBuffer_;
    core::MacroAssignments audioMacros_;    ///< Solo el hilo de audio.
    bool audioMacrosActive_ = false;        ///< audioMacros_ tiene algún destino. Ídem.
    float outputGainDb_ = 0.0f;             ///< La ganancia de salida que suena (modulada). Ídem.
    dsp::GainRamp outputGain_;
    dsp::SpscFifo preFifo_{1 << 15};
    dsp::SpscFifo postFifo_{1 << 15};
    std::atomic<double> sampleRate_{0.0};
    std::array<std::atomic<float>, core::kNumBands> dynamicGains_{};
    core::SpectralMaskPublisher spectralMask_;
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

    core::MatchState match_;
    core::MatchSidechainTap matchSidechain_;
    core::MatchWorker matchWorker_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace esceq
