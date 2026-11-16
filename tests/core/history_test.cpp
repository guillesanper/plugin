// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/history.h"

#include <catch2/catch_test_macros.hpp>

using namespace esceq::core;

namespace {

EditState withGain(float db)
{
    EditState state;
    state.parameters[kOutputGain] = db;
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
    CHECK(history.present() == EditState{});
}

TEST_CASE("Historial: deshacer y rehacer recorren las acciones en orden")
{
    UndoHistory history;
    history.reset(withGain(0.0f));
    CHECK(history.commit(withGain(1.0f)));
    CHECK(history.commit(withGain(2.0f)));
    CHECK(history.commit(withGain(3.0f)));
    CHECK(history.undoCount() == 3);

    CHECK(history.undo() == withGain(2.0f));
    CHECK(history.undo() == withGain(1.0f));
    CHECK(history.canRedo());
    CHECK(history.redo() == withGain(2.0f));
    CHECK(history.redo() == withGain(3.0f));
    CHECK_FALSE(history.canRedo());
    CHECK_FALSE(history.redo().has_value());

    CHECK(history.undo() == withGain(2.0f));
    CHECK(history.undo() == withGain(1.0f));
    CHECK(history.undo() == withGain(0.0f));
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.undo().has_value());
    CHECK(history.present() == withGain(0.0f));
}

TEST_CASE("Historial: registrar sin cambios no crea una accion")
{
    UndoHistory history;
    history.reset(withGain(1.0f));
    CHECK_FALSE(history.commit(withGain(1.0f)));
    CHECK_FALSE(history.canUndo());

    CHECK(history.commit(withGain(2.0f)));
    CHECK_FALSE(history.commit(withGain(2.0f)));
    CHECK(history.undoCount() == 1);

    // Valores que se sanean al mismo estado tampoco cuentan como cambio.
    CHECK_FALSE(history.commit(withGain(2.0f + 0.0f)));
    auto outOfRange = withGain(999.0f);
    CHECK(history.commit(outOfRange));
    CHECK_FALSE(history.commit(withGain(parameters()[kOutputGain].max)));
}

TEST_CASE("Historial: una accion nueva descarta lo que se podia rehacer")
{
    UndoHistory history;
    history.reset(withGain(0.0f));
    history.commit(withGain(1.0f));
    history.commit(withGain(2.0f));
    history.undo();
    REQUIRE(history.canRedo());

    CHECK(history.commit(withGain(5.0f)));
    CHECK_FALSE(history.canRedo());
    CHECK(history.undo() == withGain(1.0f));
    CHECK(history.redo() == withGain(5.0f));
}

TEST_CASE("Historial: reset olvida todo lo anterior")
{
    UndoHistory history;
    history.commit(withGain(1.0f));
    history.commit(withGain(2.0f));
    history.undo();

    history.reset(withGain(7.0f));
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
    CHECK(history.present() == withGain(7.0f));
}

TEST_CASE("Historial: al llenarse olvida las acciones mas antiguas")
{
    UndoHistory history(3);
    history.reset(withGain(0.0f));
    for (int i = 1; i <= 10; ++i)
        history.commit(withGain(static_cast<float>(i)));

    CHECK(history.undoCount() == 3);
    CHECK(history.undo() == withGain(9.0f));
    CHECK(history.undo() == withGain(8.0f));
    CHECK(history.undo() == withGain(7.0f));
    CHECK_FALSE(history.canUndo());

    // Una capacidad absurda se trata como 1.
    UndoHistory tiny(0);
    tiny.reset(withGain(0.0f));
    tiny.commit(withGain(1.0f));
    tiny.commit(withGain(2.0f));
    CHECK(tiny.undoCount() == 1);
    CHECK(tiny.undo() == withGain(1.0f));
}

TEST_CASE("Historial: guarda todos los parametros, no solo el que cambia")
{
    UndoHistory history;
    EditState state;
    history.reset(state);

    auto& values = state.parameters;
    values[bandParameter(3, BandParam::Enabled)] = 1.0f;
    values[bandParameter(3, BandParam::Frequency)] = 440.0f;
    values[channelParameter(3)] = 3.0f;
    history.commit(state);

    values[dynamicParameter(3, DynParam::Threshold)] = -40.0f;
    history.commit(state);

    const auto back = history.undo();
    REQUIRE(back.has_value());
    CHECK(back->parameters[bandParameter(3, BandParam::Frequency)] == 440.0f);
    CHECK(back->parameters[channelParameter(3)] == 3.0f);
    CHECK(back->parameters[dynamicParameter(3, DynParam::Threshold)] ==
          parameters()[dynamicParameter(3, DynParam::Threshold)].defaultValue);
    CHECK(history.undo() == EditState{});
}

TEST_CASE("Historial: cambiar el algoritmo de filtro es una accion que se deshace")
{
    UndoHistory history;
    EditState state;
    state.filterAlgorithm = esceq::dsp::FilterAlgorithm::Classic;
    history.reset(state);

    // Los mismos parametros con otro algoritmo suenan distinto: es un cambio.
    state.filterAlgorithm = esceq::dsp::FilterAlgorithm::Matched;
    CHECK(history.commit(state));
    CHECK_FALSE(history.commit(state));

    state.parameters[kOutputGain] = 3.0f;
    CHECK(history.commit(state));

    const auto before = history.undo();
    REQUIRE(before.has_value());
    CHECK(before->filterAlgorithm == esceq::dsp::FilterAlgorithm::Matched);
    CHECK(before->parameters[kOutputGain] == 0.0f);
    const auto first = history.undo();
    REQUIRE(first.has_value());
    CHECK(first->filterAlgorithm == esceq::dsp::FilterAlgorithm::Classic);
    CHECK(history.redo()->filterAlgorithm == esceq::dsp::FilterAlgorithm::Matched);
}
