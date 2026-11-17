// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/meter_logic.h"

#include "esceq/dsp/spectrum_analyzer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq::ui;

TEST_CASE("Medidor: escala por tramos, creciente y recortada")
{
    CHECK(meterFraction(kMeterBottomDb) == 0.0f);
    CHECK(meterFraction(-200.0) == 0.0f);
    CHECK(meterFraction(kMeterTopDb) == 1.0f);
    CHECK(meterFraction(40.0) == 1.0f);
    CHECK(meterFraction(std::numeric_limits<double>::quiet_NaN()) == 0.0f);
    CHECK(meterFraction(-std::numeric_limits<double>::infinity()) == 0.0f);

    // -24 dB queda al 30 %; la zona util ocupa el 70 % superior.
    CHECK_THAT(meterFraction(-24.0), WithinAbs(0.3, 1e-6));
    CHECK_THAT(meterFraction(-9.0), WithinAbs(0.65, 1e-6));
    CHECK_THAT(meterFraction(-42.0), WithinAbs(0.15, 1e-6));

    // Estrictamente creciente y continua.
    float previous = meterFraction(kMeterBottomDb);
    for (double db = kMeterBottomDb + 0.25; db <= kMeterTopDb; db += 0.25)
    {
        const float f = meterFraction(db);
        CHECK(f > previous);
        CHECK(f - previous < 0.01f);
        previous = f;
    }
}

TEST_CASE("Medidor: marcas de la escala, de arriba abajo y dentro del medidor")
{
    const auto ticks = meterTicks();
    REQUIRE(ticks.size() >= 5);
    bool hasZero = false;
    for (std::size_t i = 0; i < ticks.size(); ++i)
    {
        CHECK(ticks[i].fraction == meterFraction(ticks[i].db));
        CHECK(ticks[i].fraction > 0.0f);
        CHECK(ticks[i].fraction <= 1.0f);
        if (i > 0)
            CHECK(ticks[i].db < ticks[i - 1].db);
        hasZero = hasZero || (ticks[i].db == 0.0 && ticks[i].labelled);
    }
    CHECK(hasZero);
}

TEST_CASE("Medidor: sube al instante y baja a velocidad constante")
{
    PeakMeter meter;
    CHECK(meter.levelDb() == PeakMeter::kSilenceDb);

    meter.update(0.5, 0.033);
    CHECK_THAT(meter.levelDb(), WithinAbs(-6.0206, 1e-3));

    // Medio segundo en silencio: baja 12 dB.
    for (int i = 0; i < 50; ++i)
        meter.update(0.0, 0.01);
    CHECK_THAT(meter.levelDb(), WithinAbs(-6.0206 - 0.5 * PeakMeter::kFallDbPerSecond, 1e-3));

    // Un pico mas alto lo sube de golpe; uno mas bajo no lo baja de golpe.
    meter.update(1.0, 0.01);
    CHECK_THAT(meter.levelDb(), WithinAbs(0.0, 1e-9));
    meter.update(0.1, 0.01);
    CHECK_THAT(meter.levelDb(), WithinAbs(-0.24, 1e-6));

    // No baja del silencio.
    for (int i = 0; i < 1000; ++i)
        meter.update(0.0, 0.1);
    CHECK(meter.levelDb() == PeakMeter::kSilenceDb);
}

TEST_CASE("Medidor: el pico retenido aguanta y luego cae, sin bajar del nivel")
{
    PeakMeter meter;
    meter.update(0.5, 0.01);
    const double peak = meter.holdDb();
    CHECK_THAT(peak, WithinAbs(-6.0206, 1e-3));

    // Durante la retencion no se mueve, aunque el nivel baje.
    for (int i = 0; i < 100; ++i)
        meter.update(0.0, 0.01);
    CHECK(meter.holdDb() == peak);
    CHECK(meter.levelDb() < peak);

    // Pasada la retencion, cae.
    for (int i = 0; i < 100; ++i)
        meter.update(0.0, 0.01);
    CHECK(meter.holdDb() < peak);
    CHECK(meter.holdDb() >= meter.levelDb());

    // Un pico nuevo mas alto lo renueva.
    meter.update(0.9, 0.01);
    CHECK_THAT(meter.holdDb(), WithinAbs(20.0 * std::log10(0.9), 1e-6));
}

