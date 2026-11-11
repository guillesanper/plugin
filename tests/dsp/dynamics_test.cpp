// SPDX-License-Identifier: MIT
#include "esceq/dsp/dynamics.h"
#include "esceq/dsp/equalizer.h"

#include "filter_test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;
using namespace esceq::test;

namespace
{

constexpr double kFs = 48000.0;

std::vector<float> sine(int numSamples, double hz, double amplitude)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kFs));
    return out;
}

/// Pico de la señal entre `from` y el final, en dBFS.
double peakDb(const std::vector<float>& signal, std::size_t from)
{
    float peak = 0.0f;
    for (std::size_t i = from; i < signal.size(); ++i)
        peak = std::max(peak, std::abs(signal[i]));
    return 20.0 * std::log10(std::max(static_cast<double>(peak), 1.0e-12));
}

float maxStep(const std::vector<float>& signal)
{
    float worst = 0.0f;
    for (std::size_t i = 1; i < signal.size(); ++i)
        worst = std::max(worst, std::abs(signal[i] - signal[i - 1]));
    return worst;
}

/// Pico, en dB, de lo que hay por encima de 8 kHz desde la muestra `from`.
/// Un seno grave no tiene nada ahí: lo que aparezca es el "chasquido" de un
/// cambio demasiado brusco. Detecta saltos que el paso entre muestras no ve,
/// porque el filtro SVF reparte un salto de coeficientes en varias muestras.
double clickDb(std::vector<float> signal, std::size_t from)
{
    BandFilter highPass;
    highPass.setDesign(designBand({FilterType::LowCut, 8000.0, 0.0, kButterworthQ, 48}, kFs));
    float* channel = signal.data();
    highPass.process(&channel, 1, static_cast<int>(signal.size()));
    return peakDb(signal, from);
}

/// Procesa `signal` (mono) en bloques, con un sidechain mono opcional.
std::vector<float> run(Equalizer& eq, std::vector<float> signal, int blockSize,
                       const std::vector<float>* sidechain = nullptr)
{
    const int total = static_cast<int>(signal.size());
    for (int pos = 0; pos < total; pos += blockSize)
    {
        float* channel = signal.data() + pos;
        const float* sc = sidechain != nullptr ? sidechain->data() + pos : nullptr;
        eq.process(&channel, 1, std::min(blockSize, total - pos), sidechain != nullptr ? &sc : nullptr,
                   sidechain != nullptr ? 1 : 0);
    }
    return signal;
}

