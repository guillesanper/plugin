// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/curve_mapping.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace esceq;
using namespace esceq::ui;

TEST_CASE("Vista: frecuencias en escala logaritmica")
{
    const CurveMapping m({10.0f, 20.0f, 300.0f, 200.0f}, 12.0);

    CHECK_THAT(m.xForFrequency(20.0), WithinAbs(10.0, 1e-4));
    CHECK_THAT(m.xForFrequency(20000.0), WithinAbs(310.0, 1e-3));
    CHECK_THAT(m.xForFrequency(200.0), WithinAbs(110.0, 1e-3)); // una década = un tercio
    CHECK_THAT(m.xForFrequency(2000.0), WithinAbs(210.0, 1e-3));

    for (double f : {20.0, 55.0, 1000.0, 12345.0, 20000.0})
        CHECK_THAT(m.frequencyForX(m.xForFrequency(f)), WithinRel(f, 1e-5));
}

TEST_CASE("Vista: ganancia lineal y analizador con su propia escala")
{
    const CurveMapping m({0.0f, 0.0f, 100.0f, 240.0f}, 12.0);

    CHECK_THAT(m.yForGain(12.0), WithinAbs(0.0, 1e-4));
    CHECK_THAT(m.yForGain(0.0), WithinAbs(120.0, 1e-4));
    CHECK_THAT(m.yForGain(-12.0), WithinAbs(240.0, 1e-4));
    CHECK_THAT(m.gainForY(m.yForGain(-5.5)), WithinAbs(-5.5, 1e-5));

    CHECK_THAT(m.yForAnalyzerDb(0.0), WithinAbs(0.0, 1e-4));
    CHECK_THAT(m.yForAnalyzerDb(-90.0), WithinAbs(240.0, 1e-4));
}

TEST_CASE("Vista: posicion de los nodos")
{
    const CurveMapping m({0.0f, 0.0f, 300.0f, 200.0f}, 10.0);

    const dsp::BandSettings bell{true, {dsp::FilterType::Bell, 200.0, 5.0, 1.0}};
    const auto p = nodePosition(m, bell);
    CHECK_THAT(p.x, WithinAbs(100.0, 1e-3));
    CHECK_THAT(p.y, WithinAbs(50.0, 1e-3));

    // Un corte no usa ganancia: su nodo va en la línea de 0 dB.
    const dsp::BandSettings cut{true, {dsp::FilterType::LowCut, 200.0, 5.0, 1.0}};
    CHECK_THAT(nodePosition(m, cut).y, WithinAbs(100.0, 1e-3));
}

TEST_CASE("Vista: un nodo con mas ganancia que el rango visible queda en el borde")
{
    // Error encontrado en FL Studio: arrastrando hacia abajo, la ganancia
    // llegaba a -30 dB con la vista en ±18 dB y el nodo desaparecía.
    const CurveMapping m({0.0f, 10.0f, 300.0f, 200.0f}, 18.0);

    const dsp::BandSettings low{true, {dsp::FilterType::Bell, 1000.0, -30.0, 1.0}};
    CHECK_THAT(nodePosition(m, low).y, WithinAbs(210.0, 1e-4));
    const dsp::BandSettings high{true, {dsp::FilterType::Bell, 1000.0, 30.0, 1.0}};
    CHECK_THAT(nodePosition(m, high).y, WithinAbs(10.0, 1e-4));

    // Y en el borde se puede seguir seleccionando.
    const std::array<Point, 1> nodes{nodePosition(m, low)};
    const std::array<bool, 1> visible{true};
    CHECK(hitTestNodes(nodes, visible, {nodes[0].x, 205.0f}, 12.0f) == 0);
}

TEST_CASE("Vista: arrastrar no saca un nodo de la vista")
{
    const CurveMapping m({0.0f, 0.0f, 300.0f, 360.0f}, 18.0); // 0,1 dB por píxel

    CHECK_THAT(draggedGain(m, 0.0, 30.0f), WithinAbs(-3.0, 1e-9));
    CHECK_THAT(draggedGain(m, 0.0, -30.0f), WithinAbs(3.0, 1e-9));
    CHECK(draggedGain(m, -17.0, 500.0f) == -18.0); // se para en el borde
    CHECK(draggedGain(m, 17.0, -500.0f) == 18.0);

    // Si ya estaba fuera (panel, automatización o vista más estrecha), puede
    // volver sin saltos, pero no alejarse más.
    CHECK_THAT(draggedGain(m, -30.0, -10.0f), WithinAbs(-29.0, 1e-9));
    CHECK(draggedGain(m, -30.0, 10.0f) == -30.0);
}

TEST_CASE("Vista: seleccion del nodo mas cercano")
{
    const std::array<Point, 3> nodes{{{10.0f, 10.0f}, {20.0f, 10.0f}, {20.0f, 10.0f}}};
    std::array<bool, 3> visible{true, true, false};

    CHECK(hitTestNodes(nodes, visible, {11.0f, 10.0f}, 5.0f) == 0);
    CHECK(hitTestNodes(nodes, visible, {18.0f, 10.0f}, 5.0f) == 1);
    CHECK_FALSE(hitTestNodes(nodes, visible, {50.0f, 50.0f}, 5.0f).has_value());

    // A igual distancia gana el que se dibuja encima (índice mayor) si es visible.
    visible[2] = true;
    CHECK(hitTestNodes(nodes, visible, {20.0f, 10.0f}, 5.0f) == 2);
}

TEST_CASE("Vista: rejillas y frecuencias de muestreo de la curva")
{
    const auto freqs = logFrequencies(4);
    REQUIRE(freqs.size() == 4);
    CHECK_THAT(freqs.front(), WithinRel(20.0, 1e-9));
    CHECK_THAT(freqs[1], WithinRel(200.0, 1e-9));
    CHECK_THAT(freqs.back(), WithinRel(20000.0, 1e-9));

    const auto grid = frequencyGrid();
    CHECK(grid.front().value == 20.0);
    CHECK(grid.back().value == 20000.0);

    const auto gains = gainGrid(12.0);
    CHECK(gains.front().value == -12.0);
    CHECK(gains.back().value == 12.0);
    CHECK(gains.size() == 9); // pasos de 3 dB
}