TEST_CASE("Medidor: el aviso de recorte se queda hasta que se borra")
{
    PeakMeter meter;
    meter.update(0.99, 0.01);
    CHECK_FALSE(meter.clipped());
    meter.update(1.0, 0.01); // 0 dBFS exactos no es recorte
    CHECK_FALSE(meter.clipped());
    meter.update(1.01, 0.01);
    CHECK(meter.clipped());
    for (int i = 0; i < 500; ++i)
        meter.update(0.0, 0.01);
    CHECK(meter.clipped());
    meter.resetClip();
    CHECK_FALSE(meter.clipped());
}

TEST_CASE("Medidor: entradas absurdas no producen NaN")
{
    PeakMeter meter;
    meter.update(std::numeric_limits<double>::quiet_NaN(), 0.01);
    meter.update(-0.5, std::numeric_limits<double>::quiet_NaN());
    meter.update(0.25, -3.0);
    CHECK(std::isfinite(meter.levelDb()));
    CHECK(std::isfinite(meter.holdDb()));
    CHECK_THAT(meter.levelDb(), WithinAbs(-6.0206, 1e-3)); // el pico negativo cuenta por su valor absoluto
}

TEST_CASE("Medidor de dinamica: fraccion del rango que se esta usando")
{
    // Reduccion: rango negativo.
    CHECK(dynamicMeterFraction(0.0, -12.0) == 0.0f);
    CHECK_THAT(dynamicMeterFraction(-3.0, -12.0), WithinAbs(0.25, 1e-6));
    CHECK(dynamicMeterFraction(-12.0, -12.0) == 1.0f);
    CHECK(dynamicMeterFraction(-20.0, -12.0) == 1.0f);

    // Realce: rango positivo.
    CHECK_THAT(dynamicMeterFraction(4.5, 9.0), WithinAbs(0.5, 1e-6));

    // Ganancia de signo contrario al rango, rango nulo o valores no finitos: nada.
    CHECK(dynamicMeterFraction(3.0, -12.0) == 0.0f);
    CHECK(dynamicMeterFraction(-3.0, 0.0) == 0.0f);
    CHECK(dynamicMeterFraction(std::numeric_limits<double>::quiet_NaN(), -12.0) == 0.0f);
}

TEST_CASE("Analizador: el primer dato tras el silencio se toma tal cual")
{
    constexpr float kFloor = -120.0f;
    SpectrumBallistics b(3, kFloor);
    CHECK(b.levels() == std::vector<float>{kFloor, kFloor, kFloor});
    // Aunque no haya pasado tiempo: sin transición desde el silencio.
    CHECK(b.update({-30.0f, -40.0f, kFloor}, 0.0));
    CHECK(b.levels() == std::vector<float>{-30.0f, -40.0f, kFloor});
}

TEST_CASE("Analizador: sube rapido y baja despacio, con constantes de tiempo exponenciales")
{
    SpectrumBallistics b(2, -120.0f);
    b.update({-40.0f, -40.0f}, 0.0);

    // Tras una constante de tiempo, cada punto ha recorrido 1 - 1/e del camino.
    const float path = 1.0f - std::exp(-1.0f);
    b.update({-20.0f, -40.0f}, SpectrumBallistics::kRiseSeconds);
    CHECK_THAT(b.levels()[0], WithinAbs(-40.0 + 20.0 * path, 1e-4));
    CHECK(b.levels()[1] == -40.0f);

    SpectrumBallistics c(1, -120.0f);
    c.update({-20.0f}, 0.0);
    c.update({-40.0f}, SpectrumBallistics::kFallSeconds);
    CHECK_THAT(c.levels()[0], WithinAbs(-20.0 - 20.0 * path, 1e-4));

    // En el mismo tiempo, subir avanza más que bajar.
    SpectrumBallistics up(1, -120.0f), down(1, -120.0f);
    up.update({-40.0f}, 0.0);
    down.update({-20.0f}, 0.0);
    up.update({-20.0f}, 0.033);
    down.update({-40.0f}, 0.033);
    CHECK(up.levels()[0] - (-40.0f) > (-20.0f) - down.levels()[0]);
}