BandSettings dynamicBell(double hz, double q, double thresholdDb, double rangeDb, double kneeDb = 0.0,
                         double attackMs = 1.0, double releaseMs = 100.0, bool external = false)
{
    BandSettings s;
    s.enabled = true;
    s.filter = {FilterType::Bell, hz, 0.0, q};
    s.dynamics = {true, thresholdDb, rangeDb, kneeDb, attackMs, releaseMs, external};
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// Curva de ganancia

TEST_CASE("Dinamica: curva de ganancia con rodilla dura")
{
    CHECK(dynamicGainDb(-30.0, -20.0, -12.0, 0.0) == 0.0);
    CHECK(dynamicGainDb(-20.0, -20.0, -12.0, 0.0) == 0.0);
    CHECK(dynamicGainDb(-15.0, -20.0, -12.0, 0.0) == -5.0); // exceso 1:1
    CHECK(dynamicGainDb(0.0, -20.0, -12.0, 0.0) == -12.0);  // limitado por el rango
    CHECK(dynamicGainDb(-15.0, -20.0, 6.0, 0.0) == 5.0);    // rango positivo: expande
    CHECK(dynamicGainDb(0.0, -20.0, 6.0, 0.0) == 6.0);
    CHECK(dynamicGainDb(0.0, -20.0, 0.0, 6.0) == 0.0); // sin rango no hace nada
}

TEST_CASE("Dinamica: la rodilla suave es continua y derivable")
{
    const double knee = 8.0;
    // En el umbral, la mitad de la rodilla al cuadrado entre dos rodillas.
    CHECK_THAT(dynamicGainDb(-20.0, -20.0, -30.0, knee), WithinAbs(-1.0, 1e-12));
    CHECK(dynamicGainDb(-24.0001, -20.0, -30.0, knee) == 0.0);
    CHECK_THAT(dynamicGainDb(-16.0, -20.0, -30.0, knee), WithinAbs(-4.0, 1e-9));

    double previous = 0.0;
    double previousSlope = 0.0;
    for (double level = -30.0; level <= 0.0; level += 0.01)
    {
        const double gain = dynamicGainDb(level, -20.0, -30.0, knee);
        const double slope = (gain - previous) / 0.01;
        CHECK(gain <= previous + 1e-12);                      // monótona
        CHECK(std::abs(slope - previousSlope) < 0.01 + 1e-9); // sin esquinas dentro de la rodilla
        previous = gain;
        previousSlope = slope;
        if (level > -14.0)
            break; // más allá aparece el límite del rango, que sí es una esquina
    }
}

TEST_CASE("Dinamica: entradas no finitas dan 0 dB")
{
    CHECK(dynamicGainDb(std::nan(""), -20.0, -12.0, 6.0) == 0.0);
    CHECK(dynamicGainDb(-10.0, std::nan(""), -12.0, 6.0) == 0.0);
    CHECK(dynamicGainDb(-10.0, -20.0, std::nan(""), 6.0) == 0.0);
    CHECK(dynamicGainDb(-10.0, -20.0, -12.0, std::nan("")) == -10.0);
}

// ---------------------------------------------------------------------------
// Envolvente y detector

TEST_CASE("Dinamica: la envolvente sigue ataque y relajacion con sus constantes de tiempo")
{
    EnvelopeFollower env;
    env.prepare(kFs);
    env.setTimes(10.0, 100.0);

    // Tras una constante de tiempo, 1 - 1/e del escalón.
    for (int i = 0; i < 480; ++i)
        env.process(1.0);
    CHECK_THAT(env.value(), WithinAbs(1.0 - std::exp(-1.0), 1e-6));

    for (int i = 0; i < 48000; ++i)
        env.process(1.0);
    const double top = env.value();
    for (int i = 0; i < 4800; ++i)
        env.process(0.0);
    CHECK_THAT(env.value(), WithinAbs(top * std::exp(-1.0), 1e-6));

    // Cambiar los tiempos no altera el nivel.
    const double before = env.value();
    env.setTimes(1.0, 5.0);
    CHECK(env.value() == before);
}

TEST_CASE("Dinamica: filtro del detector segun el tipo de banda")
{
    const FilterParams bell{FilterType::Bell, 2000.0, 9.0, 4.0};
    const auto detector = designDetector(bell, kFs);
    CHECK_THAT(magnitudeDb(detector, 2000.0, kFs), WithinAbs(0.0, 1e-9)); // 0 dB en el centro, sin la ganancia
    CHECK(magnitudeDb(detector, 200.0, kFs) < -30.0);

    const auto low = designDetector({FilterType::LowShelf, 200.0, 6.0, 2.0}, kFs);
    CHECK(magnitudeDb(low, 50.0, kFs) > -0.1);
    CHECK(magnitudeDb(low, 2000.0, kFs) < -35.0);

    const auto high = designDetector({FilterType::HighShelf, 5000.0, 6.0, 2.0}, kFs);
    CHECK(magnitudeDb(high, 15000.0, kFs) > -0.5);
    CHECK(magnitudeDb(high, 500.0, kFs) < -35.0);

    CHECK(designDetector({FilterType::Tilt, 1000.0, 3.0}, kFs).numSections == 0);

    CHECK(supportsDynamics(FilterType::Bell));
    CHECK(supportsDynamics(FilterType::Tilt));
    CHECK_FALSE(supportsDynamics(FilterType::LowCut));
    CHECK_FALSE(supportsDynamics(FilterType::Notch));
}

// ---------------------------------------------------------------------------
// Ecualizador con dinámica

TEST_CASE("Dinamica: un seno por encima del umbral se reduce lo que dice la curva")
{
    // Seno a -6 dBFS en el centro de la banda: el detector ve -6 dB.
    const auto input = sine(48000, 1000.0, 0.5);
    const double inputDb = peakDb(input, 0);

    SECTION("limitado por el rango")
    {
        Equalizer eq;
        eq.setBand(0, dynamicBell(1000.0, 2.0, -20.0, -9.0));
        eq.prepare(kFs);
        const auto out = run(eq, input, 512);
        CHECK_THAT(peakDb(out, 24000) - inputDb, WithinAbs(-9.0, 0.1));
        CHECK_THAT(eq.dynamicGainDb(0), WithinAbs(-9.0, 1e-9));
    }
    SECTION("el exceso sobre el umbral se traslada 1:1")
    {
        Equalizer eq;
        eq.setBand(0, dynamicBell(1000.0, 2.0, -20.0, -30.0, 0.0, kMinAttackMs));
        eq.prepare(kFs);
        const auto out = run(eq, input, 512);
        // La salida queda en el umbral (con el rizado de la envolvente).
        CHECK_THAT(peakDb(out, 24000), WithinAbs(-20.0, 0.1));
    }
    SECTION("rango positivo: expande")
    {
        Equalizer eq;
        eq.setBand(0, dynamicBell(1000.0, 2.0, -20.0, 4.0));
        eq.prepare(kFs);
        const auto out = run(eq, input, 512);
        CHECK_THAT(peakDb(out, 24000) - inputDb, WithinAbs(4.0, 0.1));
    }
    SECTION("la ganancia dinamica se suma a la estatica")
    {
        auto settings = dynamicBell(1000.0, 2.0, -20.0, -9.0);
        settings.filter.gainDb = 3.0;
        Equalizer eq;
        eq.setBand(0, settings);
        eq.prepare(kFs);
        const auto out = run(eq, input, 512);
        CHECK_THAT(peakDb(out, 24000) - inputDb, WithinAbs(-6.0, 0.1));
    }
}

TEST_CASE("Dinamica: por debajo del umbral la banda suena como la estatica, bit a bit")
{
    const auto input = sine(24000, 1000.0, 0.01); // -40 dBFS

    Equalizer eq;
    eq.setBand(0, dynamicBell(1000.0, 2.0, -20.0, -12.0));
    eq.prepare(kFs);
    CHECK(run(eq, input, 256) == input); // campana de 0 dB sin ganancia dinámica: identidad
    CHECK(eq.dynamicGainDb(0) == 0.0);
}

TEST_CASE("Dinamica: el detector solo escucha su zona del espectro")
{
    // Un grave fuerte no dispara una banda dinámica en agudos (de-esser).
    const auto input = sine(24000, 150.0, 0.7);

    Equalizer eq;
    eq.setBand(0, dynamicBell(7000.0, 2.0, -30.0, -12.0));
    eq.prepare(kFs);
    CHECK(run(eq, input, 256) == input);
}

TEST_CASE("Dinamica: de-esser, reduce los agudos y deja el resto")
{
    // Voz simplificada: un tono grave continuo y ráfagas de "sibilancia" a 7 kHz.
    std::vector<float> input = sine(48000, 200.0, 0.3);
    const auto hiss = sine(48000, 7000.0, 0.4);
    for (std::size_t i = 0; i < input.size(); ++i)
        if ((i / 4800) % 2 == 1)
            input[i] += hiss[i];

    Equalizer eq;
    eq.setBand(0, dynamicBell(7000.0, 1.5, -24.0, -12.0, 3.0, 0.5, 50.0));
    eq.prepare(kFs);
    const auto out = run(eq, input, 64);

    // Ráfaga de 7 kHz (muestras 33600..38400): reducida unos 12 dB respecto a la entrada.
    const std::vector<float> burstIn(input.begin() + 34800, input.begin() + 38400);
    const std::vector<float> burstOut(out.begin() + 34800, out.begin() + 38400);
    CHECK(measuredMagnitudeDb(burstOut, 7000.0, kFs) - measuredMagnitudeDb(burstIn, 7000.0, kFs) < -10.0);
    // El grave casi no cambia (la campana de 7 kHz apenas le afecta).
    CHECK_THAT(measuredMagnitudeDb(burstOut, 200.0, kFs) - measuredMagnitudeDb(burstIn, 200.0, kFs),
               WithinAbs(0.0, 0.1));

    // Sin sibilancia (tras la relajación), la banda vuelve a 0 dB.
    const std::vector<float> quietIn(input.begin() + 43200, input.begin() + 48000);
    const std::vector<float> quietOut(out.begin() + 43200, out.begin() + 48000);
    CHECK_THAT(measuredMagnitudeDb(quietOut, 200.0, kFs) - measuredMagnitudeDb(quietIn, 200.0, kFs),
               WithinAbs(0.0, 0.01));
}

TEST_CASE("Dinamica: shelf de graves dinamico")
{
    const auto input = sine(48000, 60.0, 0.5);
    BandSettings s;
    s.enabled = true;
    s.filter = {FilterType::LowShelf, 200.0, 0.0, kButterworthQ};
    s.dynamics = {true, -20.0, -6.0, 0.0, 5.0, 200.0, false};

    Equalizer eq;
    eq.setBand(0, s);
    eq.prepare(kFs);
    const auto out = run(eq, input, 512);
    // A 60 Hz el shelf aplica casi toda su ganancia.
    CHECK_THAT(peakDb(out, 24000) - peakDb(input, 0), WithinAbs(-6.0, 0.3));
}

TEST_CASE("Dinamica: sidechain externo")
{
    const auto quiet = sine(24000, 1000.0, 0.05); // -26 dBFS, por debajo del umbral
    const auto loud = sine(24000, 1000.0, 0.9);

    SECTION("el sidechain controla la banda")
    {
        Equalizer eq;
        eq.setBand(0, dynamicBell(1000.0, 2.0, -20.0, -10.0, 0.0, 1.0, 100.0, true));
        eq.prepare(kFs);
        const auto out = run(eq, quiet, 256, &loud);
        CHECK_THAT(peakDb(out, 12000) - peakDb(quiet, 0), WithinAbs(-10.0, 0.1));
    }
    SECTION("sin sidechain conectado, la deteccion externa no recibe senal")
    {
        Equalizer eq;
        eq.setBand(0, dynamicBell(1000.0, 2.0, -60.0, -10.0, 0.0, 1.0, 100.0, true));
        eq.prepare(kFs);
        CHECK(run(eq, loud, 256) == loud);
    }
    SECTION("con deteccion interna, el sidechain se ignora")
    {
        Equalizer eq;
        eq.setBand(0, dynamicBell(1000.0, 2.0, -20.0, -10.0));
        eq.prepare(kFs);
        CHECK(run(eq, quiet, 256, &loud) == quiet);
    }
}

TEST_CASE("Dinamica: no depende del tamano de bloque")
{
    std::vector<float> input = sine(16384, 900.0, 0.6);
    std::vector<float> sc = sine(16384, 3000.0, 0.8);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const float gate = (i / 1000) % 2 == 0 ? 1.0f : 0.1f; // nivel que sube y baja
        input[i] *= gate;
        sc[i] *= 1.1f - gate;
    }

    auto configure = [](Equalizer& eq)
    {
        eq.setBand(0, dynamicBell(900.0, 1.5, -18.0, -12.0, 6.0, 2.0, 40.0));
        eq.setBand(1, dynamicBell(3000.0, 3.0, -25.0, 6.0, 0.0, 0.5, 20.0, true));
        eq.setBand(2, {true, {FilterType::LowCut, 60.0, 0.0, kButterworthQ, 24}});
        eq.prepare(kFs);
    };

    Equalizer reference;
    configure(reference);
    const auto expected = run(reference, input, 16384, &sc);

    for (int blockSize : {1, 7, 32, 100, 4096})
    {
        Equalizer eq;
        configure(eq);
        INFO("bloque " << blockSize);
        CHECK(run(eq, input, blockSize, &sc) == expected);
    }
}

