// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/history.h"

#include <catch2/catch_test_macros.hpp>

using namespace esceq::core;

namespace {

ParameterValues withGain(float db)
{
    auto values = defaultValues();
    values[kOutputGain] = db;
    return values;
}

} // namespace

TEST_CASE("Historial: recien creado no hay nada que deshacer ni rehacer")
{
    UndoHistory history;
    CHECK_FALSE(history.canUndo());
    CHECK_FALSE(history.canRedo());
    CHECK_FALSE(history.undo().has_value());
    CHECK_FALSE(history.redo().has_value());
    CHECK(history.present() == defaultValues());
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
    auto values = defaultValues();
    history.reset(values);

    values[bandParameter(3, BandParam::Enabled)] = 1.0f;
    values[bandParameter(3, BandParam::Frequency)] = 440.0f;
    values[channelParameter(3)] = 3.0f;
    history.commit(values);

    values[dynamicParameter(3, DynParam::Threshold)] = -40.0f;
    history.commit(values);

    const auto back = history.undo();
    REQUIRE(back.has_value());
    CHECK((*back)[bandParameter(3, BandParam::Frequency)] == 440.0f);
    CHECK((*back)[channelParameter(3)] == 3.0f);
    CHECK((*back)[dynamicParameter(3, DynParam::Threshold)] == parameters()[dynamicParameter(3, DynParam::Threshold)].defaultValue);
    CHECK(history.undo() == defaultValues());
}