TEST_CASE("Analizador: solo pide repintar si algun punto se mueve lo bastante para verse")
{
    SpectrumBallistics b(2, -120.0f);
    b.update({-40.0f, -40.0f}, 0.0);
    CHECK_FALSE(b.update({-40.0f, -40.0f}, 0.033)); // nada cambia
    CHECK_FALSE(b.update({-40.0f, -40.01f}, 0.033)); // menos de kRepaintDb
    CHECK(b.update({-40.0f, -30.0f}, 0.033));
    CHECK_FALSE(b.update({-40.0f, -30.0f}, 0.0)); // sin tiempo no se mueve
}

TEST_CASE("Analizador: la caida se puede elegir y la subida no cambia")
{
    CHECK(SpectrumBallistics(1, -120.0f).fallSeconds() == SpectrumBallistics::kFallSeconds);
    const float path = 1.0f - std::exp(-1.0f);
    for (const double fall : {1.0, 0.35, 0.12})
    {
        INFO(fall);
        SpectrumBallistics b(2, -120.0f);
        b.setFallSeconds(fall);
        CHECK(b.fallSeconds() == fall);
        b.update({-20.0f, -40.0f}, 0.0);
        b.update({-40.0f, -40.0f}, fall);
        CHECK_THAT(b.levels()[0], WithinAbs(-20.0 - 20.0 * path, 1e-4));

        SpectrumBallistics up(1, -120.0f);
        up.setFallSeconds(fall);
        up.update({-40.0f}, 0.0);
        up.update({-20.0f}, SpectrumBallistics::kRiseSeconds);
        CHECK_THAT(up.levels()[0], WithinAbs(-40.0 + 20.0 * path, 1e-4));
    }

    // Lo que no es una constante de tiempo valida no cambia nada.
    SpectrumBallistics b(1, -120.0f);
    b.setFallSeconds(0.0);
    b.setFallSeconds(-1.0);
    b.setFallSeconds(std::numeric_limits<double>::quiet_NaN());
    CHECK(b.fallSeconds() == SpectrumBallistics::kFallSeconds);
}

TEST_CASE("Analizador: los ajustes guardados se traducen a la FFT y la caida")
{
    CHECK(analyzerFftOrder(4096) == 12);
    CHECK(analyzerFftOrder(8192) == 13);
    CHECK(analyzerFftOrder(16384) == 14);
    CHECK(analyzerFftOrder(1000) == 12); // lo que no vale, la de siempre
    CHECK(analyzerFallSeconds(1000) == 1.0);
    CHECK(analyzerFallSeconds(350) == 0.35);
    CHECK(analyzerFallSeconds(120) == 0.12);
    CHECK(analyzerFallSeconds(7) == SpectrumBallistics::kFallSeconds);
}