TEST_CASE("Dinamica: modulacion rapida sin chasquidos")
{
    // Seno continuo de 3 kHz; el sidechain abre y cierra la banda cada 240
    // muestras con el ataque más rápido y una relajación corta. La ganancia
    // salta 24 dB arriba y abajo 100 veces por segundo.
    const auto input = sine(48000, 3000.0, 0.25);
    std::vector<float> sc(input.size(), 0.0f);
    const auto burst = sine(48000, 3000.0, 1.0);
    for (std::size_t i = 0; i < sc.size(); ++i)
        if ((i / 240) % 2 == 0)
            sc[i] = burst[i];

    Equalizer eq;
    eq.setBand(0, dynamicBell(3000.0, 1.0, -30.0, -24.0, 0.0, kMinAttackMs, 2.0, true));
    eq.prepare(kFs);
    const auto out = run(eq, input, 128, &sc);

    // El paso máximo de un seno es 2π·f/fs·A: lo que sea mucho mayor es un chasquido.
    const float clean = maxStep(input);
    INFO("paso limpio " << clean << ", paso con modulacion " << maxStep(out));
    CHECK(maxStep(out) < clean * 1.25f);
    CHECK(peakDb(out, 0) < peakDb(input, 0) + 0.5); // tampoco sobrepasa la entrada
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v); }));
}

