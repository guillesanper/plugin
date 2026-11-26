// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/history.h"

#include <catch2/catch_test_macros.hpp>

using namespace esceq::core;

namespace {

const std::size_t kGain0 = bandParameter(0, BandParam::Gain);
const std::size_t kGain1 = bandParameter(1, BandParam::Gain);

/// Una acción que cambia un parámetro de `before` a `after`.
EditAction change(std::size_t index, float before, float after)
{
    EditAction action;
    action.parameters.push_back({index, before, after});
    return action;
}

/// Lo que queda al aplicar `action` (cada parámetro a su valor `after`) sobre `state`.
EditState applied(EditState state, const EditAction& action)
{
    for (const auto& c : action.parameters)
        state.parameters[c.index] = c.after;
    if (action.algorithmAfter)
        state.filterAlgorithm = *action.algorithmAfter;
    if (action.macrosAfter)
        state.macros = *action.macrosAfter;
    return state;
}

} // namespace

TEST_CASE("Historial: recien creado no hay nada que deshacer ni rehacer")
{
    UndoHistory history;
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
    CHECK_FALSE(history.undo().has_value());
    CHECK_FALSE(history.redo().has_value());
}

TEST_CASE("Historial: deshacer y rehacer recorren las acciones en orden")
{
    UndoHistory history;
    CHECK(history.commit(change(kOutputGain, 0.0f, 1.0f)));
    CHECK(history.commit(change(kOutputGain, 1.0f, 2.0f)));
    CHECK(history.commit(change(kOutputGain, 2.0f, 3.0f)));
    CHECK(history.undoCount() == 3);

    // Deshacer devuelve la acción al revés: lo que hay que poner es `after`.
    CHECK(history.undo() == change(kOutputGain, 3.0f, 2.0f));
    CHECK(history.undo() == change(kOutputGain, 2.0f, 1.0f));
    CHECK(history.canRedo());
    CHECK(history.redo() == change(kOutputGain, 1.0f, 2.0f));
    CHECK(history.redo() == change(kOutputGain, 2.0f, 3.0f));
    CHECK_FALSE(history.canRedo());
    CHECK_FALSE(history.redo().has_value());

    CHECK(history.undo() == change(kOutputGain, 3.0f, 2.0f));
    CHECK(history.undo() == change(kOutputGain, 2.0f, 1.0f));
    CHECK(history.undo() == change(kOutputGain, 1.0f, 0.0f));
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.undo().has_value());
    CHECK(history.redoCount() == 3);
}

TEST_CASE("Historial: una accion vacia no se registra")
{
    UndoHistory history;
    CHECK_FALSE(history.commit(EditAction{}));
    CHECK_FALSE(history.canUndo());
}

TEST_CASE("Historial: una accion nueva descarta lo que se podia rehacer")
{
    UndoHistory history;
    history.commit(change(kOutputGain, 0.0f, 1.0f));
    history.commit(change(kOutputGain, 1.0f, 2.0f));
    history.undo();
    REQUIRE(history.canRedo());

    CHECK(history.commit(change(kOutputGain, 1.0f, 5.0f)));
    CHECK_FALSE(history.canRedo());
    CHECK(history.undo() == change(kOutputGain, 5.0f, 1.0f));
    CHECK(history.redo() == change(kOutputGain, 1.0f, 5.0f));
}

TEST_CASE("Historial: clear olvida todo lo anterior")
{
    UndoHistory history;
    history.commit(change(kOutputGain, 0.0f, 1.0f));
    history.commit(change(kOutputGain, 1.0f, 2.0f));
    history.undo();

    history.clear();
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
}

TEST_CASE("Historial: al llenarse olvida las acciones mas antiguas")
{
    UndoHistory history(3);
    for (int i = 1; i <= 10; ++i)
        history.commit(change(kOutputGain, static_cast<float>(i - 1), static_cast<float>(i)));

    CHECK(history.undoCount() == 3);
    CHECK(history.undo() == change(kOutputGain, 10.0f, 9.0f));
    CHECK(history.undo() == change(kOutputGain, 9.0f, 8.0f));
    CHECK(history.undo() == change(kOutputGain, 8.0f, 7.0f));
    CHECK_FALSE(history.canUndo());

    // Una capacidad absurda se trata como 1.
    UndoHistory tiny(0);
    tiny.commit(change(kOutputGain, 0.0f, 1.0f));
    tiny.commit(change(kOutputGain, 1.0f, 2.0f));
    CHECK(tiny.undoCount() == 1);
    CHECK(tiny.undo() == change(kOutputGain, 2.0f, 1.0f));
}

