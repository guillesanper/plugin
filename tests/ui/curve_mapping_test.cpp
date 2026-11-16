// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/curve_mapping.h"

#include "esceq/core/band_model.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>

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

TEST_CASE("Vista: indicador de ganancia fuera del rango visible")
{
    const CurveMapping m({0.0f, 0.0f, 400.0f, 360.0f}, 18.0);

    CHECK(gainOverflow(m, {true, {dsp::FilterType::Bell, 1000.0, 24.0, 1.0}}) == 1);
    CHECK(gainOverflow(m, {true, {dsp::FilterType::Bell, 1000.0, -30.0, 1.0}}) == -1);
    CHECK(gainOverflow(m, {true, {dsp::FilterType::Bell, 1000.0, 12.0, 1.0}}) == 0);
    // Justo en el borde no se considera fuera.
    CHECK(gainOverflow(m, {true, {dsp::FilterType::Bell, 1000.0, 18.0, 1.0}}) == 0);
    CHECK(gainOverflow(m, {true, {dsp::FilterType::Bell, 1000.0, -18.0, 1.0}}) == 0);
    // Los tipos sin ganancia nunca se salen: su nodo va en la linea de 0 dB.
    CHECK(gainOverflow(m, {true, {dsp::FilterType::LowCut, 1000.0, 30.0, 1.0}}) == 0);
    // Con la vista ampliada, la misma banda ya se ve.
    CHECK(gainOverflow(CurveMapping({0.0f, 0.0f, 400.0f, 360.0f}, 30.0), {true, {dsp::FilterType::Bell, 1000.0, 24.0, 1.0}}) == 0);
}

TEST_CASE("Vista: la etiqueta flotante va sobre el nodo y nunca se sale de la curva")
{
    const Rect area{10.0f, 20.0f, 400.0f, 300.0f};

    // En mitad de la vista: centrada sobre el nodo, a la distancia pedida.
    auto label = floatingLabelRect(area, {200.0f, 150.0f}, 120.0f, 40.0f, 12.0f);
    CHECK(label.x == 140.0f);
    CHECK(label.y == 150.0f - 12.0f - 40.0f);
    CHECK(label.width == 120.0f);
    CHECK(label.height == 40.0f);

    // Nodo arriba del todo: la etiqueta pasa debajo.
    label = floatingLabelRect(area, {200.0f, 30.0f}, 120.0f, 40.0f, 12.0f);
    CHECK(label.y == 42.0f);

    // Nodos en las esquinas: dentro del area, y sin tapar el nodo.
    for (const Point node : {Point{10.0f, 20.0f}, Point{410.0f, 20.0f}, Point{10.0f, 320.0f}, Point{410.0f, 320.0f},
                             Point{200.0f, 320.0f}, Point{12.0f, 150.0f}})
    {
        INFO("nodo en " << node.x << ", " << node.y);
        label = floatingLabelRect(area, node, 120.0f, 40.0f, 12.0f);
        CHECK(label.x >= area.x);
        CHECK(label.y >= area.y);
        CHECK(label.x + label.width <= area.x + area.width);
        CHECK(label.y + label.height <= area.y + area.height);
        const bool coversNode = node.y > label.y && node.y < label.y + label.height;
        CHECK_FALSE(coversNode);
    }

    // Una etiqueta mayor que el area se recorta a ella.
    label = floatingLabelRect({0.0f, 0.0f, 50.0f, 20.0f}, {25.0f, 10.0f}, 120.0f, 40.0f, 12.0f);
    CHECK(label.width == 50.0f);
    CHECK(label.height == 20.0f);
}

TEST_CASE("Vista: textos de la etiqueta de un nodo")
{
    auto values = core::defaultValues();
    auto set = [&](int band, core::BandParam p, float v) { values[core::bandParameter(band, p)] = v; };

    set(2, core::BandParam::Frequency, 1200.0f);
    set(2, core::BandParam::Gain, 3.0f);
    set(2, core::BandParam::Q, 1.5f);
    auto readout = nodeReadout(values, 2);
    CHECK(readout.title == "Band 3 \xC2\xB7 Bell");
    CHECK(readout.frequency == "1.20 kHz");
    CHECK(readout.gain == "+3.0 dB");
    CHECK(readout.q == "Q 1.50");

    // El canal aparece solo cuando no es estereo.
    values[core::channelParameter(2)] = 3.0f;
    CHECK(nodeReadout(values, 2).title == "Band 3 \xC2\xB7 Bell \xC2\xB7 Mid");

    // Corte de 24 dB/oct: sin ganancia, con pendiente y Q.
    set(0, core::BandParam::Type, 3.0f);
    set(0, core::BandParam::Slope, 3.0f);
    set(0, core::BandParam::Frequency, 80.0f);
    readout = nodeReadout(values, 0);
    CHECK(readout.title == "Band 1 \xC2\xB7 Low Cut");
    CHECK(readout.frequency == "80.0 Hz");
    CHECK(readout.gain.empty());
    CHECK(readout.q == "24 dB/oct  Q 0.71");

    // Corte de 6 dB/oct: el Q no influye.
    set(0, core::BandParam::Slope, 0.0f);
    CHECK(nodeReadout(values, 0).q == "6 dB/oct");

    // Tilt: con ganancia y sin Q.
    set(1, core::BandParam::Type, 8.0f);
    readout = nodeReadout(values, 1);
    CHECK(readout.gain == "0.0 dB");
    CHECK(readout.q.empty());
}