TEST_CASE("Dinamica: desactivarla con la ganancia reducida hace un fundido")
{
    // La banda reduce 24 dB un seno fuerte; al desactivar la dinámica la
    // ganancia vuelve a 0 dB con un fundido de 10 ms, no de golpe. (1300 Hz:
    // un periodo que no es múltiplo de los tramos de 32 muestras, para que el
    // cambio no caiga siempre en un cruce por cero.)
    const auto input = sine(19200, 1300.0, 0.5);
    auto settings = dynamicBell(1300.0, 1.0, -40.0, -24.0, 0.0, 1.0, 5000.0);

    Equalizer eq;
    eq.setBand(0, settings);
    eq.prepare(kFs);

    auto out = input;
    float* ch = out.data();
    eq.process(&ch, 1, 9600);
    CHECK(eq.dynamicGainDb(0) < -23.0);

    settings.dynamics.enabled = false;
    eq.setBand(0, settings);
    ch = out.data() + 9600;
    eq.process(&ch, 1, 9600);

    // Medido: -77 dB con el fundido; -36 dB si la ganancia vuelve de golpe.
    INFO("agudos por el cambio: " << clickDb(out, 2000) << " dB");
    CHECK(clickDb(out, 2000) < -60.0);
    CHECK(eq.isSettled());
    CHECK(eq.dynamicGainDb(0) == 0.0);
    // Ya sin dinámica: la campana de 0 dB vuelve a ser la identidad exacta.
    CHECK(std::equal(out.begin() + 18000, out.end(), input.begin() + 18000));
}

