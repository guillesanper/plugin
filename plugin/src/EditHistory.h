// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/history.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace esceq {

class PluginProcessor;

/// Deshacer y rehacer dentro del plugin, con las acciones agrupadas por gesto.
/// Deshace los parámetros y el algoritmo de filtro, por diferencias
/// (core::UndoHistory, ADR 0028).
///
/// Escucha el principio y el final de los gestos de todos los parámetros
/// (los marca la interfaz con beginChangeGesture/endChangeGesture). Al
/// empezar el primer gesto de un parámetro en una acción, guarda su valor
/// (core::PendingAction); cuando termina el último gesto abierto, registra la
/// acción: de cada parámetro tocado, el valor de antes y el de ahora. Así:
///  - un arrastre entero es una sola acción, por muchos valores que recorra;
///  - añadir una banda (seis parámetros seguidos) es una sola acción;
///  - una ráfaga de rueda es una sola acción: el registro espera
///    kCoalesceMs por si llega otro gesto.
///
/// Los cambios que hace el host (automation clips, controladores) no abren
/// gestos: no entran en ninguna acción, y deshacer y rehacer no los tocan
/// (solo cambian los parámetros de la acción).
///
/// Los gestos que abren deshacer y rehacer (applyEdit) no se registran: si el
/// host automatiza algo mientras tanto, el rehacer no se pierde.
///
/// Los gestos abiertos se cuentan por parámetro: un final sin principio no
/// descuadra nada. Si la interfaz desaparece con un gesto abierto, llama a
/// abandonOpenGestures() para que el historial no se quede esperándolo.
///
/// Las funciones públicas se llaman desde el hilo de mensajes. La excepción
/// es reset(): llega desde setStateInformation, y si el host carga el estado
/// desde otro hilo (VST3 y CLAP no lo permiten; AU no lo prohíbe) no hay
/// carreras de datos, pero un deshacer simultáneo puede quedar a medias.
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

    /// Registra ya lo tocado como una acción (por ejemplo, tras cargar un
    /// preset: sus gestos son los de applyValues).
    void commitNow();

    /// El algoritmo de filtro va a cambiar como parte de la acción en curso:
    /// guarda el de ahora. Ver PluginProcessor::changeFilterAlgorithm().
    void touchFilterAlgorithm();

    /// Si hay una acción esperando su registro (un gesto terminado hace menos
    /// de kCoalesceMs), la registra ya. Se llama antes de un cambio que no es
    /// un gesto (el algoritmo de filtro) para que sea una acción aparte.
    void flushPending();

    /// Da por terminados los gestos abiertos (la interfaz se cierra en mitad
    /// de un arrastre). Si había alguno, lo hecho se registra como una acción.
    void abandonOpenGestures();

private:
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;
    /// Aplica lo que devuelve el historial sin que sus gestos se registren.
    void apply(const core::EditAction& action);
    /// Registra lo tocado como una acción. Con lock_ tomado. Los parámetros
    /// con un gesto aún abierto empiezan la acción siguiente desde su valor
    /// de ahora.
    void commitPending();

    PluginProcessor& processor_;
    core::UndoHistory history_;
    core::PendingAction pending_;
    juce::CriticalSection lock_; ///< Protege history_, pending_, openGestures_ y numOpenGestures_.
    std::array<bool, core::kNumParameters> openGestures_{};
    int numOpenGestures_ = 0;
    std::atomic<bool> applying_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditHistory)
};

} // namespace esceq
