// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/core/state.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace esceq::core {

/// Lo que el usuario edita y suena: los parámetros y el algoritmo de filtro
/// de la instancia. Es lo que deshace el historial. (Las preferencias de la
/// interfaz no se deshacen.)
struct EditState
{
    ParameterValues parameters = defaultValues();
    dsp::FilterAlgorithm filterAlgorithm = kNewInstanceFilterAlgorithm;

    friend bool operator==(const EditState&, const EditState&) = default;
};

/// Historial de deshacer y rehacer de los parámetros del plugin y su algoritmo de filtro.
///
/// Guarda instantáneas completas (unos 1,4 kB cada una): cada acción del usuario
/// es una instantánea, y deshacer es volver a la anterior. Quién decide qué
/// es "una acción" (un arrastre entero, una ráfaga de rueda) es quien llama
/// a commit(); ver plugin/src/EditHistory.h.
///
/// No es segura entre hilos ni en tiempo real: se usa desde el hilo de mensajes.
class UndoHistory
{
public:
    static constexpr std::size_t kDefaultCapacity = 200;

    /// `capacity`: número máximo de acciones que se pueden deshacer (>= 1).
    explicit UndoHistory(std::size_t capacity = kDefaultCapacity);

    /// Olvida el historial y toma `current` como punto de partida. Se usa al
    /// cargar un proyecto: lo anterior ya no se puede deshacer.
    void reset(const EditState& current);

    /// Registra el estado actual como una acción nueva. Si no ha cambiado
    /// nada respecto al último estado, no hace nada y devuelve false. Una
    /// acción nueva descarta lo que se pudiera rehacer. Si se supera la
    /// capacidad, se olvida la acción más antigua.
    bool commit(const EditState& current);

    bool canUndo() const noexcept { return index_ > 0; }
    bool canRedo() const noexcept { return index_ + 1 < states_.size(); }

    /// Vuelve una acción atrás y devuelve el estado que hay que aplicar, o
    /// nullopt si no hay nada que deshacer.
    std::optional<EditState> undo();

    /// Vuelve una acción adelante.
    std::optional<EditState> redo();

    /// Estado en el que está el historial.
    const EditState& present() const noexcept { return states_[index_]; }

    std::size_t undoCount() const noexcept { return index_; }
    std::size_t redoCount() const noexcept { return states_.size() - index_ - 1; }

private:
    std::size_t capacity_;
    std::vector<EditState> states_;
    std::size_t index_ = 0;
};

} // namespace esceq::core
