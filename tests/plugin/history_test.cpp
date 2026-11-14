// SPDX-License-Identifier: AGPL-3.0-or-later
#include "test_support.h"

#include "esceq/core/parameters.h"

#include <catch2/catch_test_macros.hpp>

using namespace esceq;
using namespace esceq::test;

namespace {

const std::size_t kFreq = core::bandParameter(0, core::BandParam::Frequency);
const std::size_t kGain = core::bandParameter(0, core::BandParam::Gain);

} // namespace

TEST_CASE("Deshacer: recien creado no hay nada que deshacer")
{
    auto p = makeProcessor();
    settle();
    CHECK_FALSE(p->history().canUndo());
    CHECK_FALSE(p->history().canRedo());
    p->history().undo(); // no hace nada
    CHECK(p->currentValues() == core::defaultValues());
}

TEST_CASE("Deshacer: un arrastre entero es una sola accion")
{
    auto p = makeProcessor();
    const float before = valueOf(*p, kFreq);

    // Un arrastre recorre muchos valores, pero es un solo gesto.
    drag(*p, kFreq, {200.0f, 300.0f, 450.0f, 700.0f, 1500.0f, 2500.0f});
    settle();
    REQUIRE(p->history().canUndo());

    p->history().undo();
    CHECK(valueOf(*p, kFreq) == before);
    CHECK_FALSE(p->history().canUndo()); // no hay acciones intermedias
    REQUIRE(p->history().canRedo());

    p->history().redo();
    CHECK(valueOf(*p, kFreq) == 2500.0f);
    CHECK_FALSE(p->history().canRedo());
}

TEST_CASE("Deshacer: arrastrar un nodo mueve dos parametros y es una sola accion")
{
    auto p = makeProcessor();

    // Como en la curva: dos gestos abiertos a la vez (frecuencia y ganancia).
    auto& freq = p->parameter(kFreq);
    auto& gain = p->parameter(kGain);
    freq.beginChangeGesture();
    gain.beginChangeGesture();
    for (int i = 1; i <= 20; ++i)
    {
        freq.setValueNotifyingHost(freq.convertTo0to1(1000.0f + 100.0f * static_cast<float>(i)));
        gain.setValueNotifyingHost(gain.convertTo0to1(0.5f * static_cast<float>(i)));
    }
    freq.endChangeGesture();
    settle(); // todavia queda un gesto abierto: no se registra nada
    CHECK_FALSE(p->history().canUndo());
    gain.endChangeGesture();
    settle();

    REQUIRE(p->history().canUndo());
    p->history().undo();
    CHECK(valueOf(*p, kFreq) == 1000.0f);
    CHECK(valueOf(*p, kGain) == 0.0f);
    CHECK_FALSE(p->history().canUndo());
}

TEST_CASE("Deshacer: anadir una banda (varios parametros seguidos) es una sola accion")
{
    auto p = makeProcessor();
    using core::BandParam;

    for (const auto& [param, value] : {std::pair{BandParam::Type, 2.0f}, {BandParam::Frequency, 5000.0f},
                                       {BandParam::Gain, 4.0f},  {BandParam::Q, 1.2f},
                                       {BandParam::Enabled, 1.0f}})
        drag(*p, core::bandParameter(3, param), {value});
    settle();

    REQUIRE(p->history().canUndo());
    p->history().undo();
    CHECK(p->currentValues() == core::defaultValues());
    CHECK_FALSE(p->history().canUndo());
}

TEST_CASE("Deshacer: una rafaga de rueda es una accion; dos movimientos separados, dos")
{
    auto p = makeProcessor();

    for (const float q : {0.8f, 0.9f, 1.0f, 1.1f, 1.2f})
        drag(*p, core::bandParameter(0, core::BandParam::Q), {q});
    settle();
    drag(*p, core::bandParameter(0, core::BandParam::Q), {2.0f});
    settle();

    p->history().undo();
    CHECK(valueOf(*p, core::bandParameter(0, core::BandParam::Q)) == 1.2f);
    p->history().undo();
    CHECK(valueOf(*p, core::bandParameter(0, core::BandParam::Q)) == core::parameters()[core::bandParameter(0, core::BandParam::Q)].defaultValue);
    CHECK_FALSE(p->history().canUndo());
}

TEST_CASE("Deshacer: deshacer justo despues de un gesto no pierde la accion")
{
    auto p = makeProcessor();
    drag(*p, kGain, {6.0f});
    // Sin esperar al temporizador: la accion pendiente se registra antes de deshacer.
    pump(20);
    CHECK(p->history().canUndo());
    p->history().undo();
    CHECK(valueOf(*p, kGain) == 0.0f);
    p->history().redo();
    CHECK(valueOf(*p, kGain) == 6.0f);
}

