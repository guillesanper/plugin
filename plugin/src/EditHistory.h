// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/history.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace esceq {

class PluginProcessor;

/// Deshacer y rehacer dentro del plugin, con las acciones agrupadas por gesto.
///
/// Escucha el principio y el final de los gestos de todos los parámetros
/// (los marca la interfaz con beginChangeGesture/endChangeGesture) y, cuando
/// termina el último gesto abierto, registra una instantánea en
/// core::UndoHistory. Así:
///  - un arrastre entero es una sola acción, por muchos valores que recorra;
///  - añadir una banda (seis parámetros seguidos) es una sola acción;
///  - una ráfaga de rueda es una sola acción: el registro espera
///    kCoalesceMs por si llega otro gesto.
///
/// Los cambios que hace el host (automatización) no abren gestos y no crean
/// acciones: quedan incluidos en la instantánea de la siguiente acción.
///
/// Los gestos que abren deshacer y rehacer (applyValues) no programan ningún
/// registro: si el host automatiza algo mientras tanto, el rehacer no se pierde.
///
/// Los gestos abiertos se cuentan por parámetro: un final sin principio no
/// descuadra nada. Si la interfaz desaparece con un gesto abierto, llama a
/// abandonOpenGestures() para que el historial no se quede esperándolo.
///
/// Las funciones públicas se llaman desde el hilo de mensajes.
class EditHistory final : private juce::AudioProcessorParameter::Listener,
                          private juce::AsyncUpdater,
                          private juce::Timer
{
public:
    static constexpr int kCoalesceMs = 300;

    explicit EditHistory(PluginProcessor& processor);
    ~EditHistory() override;

    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

    /// Olvida el historial: el estado actual pasa a ser el punto de partida.
    /// Se llama al cargar el estado desde el host.
    void reset();

    /// Registra ya el estado actual como una acción (por ejemplo, tras cargar un preset).
    void commitNow();

    /// Da por terminados los gestos abiertos (la interfaz se cierra en mitad
    /// de un arrastre). Si había alguno, lo hecho se registra como una acción.
    void abandonOpenGestures();

private:
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void flushPending();
    /// Aplica una instantánea del historial sin que sus gestos programen un registro.
    void apply(const core::ParameterValues& values);

    PluginProcessor& processor_;
    core::UndoHistory history_;
    juce::CriticalSection lock_; ///< Protege history_, openGestures_ y numOpenGestures_.
    std::array<bool, core::kNumParameters> openGestures_{};
    int numOpenGestures_ = 0;
    std::atomic<bool> applying_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditHistory)
};

} // namespace esceq