TEST_CASE("Vista: con margen, el nodo fuera de rango queda entero dentro de la vista")
{
    const CurveMapping m({0.0f, 10.0f, 400.0f, 360.0f}, 18.0);
    const dsp::BandSettings high{true, {dsp::FilterType::Bell, 1000.0, 30.0, 1.0}};
    const dsp::BandSettings low{true, {dsp::FilterType::Bell, 1000.0, -30.0, 1.0}};
    const dsp::BandSettings inside{true, {dsp::FilterType::Bell, 1000.0, 3.0, 1.0}};

    CHECK(nodePosition(m, high, 18.0f).y == 28.0f);
    CHECK(nodePosition(m, low, 18.0f).y == 352.0f);
    // Un nodo que se ve no se mueve por el margen.
    CHECK(nodePosition(m, inside, 18.0f).y == nodePosition(m, inside).y);
    // Sin margen, el comportamiento de siempre: pegado al borde.
    CHECK(nodePosition(m, high).y == 10.0f);
    // Un margen absurdo no cruza el centro ni produce valores no finitos.
    CHECK(nodePosition(m, high, 5000.0f).y == 190.0f);
    CHECK(nodePosition(m, high, -5.0f).y == 10.0f);
}

TEST_CASE("Vista: una banda nueva no hereda la dinamica ni el canal de antes")
{
    using core::BandParam;
    using core::DynParam;
    auto values = core::defaultValues();
    const int band = 4;
    // La banda estuvo activa como corte dinámico en Side, y se desactivó.
    values[core::bandParameter(band, BandParam::Type)] = 3.0f;
    values[core::bandParameter(band, BandParam::Q)] = 5.0f;
    values[core::bandParameter(band, BandParam::Slope)] = 7.0f;
    values[core::dynamicParameter(band, DynParam::Enabled)] = 1.0f;
    values[core::dynamicParameter(band, DynParam::Threshold)] = -50.0f;
    values[core::dynamicParameter(band, DynParam::Source)] = 1.0f;
    values[core::channelParameter(band)] = 4.0f;

    const auto changes = newBandChanges(values, band, 2500.0, -4.0);
    REQUIRE_FALSE(changes.empty());
    CHECK(changes.back() == ParameterChange{core::bandParameter(band, BandParam::Enabled), 1.0f});

    for (const auto& change : changes)
        values[change.index] = change.value;
    const auto settings = core::bandSettings(values, band);
    CHECK(settings.enabled);
    CHECK(settings.filter.type == dsp::FilterType::Bell);
    CHECK_THAT(settings.filter.frequencyHz, WithinRel(2500.0, 1e-6));
    CHECK(settings.filter.gainDb == -4.0);
    CHECK(settings.channel == dsp::ChannelMode::Stereo);
    CHECK_FALSE(settings.dynamics.enabled);
    CHECK_FALSE(settings.dynamics.external);
    for (int i = 0; i < 6; ++i)
        if (i != static_cast<int>(BandParam::Frequency) && i != static_cast<int>(BandParam::Gain) &&
            i != static_cast<int>(BandParam::Enabled))
        {
            const auto index = core::bandParameter(band, static_cast<BandParam>(i));
            CHECK(values[index] == core::parameters()[index].defaultValue);
        }
    for (int i = 0; i < 7; ++i)
    {
        const auto index = core::dynamicParameter(band, static_cast<DynParam>(i));
        CHECK(values[index] == core::parameters()[index].defaultValue);
    }
}

TEST_CASE("Vista: una banda nueva solo cambia lo que hace falta")
{
    const auto values = core::defaultValues();
    const auto changes = newBandChanges(values, 0, 1000.0, 0.0); // todo ya está por defecto
    REQUIRE(changes.size() == 1);
    CHECK(changes[0] == ParameterChange{core::bandParameter(0, core::BandParam::Enabled), 1.0f});
}