TEST_CASE("Dinamica: mover el umbral de golpe no produce saltos")
{
    const auto input = sine(19200, 1300.0, 0.5);
    auto settings = dynamicBell(1300.0, 1.0, -40.0, -24.0, 0.0, 1.0, 5000.0);

    Equalizer eq;
    eq.setBand(0, settings);
    eq.prepare(kFs);

    auto out = input;
    float* ch = out.data();
    eq.process(&ch, 1, 9600);

    settings.dynamics.thresholdDb = 0.0; // la reducción desaparece
    eq.setBand(0, settings);
    ch = out.data() + 9600;
    eq.process(&ch, 1, 9600);

    // Medido: -82 dB suavizando el umbral muestra a muestra; -39 dB si se
    // suaviza en escalones de 32 muestras, y -36 dB si salta de golpe.
    INFO("agudos por el cambio: " << clickDb(out, 2000) << " dB");
    CHECK(clickDb(out, 2000) < -60.0);
    CHECK(eq.dynamicGainDb(0) == 0.0);
    CHECK(eq.isSettled());
}

TEST_CASE("Dinamica: cambiar a un tipo sin ganancia la apaga con el fundido de tipo")
{
    const auto input = sine(9600, 1500.0, 0.5);
    auto settings = dynamicBell(1500.0, 1.0, -40.0, -24.0);

    Equalizer eq;
    eq.setBand(0, settings);
    eq.prepare(kFs);
    auto out = input;
    float* ch = out.data();
    eq.process(&ch, 1, 4800);

    settings.filter.type = FilterType::AllPass;
    eq.setBand(0, settings);
    ch = out.data() + 4800;
    eq.process(&ch, 1, 4800);

    CHECK(eq.dynamicGainDb(0) == 0.0);
    CHECK(eq.isSettled());
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v); }));
}

TEST_CASE("Dinamica: ajustes absurdos no producen NaN")
{
    Equalizer eq;
    eq.prepare(44100.0);
    for (int band = 0; band < kMaxBands; ++band)
    {
        BandSettings s;
        s.enabled = true;
        s.filter = {static_cast<FilterType>(band % 9), 100.0 * band + 20.0, 6.0, 2.0};
        s.dynamics = {true,         band % 2 ? std::nan("") : -500.0, band % 3 ? 1.0e9 : -1.0e9,
                      -5.0,         band % 2 ? 0.0 : std::nan(""),    -1.0,
                      band % 2 == 0};
        eq.setBand(band, s);
    }

    std::vector<float> left = sine(8192, 440.0, 0.9), right = sine(8192, 5000.0, 0.9);
    const auto sc = sine(8192, 2000.0, 1.0);
    float* channels[] = {left.data(), right.data()};
    const float* side[] = {sc.data()};
    eq.process(channels, 2, 8192, side, 1);

    CHECK(std::all_of(left.begin(), left.end(), [](float v) { return std::isfinite(v); }));
    CHECK(std::all_of(right.begin(), right.end(), [](float v) { return std::isfinite(v); }));
}