TEST_CASE("Deshacer: deshacer y rehacer no crean acciones nuevas")
{
    auto p = makeProcessor();
    drag(*p, kGain, {3.0f});
    settle();
    drag(*p, kGain, {6.0f});
    settle();

    p->history().undo();
    settle(); // los gestos que hace el propio deshacer no cuentan
    CHECK(valueOf(*p, kGain) == 3.0f);
    CHECK(p->history().canRedo());
    p->history().redo();
    settle();
    CHECK(valueOf(*p, kGain) == 6.0f);
    CHECK_FALSE(p->history().canRedo());
    p->history().undo();
    p->history().undo();
    CHECK(valueOf(*p, kGain) == 0.0f);
    CHECK_FALSE(p->history().canUndo());
}

TEST_CASE("Deshacer: los cambios del host (automatizacion) no crean acciones")
{
    auto p = makeProcessor();
    auto& gain = p->parameter(kGain);
    // El host mueve el parametro sin gestos.
    for (int i = 0; i < 50; ++i)
        gain.setValueNotifyingHost(gain.convertTo0to1(static_cast<float>(i) * 0.1f));
    settle();
    CHECK_FALSE(p->history().canUndo());
}

TEST_CASE("Deshacer: cargar un estado desde el host borra el historial")
{
    auto p = makeProcessor();
    drag(*p, kGain, {3.0f});
    settle();
    REQUIRE(p->history().canUndo());

    juce::MemoryBlock saved;
    p->getStateInformation(saved);
    p->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    settle();
    CHECK_FALSE(p->history().canUndo());
    CHECK_FALSE(p->history().canRedo());
    CHECK(valueOf(*p, kGain) == 3.0f);
}

TEST_CASE("Deshacer: la automatizacion durante un deshacer no crea acciones ni borra el rehacer")
{
    auto p = makeProcessor();
    drag(*p, core::bandParameter(1, core::BandParam::Gain), {6.0f});
    settle();
    drag(*p, core::bandParameter(2, core::BandParam::Gain), {-3.0f});
    settle();
    drag(*p, core::bandParameter(3, core::BandParam::Gain), {2.0f});
    settle();

    // Tres pulsaciones de deshacer mientras el host sigue moviendo la salida.
    float automated = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        p->history().undo();
        auto& output = p->parameter(core::kOutputGain);
        output.setValueNotifyingHost(output.convertTo0to1(automated += 0.5f));
        settle();
    }
    CHECK(valueOf(*p, core::bandParameter(1, core::BandParam::Gain)) == 0.0f);
    CHECK(valueOf(*p, core::bandParameter(2, core::BandParam::Gain)) == 0.0f);
    CHECK(valueOf(*p, core::bandParameter(3, core::BandParam::Gain)) == 0.0f);
    REQUIRE(p->history().canRedo());

    p->history().redo();
    CHECK(valueOf(*p, core::bandParameter(1, core::BandParam::Gain)) == 6.0f);
}

TEST_CASE("Deshacer: un gesto abandonado no bloquea el historial")
{
    auto p = makeProcessor();
    // La interfaz se cierra en mitad de un arrastre: el gesto no se cierra.
    auto& stuck = p->parameter(core::bandParameter(0, core::BandParam::Gain));
    stuck.beginChangeGesture();
    stuck.setValueNotifyingHost(stuck.convertTo0to1(4.0f));
    p->history().abandonOpenGestures();
    settle();
    REQUIRE(p->history().canUndo());

    drag(*p, core::bandParameter(1, core::BandParam::Gain), {6.0f});
    settle();
    p->history().undo();
    CHECK(valueOf(*p, core::bandParameter(1, core::BandParam::Gain)) == 0.0f);
    CHECK(valueOf(*p, core::bandParameter(0, core::BandParam::Gain)) == 4.0f);
}

TEST_CASE("Deshacer: un final de gesto sin principio no descuadra la agrupacion")
{
    auto p = makeProcessor();
    p->parameter(core::bandParameter(5, core::BandParam::Q)).endChangeGesture(); // sin principio

    // Un arrastre de dos parámetros sigue siendo una sola acción.
    auto& freq = p->parameter(core::bandParameter(0, core::BandParam::Frequency));
    auto& gain = p->parameter(core::bandParameter(0, core::BandParam::Gain));
    freq.beginChangeGesture();
    gain.beginChangeGesture();
    freq.setValueNotifyingHost(freq.convertTo0to1(500.0f));
    gain.setValueNotifyingHost(gain.convertTo0to1(3.0f));
    freq.endChangeGesture();
    settle(); // aún queda abierto el de la ganancia: no se registra nada
    CHECK_FALSE(p->history().canUndo());
    gain.setValueNotifyingHost(gain.convertTo0to1(5.0f));
    gain.endChangeGesture();
    settle();
    REQUIRE(p->history().canUndo());
    p->history().undo();
    CHECK_FALSE(p->history().canUndo());
}