TEST_CASE("Accion pendiente: guarda de cada parametro tocado el valor de antes y el de despues")
{
    EditState state;
    PendingAction pending;
    CHECK_FALSE(pending.touched());

    // Dos gestos sobre la misma ganancia (una ráfaga de rueda): el valor de
    // antes es el del primero.
    pending.touch(kGain0, state.parameters[kGain0]);
    state.parameters[kGain0] = 2.0f;
    pending.touch(kGain0, state.parameters[kGain0]);
    state.parameters[kGain0] = 4.0f;
    pending.touch(kGain1, state.parameters[kGain1]);
    state.parameters[kGain1] = -3.0f;
    CHECK(pending.touched());

    const EditAction action = pending.finish(state);
    CHECK_FALSE(pending.touched());
    REQUIRE(action.parameters.size() == 2);
    CHECK(action.parameters[0] == ParameterChange{kGain0, 0.0f, 4.0f});
    CHECK(action.parameters[1] == ParameterChange{kGain1, 0.0f, -3.0f});
    CHECK_FALSE(action.algorithmBefore.has_value());
    CHECK_FALSE(action.algorithmAfter.has_value());
}

TEST_CASE("Accion pendiente: lo que mueve el host sin gestos no entra en la accion")
{
    EditState state;
    PendingAction pending;
    pending.touch(kGain0, state.parameters[kGain0]);
    state.parameters[kGain0] = 6.0f;
    // El host mueve otro parámetro mientras tanto, sin gesto.
    state.parameters[kOutputGain] = -5.0f;
    state.parameters[kGain1] = 2.5f;

    const EditAction action = pending.finish(state);
    REQUIRE(action.parameters.size() == 1);
    CHECK(action.parameters[0].index == kGain0);

    // Deshacer y rehacer sobre un estado en el que el host sigue moviendo la
    // salida: solo cambia la ganancia de la banda.
    UndoHistory history;
    history.commit(action);
    state.parameters[kOutputGain] = 7.0f;
    const auto undone = applied(state, *history.undo());
    CHECK(undone.parameters[kGain0] == 0.0f);
    CHECK(undone.parameters[kOutputGain] == 7.0f);
    CHECK(undone.parameters[kGain1] == 2.5f);
    const auto redone = applied(undone, *history.redo());
    CHECK(redone.parameters[kGain0] == 6.0f);
    CHECK(redone.parameters[kOutputGain] == 7.0f);
}

TEST_CASE("Accion pendiente: un gesto que acaba donde empezo no es una accion")
{
    EditState state;
    PendingAction pending;
    pending.touch(kGain0, 0.0f);
    state.parameters[kGain0] = 3.0f;
    state.parameters[kGain0] = 0.0f; // el arrastre vuelve al sitio
    CHECK(pending.finish(state).empty());

    // Valores que se sanean al mismo valor tampoco cuentan como cambio.
    state.parameters[kOutputGain] = 999.0f;
    pending.touch(kOutputGain, 999.0f);
    state.parameters[kOutputGain] = parameters()[kOutputGain].max;
    CHECK(pending.finish(state).empty());

    // Y los valores se guardan saneados.
    pending.touch(kOutputGain, 0.0f);
    state.parameters[kOutputGain] = 999.0f;
    const auto action = pending.finish(state);
    REQUIRE(action.parameters.size() == 1);
    CHECK(action.parameters[0].after == parameters()[kOutputGain].max);
}

TEST_CASE("Accion pendiente: clear la olvida; los indices fuera de rango se ignoran")
{
    EditState state;
    PendingAction pending;
    pending.touch(kNumParameters, 1.0f);
    CHECK_FALSE(pending.touched());
    pending.touch(kGain0, 0.0f);
    pending.touchAlgorithm(esceq::dsp::FilterAlgorithm::Classic);
    pending.clear();
    CHECK_FALSE(pending.touched());
    state.parameters[kGain0] = 5.0f;
    state.filterAlgorithm = esceq::dsp::FilterAlgorithm::Matched;
    CHECK(pending.finish(state).empty());
}

TEST_CASE("Historial: cambiar el algoritmo de filtro es una accion que se deshace")
{
    using esceq::dsp::FilterAlgorithm;
    EditState state;
    state.filterAlgorithm = FilterAlgorithm::Classic;
    PendingAction pending;
    UndoHistory history;

    pending.touchAlgorithm(state.filterAlgorithm);
    state.filterAlgorithm = FilterAlgorithm::Matched;
    // El mismo algoritmo, tocado otra vez, no cambia el de antes.
    pending.touchAlgorithm(state.filterAlgorithm);
    auto action = pending.finish(state);
    CHECK(action.parameters.empty());
    CHECK(action.algorithmBefore == FilterAlgorithm::Classic);
    CHECK(action.algorithmAfter == FilterAlgorithm::Matched);
    CHECK(history.commit(action));

    // Elegir el que ya suena no es una acción.
    pending.touchAlgorithm(state.filterAlgorithm);
    CHECK(pending.finish(state).empty());

    pending.touch(kOutputGain, 0.0f);
    state.parameters[kOutputGain] = 3.0f;
    CHECK(history.commit(pending.finish(state)));

    const auto gain = history.undo();
    REQUIRE(gain.has_value());
    CHECK_FALSE(gain->algorithmAfter.has_value()); // deshacer la ganancia no toca el algoritmo
    state = applied(state, *gain);
    CHECK(state.parameters[kOutputGain] == 0.0f);
    const auto algorithm = history.undo();
    REQUIRE(algorithm.has_value());
    CHECK(algorithm->algorithmAfter == FilterAlgorithm::Classic);
    CHECK(algorithm->parameters.empty());
    CHECK(history.redo()->algorithmAfter == FilterAlgorithm::Matched);
}