TEST_CASE("Vista: las flechas eligen la banda activa siguiente o anterior, dando la vuelta")
{
    auto values = core::defaultValues();
    CHECK_FALSE(stepSelectedBand(values, -1, 1).has_value()); // ninguna activa

    for (const int band : {2, 7, 20})
        values[core::bandParameter(band, core::BandParam::Enabled)] = 1.0f;

    CHECK(stepSelectedBand(values, -1, 1) == 2);   // sin elegir: la primera
    CHECK(stepSelectedBand(values, -1, -1) == 20); // sin elegir: la última
    CHECK(stepSelectedBand(values, 2, 1) == 7);
    CHECK(stepSelectedBand(values, 7, -1) == 2);
    CHECK(stepSelectedBand(values, 20, 1) == 2);  // da la vuelta
    CHECK(stepSelectedBand(values, 2, -1) == 20);
    CHECK(stepSelectedBand(values, 5, 1) == 7);   // desde una apagada
    CHECK(stepSelectedBand(values, 5, -1) == 2);

    values[core::bandParameter(7, core::BandParam::Enabled)] = 0.0f;
    values[core::bandParameter(20, core::BandParam::Enabled)] = 0.0f;
    CHECK(stepSelectedBand(values, 2, 1) == 2); // la única activa
}

TEST_CASE("Vista: la rueda multiplica la Q por 2^1,5 por paso")
{
    CHECK_THAT(wheelQ(1.0, 1.0), WithinRel(std::pow(2.0, 1.5), 1e-12));
    CHECK_THAT(wheelQ(2.0, -1.0), WithinRel(2.0 / std::pow(2.0, 1.5), 1e-12));
    CHECK(wheelQ(0.7, 0.0) == 0.7);
}

TEST_CASE("Vista: la curva se dibuja con el algoritmo de filtro de la instancia")
{
    using core::BandParam;
    using core::DynParam;
    auto values = core::defaultValues();
    values[core::bandParameter(0, BandParam::Enabled)] = 1.0f;
    values[core::bandParameter(0, BandParam::Frequency)] = 18000.0f;
    values[core::bandParameter(0, BandParam::Gain)] = 12.0f;
    values[core::bandParameter(0, BandParam::Q)] = 2.0f;

    constexpr double kRate = 44100.0;
    for (const auto algorithm : {dsp::FilterAlgorithm::Classic, dsp::FilterAlgorithm::Matched})
    {
        INFO(static_cast<int>(algorithm));
        // Lo mismo que recibe el procesador: la curva es lo que suena.
        const auto drawn = soundingBandSettings(values, 0, algorithm, 0.0);
        CHECK(drawn.filter == core::bandSettings(values, 0, algorithm).filter);
    }
    // Y los dos algoritmos se dibujan distintos cerca de Nyquist.
    const auto classic =
        dsp::designBand(soundingBandSettings(values, 0, dsp::FilterAlgorithm::Classic, 0.0).filter, kRate);
    const auto matched =
        dsp::designBand(soundingBandSettings(values, 0, dsp::FilterAlgorithm::Matched, 0.0).filter, kRate);
    CHECK(std::abs(dsp::magnitudeDb(classic, 20000.0, kRate) - dsp::magnitudeDb(matched, 20000.0, kRate)) > 1.0);
}

TEST_CASE("Vista: la curva de una banda dinamica suma la ganancia dinamica")
{
    using core::BandParam;
    using core::DynParam;
    auto values = core::defaultValues();
    values[core::bandParameter(1, BandParam::Enabled)] = 1.0f;
    values[core::bandParameter(1, BandParam::Gain)] = 2.0f;
    constexpr auto kAlgorithm = dsp::FilterAlgorithm::Matched;

    // Sin dinamica, la ganancia dinamica no cuenta.
    CHECK(soundingBandSettings(values, 1, kAlgorithm, -5.0).filter.gainDb == 2.0);

    values[core::dynamicParameter(1, DynParam::Enabled)] = 1.0f;
    const auto dynamic = soundingBandSettings(values, 1, kAlgorithm, -5.0);
    CHECK(dynamic.filter.gainDb == -3.0);
    CHECK(dynamic.filter.algorithm == kAlgorithm);

    // En un tipo sin ganancia (un corte) la dinamica no actua.
    values[core::bandParameter(1, BandParam::Type)] = 3.0f;
    REQUIRE(core::bandSettings(values, 1).filter.type == dsp::FilterType::LowCut);
    CHECK(soundingBandSettings(values, 1, kAlgorithm, -5.0).filter.gainDb == 2.0);
}
