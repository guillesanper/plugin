// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/meter_logic.h"

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