TEST_CASE("Historial: la inversa de una accion intercambia antes y despues")
{
    using esceq::dsp::FilterAlgorithm;
    EditAction action = change(kGain0, 1.0f, 2.0f);
    action.parameters.push_back({kGain1, -1.0f, 3.0f});
    action.algorithmBefore = FilterAlgorithm::Classic;
    action.algorithmAfter = FilterAlgorithm::Matched;
    const auto inverse = action.inverse();
    REQUIRE(inverse.parameters.size() == 2);
    CHECK(inverse.parameters[0] == ParameterChange{kGain0, 2.0f, 1.0f});
    CHECK(inverse.parameters[1] == ParameterChange{kGain1, 3.0f, -1.0f});
    CHECK(inverse.algorithmBefore == FilterAlgorithm::Matched);
    CHECK(inverse.algorithmAfter == FilterAlgorithm::Classic);
    CHECK(inverse.inverse() == action);
}

TEST_CASE("Historial: cambiar las asignaciones de las macros es una accion que se deshace")
{
    // PLAN.md 5.9: cambiar una asignación es una acción de deshacer, y puede ir
    // en la misma acción que los parámetros (cargar un preset).
    EditState state;
    PendingAction pending;
    UndoHistory history;

    MacroAssignments first;
    REQUIRE(first.assign(0, kGain0, 50.0f));
    pending.touchMacros(state.macros);
    state.macros = first;
    // Tocadas otra vez en la misma acción: se quedan las de antes del primer cambio.
    pending.touchMacros(state.macros);
    REQUIRE(state.macros.assign(0, kGain1, -25.0f));
    auto action = pending.finish(state);
    CHECK(action.parameters.empty());
    CHECK_FALSE(action.algorithmAfter.has_value());
    CHECK(action.macrosBefore == MacroAssignments{});
    CHECK(action.macrosAfter == state.macros);
    CHECK_FALSE(action.empty());
    CHECK(history.commit(action));

    // Dejarlas como estaban no es una acción.
    pending.touchMacros(state.macros);
    CHECK(pending.finish(state).empty());

    // Una carga de preset: parámetros y asignaciones juntos.
    const EditState beforeLoad = state;
    pending.touch(kOutputGain, state.parameters[kOutputGain]);
    pending.touchMacros(state.macros);
    state.parameters[kOutputGain] = -3.0f;
    state.macros.clear();
    const auto load = pending.finish(state);
    CHECK(load.parameters.size() == 1);
    CHECK(load.macrosAfter == MacroAssignments{});
    CHECK(history.commit(load));

    const auto undoLoad = history.undo();
    REQUIRE(undoLoad.has_value());
    state = applied(state, *undoLoad);
    CHECK(state == beforeLoad);

    const auto undoAssign = history.undo();
    REQUIRE(undoAssign.has_value());
    state = applied(state, *undoAssign);
    CHECK(state.macros.empty());
    CHECK(state == EditState{});

    // Deshacer un cambio de parámetros no toca las asignaciones.
    history.redo();
    const auto redoLoad = history.redo();
    REQUIRE(redoLoad.has_value());
    CHECK(redoLoad->macrosAfter == MacroAssignments{});
    CHECK(change(kGain0, 1.0f, 2.0f).inverse().macrosAfter == std::nullopt);
}

TEST_CASE("Historial: la inversa de una accion intercambia tambien las asignaciones")
{
    EditAction action;
    MacroAssignments after;
    REQUIRE(after.assign(7, kOutputGain, 10.0f));
    action.macrosBefore = MacroAssignments{};
    action.macrosAfter = after;
    CHECK_FALSE(action.empty());
    const auto inverse = action.inverse();
    CHECK(inverse.macrosBefore == after);
    CHECK(inverse.macrosAfter == MacroAssignments{});
    CHECK(inverse.inverse() == action);

    PendingAction pending;
    pending.touchMacros(after);
    CHECK(pending.touched());
    pending.clear();
    CHECK_FALSE(pending.touched());
}
