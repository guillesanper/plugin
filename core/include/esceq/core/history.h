// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/core/state.h"

#include <array>
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

/// Un parámetro que cambia en una acción: su valor de antes y el de después
/// (saneados).
struct ParameterChange
{
    std::size_t index = 0;
    float before = 0.0f;
    float after = 0.0f;

    friend bool operator==(const ParameterChange&, const ParameterChange&) = default;
};

/// Una acción del historial: los parámetros que tocaron los gestos del
/// usuario y cambiaron, en orden de índice, y el algoritmo de filtro si
/// cambió (los dos valores, o ninguno). Aplicarla es poner cada parámetro en
/// su valor `after` (y el algoritmo en `algorithmAfter`); lo demás no se toca.
struct EditAction
{
    std::vector<ParameterChange> parameters;
    std::optional<dsp::FilterAlgorithm> algorithmBefore;
    std::optional<dsp::FilterAlgorithm> algorithmAfter;

    bool empty() const noexcept { return parameters.empty() && !algorithmAfter; }
    /// La misma acción al revés: lo que la deshace.
    EditAction inverse() const;

    friend bool operator==(const EditAction&, const EditAction&) = default;
};

/// La acción que se está haciendo: de cada parámetro que toca un gesto, el
/// valor que tenía al empezar el primero (touch()), y el algoritmo de antes de
/// cambiarlo (touchAlgorithm()). finish() la cierra con los valores de ahora.
/// Lo que no se toca (lo que mueve el host sin gestos) no entra.
class PendingAction
{
public:
    /// El parámetro `index` empieza un gesto con el valor `value`. Si ya
    /// estaba tocado en esta acción, se queda el valor del primer gesto.
    /// Índices fuera de rango se ignoran.
    void touch(std::size_t index, float value) noexcept;
    /// El algoritmo va a cambiar; ahora es `current` (si ya estaba tocado, se
    /// queda el de antes).
    void touchAlgorithm(dsp::FilterAlgorithm current) noexcept;
    /// true si se ha tocado algo desde el último finish() o clear().
    bool touched() const noexcept { return any_; }

    /// La acción, de lo tocado a `current`, sin lo que acaba como empezó
    /// (comparado saneado); y empieza otra vacía.
    EditAction finish(const EditState& current);
    void clear() noexcept;

private:
    std::array<bool, kNumParameters> touched_{};
    ParameterValues before_{};
    std::optional<dsp::FilterAlgorithm> algorithmBefore_;
    bool any_ = false;
};

/// Historial de deshacer y rehacer de los parámetros del plugin y su
/// algoritmo de filtro, por diferencias (ADR 0028).
///
/// Cada acción guarda solo lo que cambió (EditAction): deshacer y rehacer
/// devuelven lo que hay que aplicar, y no tocan nada más. Así, un parámetro
/// que mueve el host mientras tanto (un automation clip, un controlador) se
/// queda como lo deja el host. Quién decide qué es "una acción" (un arrastre
/// entero, una ráfaga de rueda) es quien llama a commit(); ver
/// plugin/src/EditHistory.h.
///
/// No es segura entre hilos ni en tiempo real: se usa desde el hilo de mensajes.
class UndoHistory
{
public:
    static constexpr std::size_t kDefaultCapacity = 200;

    /// `capacity`: número máximo de acciones que se pueden deshacer (>= 1).
    explicit UndoHistory(std::size_t capacity = kDefaultCapacity);

    /// Olvida el historial. Se usa al cargar un proyecto: lo anterior ya no
    /// se puede deshacer.
    void clear();

    /// Registra una acción nueva. Una acción vacía no se registra (devuelve
    /// false). Una acción nueva descarta lo que se pudiera rehacer. Si se
    /// supera la capacidad, se olvida la acción más antigua.
    bool commit(EditAction action);

    bool canUndo() const noexcept { return index_ > 0; }
    bool canRedo() const noexcept { return index_ < actions_.size(); }

    /// Vuelve una acción atrás y devuelve lo que hay que aplicar (la acción al
    /// revés), o nullopt si no hay nada que deshacer.
    std::optional<EditAction> undo();

    /// Vuelve una acción adelante y devuelve lo que hay que aplicar.
    std::optional<EditAction> redo();

    std::size_t undoCount() const noexcept { return index_; }
    std::size_t redoCount() const noexcept { return actions_.size() - index_; }

private:
    std::size_t capacity_;
    std::vector<EditAction> actions_;
    std::size_t index_ = 0; ///< Acciones hechas: las de antes de index_.
};

} // namespace esceq::core