TEST_CASE("Analizador: mantener picos guarda el maximo de cada punto hasta borrarlo")
{
    constexpr float kFloor = -120.0f;
    SpectrumPeakHold peaks(3, kFloor);
    CHECK(peaks.levels() == std::vector<float>{kFloor, kFloor, kFloor});
    CHECK_FALSE(peaks.hasPeaks());

    CHECK(peaks.update({-30.0f, -50.0f, kFloor}));
    CHECK(peaks.hasPeaks());
    CHECK_FALSE(peaks.update({-40.0f, -60.0f, kFloor})); // nada sube: no hay que repintar
    CHECK(peaks.levels() == std::vector<float>{-30.0f, -50.0f, kFloor});
    CHECK_FALSE(peaks.update({-30.0f, -49.99f, kFloor})); // menos de lo que se ve
    CHECK(peaks.update({-35.0f, -20.0f, -70.0f}));
    CHECK(peaks.levels() == std::vector<float>{-30.0f, -20.0f, -70.0f});

    peaks.clear();
    CHECK(peaks.levels() == std::vector<float>{kFloor, kFloor, kFloor});
    CHECK_FALSE(peaks.hasPeaks());
    CHECK(peaks.update({-45.0f, kFloor, kFloor}));
    CHECK(peaks.levels()[0] == -45.0f);

    // Un dato mas corto no se sale.
    CHECK_FALSE(peaks.update({-90.0f}));
}

TEST_CASE("Picos: el promedio del espectro empieza con el primer dato y sigue la potencia con su constante")
{
    SpectrumAverage average(3);
    CHECK(average.empty());
    CHECK(average.levels() == std::vector<float>(3, esceq::dsp::SpectrumAnalyzer::kFloorDb));

    // Sin datos, toma el primero tal cual.
    average.update({-60.0f, -20.0f, 0.0f}, 0.5);
    CHECK_FALSE(average.empty());
    CHECK_THAT(average.levels()[0], WithinAbs(-60.0, 1e-4));
    CHECK_THAT(average.levels()[1], WithinAbs(-20.0, 1e-4));
    CHECK_THAT(average.levels()[2], WithinAbs(0.0, 1e-4));

    // Tras una constante de tiempo, el 63 % del camino en potencia (no en dB),
    // igual al subir que al bajar.
    average.update({-40.0f, -40.0f, -60.0f}, SpectrumAverage::kSeconds);
    const double k = 1.0 - std::exp(-1.0);
    auto expected = [k](double from, double to) {
        const double power =
            std::pow(10.0, from / 10.0) + k * (std::pow(10.0, to / 10.0) - std::pow(10.0, from / 10.0));
        return 10.0 * std::log10(power);
    };
    CHECK_THAT(average.levels()[0], WithinAbs(expected(-60.0, -40.0), 1e-3));
    CHECK_THAT(average.levels()[1], WithinAbs(expected(-20.0, -40.0), 1e-3));
    CHECK_THAT(average.levels()[2], WithinAbs(expected(0.0, -60.0), 1e-3));

    // Con un dato constante, llega a él.
    for (int i = 0; i < 900; ++i)
        average.update({-30.0f, -30.0f, -30.0f}, 1.0 / 30.0);
    for (const float level : average.levels())
        CHECK_THAT(level, WithinAbs(-30.0, 1e-3));
}

TEST_CASE("Picos: el promedio se olvida con clear e ignora datos de otro tamano y valores no finitos")
{
    SpectrumAverage average(2);
    average.update({-10.0f, -10.0f}, 0.1);
    average.update({-50.0f}, 0.1); // otro tamaño: nada
    CHECK_THAT(average.levels()[0], WithinAbs(-10.0, 1e-4));

    average.clear();
    CHECK(average.empty());
    CHECK(average.levels()[0] == esceq::dsp::SpectrumAnalyzer::kFloorDb);
    average.update({-70.0f, std::numeric_limits<float>::quiet_NaN()}, 0.1);
    CHECK_THAT(average.levels()[0], WithinAbs(-70.0, 1e-4)); // empieza de cero
    CHECK(average.levels()[1] == esceq::dsp::SpectrumAnalyzer::kFloorDb);

    // Un dt no finito o negativo no mueve nada.
    average.update({0.0f, 0.0f}, std::numeric_limits<double>::quiet_NaN());
    average.update({0.0f, 0.0f}, -1.0);
    CHECK_THAT(average.levels()[0], WithinAbs(-70.0, 1e-4));
}
