// SPDX-License-Identifier: MIT
#include "esceq/dsp/phase_equalizer.h"

#include "filter_test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;
using namespace esceq::test;

namespace
{

constexpr double kFs = 48000.0;
constexpr PhaseMode kLinearModes[] = {PhaseMode::LinearLow, PhaseMode::LinearMedium, PhaseMode::LinearHigh};

using Stereo = std::array<std::vector<float>, 2>;

std::vector<float> noise(int numSamples, std::uint32_t seed = 4242u, float amplitude = 0.5f)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples));
    for (auto& sample : out)
    {
        seed = seed * 1664525u + 1013904223u;
        sample = amplitude * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
    return out;
}

std::vector<float> sine(int numSamples, double hz, double amplitude = 0.5)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kFs));
    return out;
}

BandSettings bell(double hz, double gain, double q = 1.0, ChannelMode channel = ChannelMode::Stereo)
{
    return {true, {FilterType::Bell, hz, gain, q}, {}, channel};
}

/// Pasa la señal en bloques de `blockSize` (o de tamaño irregular si es 0).
template <typename Eq>
void run(Eq& eq, Stereo& signal, int blockSize, const Stereo* sidechain = nullptr, int numChannels = 2)
{
    const int total = static_cast<int>(signal[0].size());
    for (int pos = 0, block = 0; pos < total; ++block)
    {
        const int size = std::min(total - pos, blockSize > 0 ? blockSize : 1 + (block * 37) % 509);
        float* channels[2] = {signal[0].data() + pos, signal[1].data() + pos};
        if (sidechain != nullptr)
        {
            const float* sc[2] = {(*sidechain)[0].data() + pos, (*sidechain)[1].data() + pos};
            eq.process(channels, numChannels, size, sc, 2);
        }
        else
            eq.process(channels, numChannels, size);
        pos += size;
    }
}

/// Deja que el hilo de diseño (aquí, el mismo hilo) termine y que el
/// ecualizador se asiente, procesando silencio. Después borra la memoria.
void settle(PhaseEqualizer& eq, int numChannels = 2)
{
    std::vector<float> left(512), right(512);
    for (int i = 0; i < 2000 && !eq.isSettled(); ++i)
    {
        eq.serviceDesigns();
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        float* channels[2] = {left.data(), right.data()};
        eq.process(channels, numChannels, 512);
    }
    REQUIRE(eq.isSettled());
    eq.reset();
}

/// Prepara en un modo, como hace el plugin al arrancar.
void prepareIn(PhaseEqualizer& eq, PhaseMode mode, std::initializer_list<BandSettings> bands, int numChannels = 2)
{
    eq.setPhaseMode(mode);
    int index = 0;
    for (const auto& band : bands)
        eq.setBand(index++, band);
    eq.prepare(kFs, numChannels);
    eq.serviceDesigns();
    eq.update();
}

/// Respuesta al impulso (canal izquierdo) de un ecualizador ya asentado.
std::vector<float> impulseResponse(PhaseEqualizer& eq, int length)
{
    Stereo signal{std::vector<float>(static_cast<std::size_t>(length)),
                  std::vector<float>(static_cast<std::size_t>(length))};
    signal[0][0] = 1.0f;
    signal[1][0] = 1.0f;
    run(eq, signal, 256);
    return signal[0];
}

float maxStep(const std::vector<float>& signal, std::size_t from = 1)
{
    float worst = 0.0f;
    for (std::size_t i = std::max<std::size_t>(from, 1); i < signal.size(); ++i)
        worst = std::max(worst, std::abs(signal[i] - signal[i - 1]));
    return worst;
}

} // namespace

TEST_CASE("PhaseEqualizer: Natural suena bit a bit como el Equalizer")
{
    const BandSettings bands[] = {
        bell(300.0, 4.0),
        {true, {FilterType::HighCut, 9000.0, 0.0, kButterworthQ, 24}},
        {true, {FilterType::Bell, 5000.0, 0.0, 2.0}, {true, -30.0, -8.0, 6.0, 1.0, 50.0, false}, ChannelMode::Mid},
        {true, {FilterType::LowShelf, 120.0, 3.0, kButterworthQ}, {true, -40.0, 6.0, 0.0, 5.0, 80.0, true}},
    };
    Equalizer reference;
    PhaseEqualizer eq;
    for (int i = 0; i < 4; ++i)
    {
        reference.setBand(i, bands[i]);
        eq.setBand(i, bands[i]);
    }
    reference.prepare(kFs);
    eq.prepare(kFs);
    CHECK(eq.latencySamples() == 0);

    Stereo a{noise(20000, 1u), noise(20000, 2u)}, sc{noise(20000, 3u, 0.9f), noise(20000, 4u, 0.9f)};
    Stereo b = a;
    run(reference, a, 0, &sc);
    run(eq, b, 0, &sc);
    CHECK(a == b);

    // También con cambios de parámetros en marcha.
    eq.setBand(0, bell(800.0, -6.0));
    reference.setBand(0, bell(800.0, -6.0));
    eq.setBypassed(true);
    reference.setBypassed(true);
    run(reference, a, 300, &sc);
    run(eq, b, 300, &sc);
    CHECK(a == b);
}

TEST_CASE("PhaseEqualizer: cada modo lineal tiene su latencia y sin bandas retrasa bit a bit")
{
    for (const auto mode : kLinearModes)
    {
        INFO("modo " << static_cast<int>(mode));
        PhaseEqualizer eq;
        prepareIn(eq, mode, {});
        CHECK(eq.activePhaseMode() == mode);
        const int latency = eq.latencySamples();
        CHECK(latency == latencySamples(mode, kFs));

        const auto input = noise(latency + 3000, 9u);
        Stereo signal{input, noise(latency + 3000, 10u)};
        const auto right = signal[1];
        run(eq, signal, 0);
        for (int i = 0; i < latency; ++i)
            REQUIRE(signal[0][static_cast<std::size_t>(i)] == 0.0f);
        bool exact = true;
        for (std::size_t i = static_cast<std::size_t>(latency); i < input.size(); ++i)
            exact = exact && signal[0][i] == input[i - static_cast<std::size_t>(latency)] &&
                    signal[1][i] == right[i - static_cast<std::size_t>(latency)];
        CHECK(exact);
    }
}

TEST_CASE("PhaseEqualizer: en fase lineal la respuesta es simetrica y tiene la magnitud de las bandas")
{
    for (const auto mode : kLinearModes)
    {
        INFO("modo " << static_cast<int>(mode));
        PhaseEqualizer eq;
        const BandSettings bands[] = {bell(2000.0, 6.0, 1.5),
                                      {true, {FilterType::LowCut, 400.0, 0.0, kButterworthQ, 24}}};
        prepareIn(eq, mode, {bands[0], bands[1]});
        settle(eq);

        const int latency = eq.latencySamples();
        const auto size = linearPhaseSize(mode, kFs);
        const auto h = impulseResponse(eq, latency + size.firLength);

        // Simétrica alrededor de la latencia: fase lineal (con el error de la FFT en float).
        double asymmetry = 0.0, peak = 0.0;
        for (int n = 1; n < size.firLength / 2; ++n)
        {
            const auto after = static_cast<std::size_t>(latency + n), before = static_cast<std::size_t>(latency - n);
            asymmetry = std::max(asymmetry, static_cast<double>(std::abs(h[after] - h[before])));
            peak = std::max(peak, static_cast<double>(std::abs(h[after])));
        }
        CHECK(asymmetry < 1e-6);
        CHECK(peak > 0.01);

        for (const double f : {1000.0, 2000.0, 4000.0, 10000.0})
        {
            const double target = 20.0 * std::log10(LinearPhaseDesigner::targetResponse(bands, f, kFs).ll);
            CHECK_THAT(measuredMagnitudeDb(h, f, kFs), WithinAbs(target, 0.05));
        }
    }
}

TEST_CASE("PhaseEqualizer: con parametros fijos el resultado no depende del tamano de bloque")
{
    PhaseEqualizer eq;
    prepareIn(eq, PhaseMode::LinearLow,
              {bell(700.0, 5.0),
               bell(3000.0, -4.0, 2.0, ChannelMode::Side),
               {true, {FilterType::Bell, 6000.0, 0.0, 1.0}, {true, -30.0, -6.0, 6.0, 1.0, 50.0, false}}});
    settle(eq);

    const Stereo input{noise(12000, 5u), noise(12000, 6u)};
    Stereo reference;
    for (const int blockSize : {1, 13, 64, 511, 4096, 0})
    {
        eq.reset();
        Stereo signal = input;
        run(eq, signal, blockSize);
        if (reference[0].empty())
        {
            reference = signal;
            continue;
        }
        INFO("bloques de " << blockSize);
        CHECK(signal == reference);
    }
}

TEST_CASE("PhaseEqualizer: el bypass en fase lineal mantiene la latencia y no salta")
{
    PhaseEqualizer eq;
    prepareIn(eq, PhaseMode::LinearMedium, {bell(1000.0, 9.0)});
    settle(eq);
    const int latency = eq.latencySamples();

    SECTION("bypass completo: la entrada retrasada, bit a bit")
    {
        eq.setBypassed(true);
        settle(eq);
        const auto input = noise(latency + 2000, 3u);
        Stereo signal{input, input};
        run(eq, signal, 100);
        bool exact = true;
        for (std::size_t i = static_cast<std::size_t>(latency); i < input.size(); ++i)
            exact = exact && signal[0][i] == input[i - static_cast<std::size_t>(latency)];
        CHECK(exact);
        CHECK(eq.latencySamples() == latency);
    }
    SECTION("activar y quitar el bypass con un seno no produce saltos")
    {
        Stereo signal{sine(latency + 20000, 1000.0, 0.3), sine(latency + 20000, 1000.0, 0.3)};
        const int total = static_cast<int>(signal[0].size());
        for (int pos = 0; pos < total; pos += 64)
        {
            if (pos == latency + 4000)
                eq.setBypassed(true);
            if (pos == latency + 12000)
                eq.setBypassed(false);
            float* channels[2] = {signal[0].data() + pos, signal[1].data() + pos};
            eq.process(channels, 2, std::min(64, total - pos));
        }
        // Un seno de 1 kHz a 0,3 (+9 dB: ~0,85) cambia como mucho 0,11 por muestra.
        CHECK(maxStep(signal[0], static_cast<std::size_t>(latency) + 10) < 0.13f);
    }
}

TEST_CASE("PhaseEqualizer: mover una banda en fase lineal cambia el FIR sin chasquidos")
{
    PhaseEqualizer eq;
    prepareIn(eq, PhaseMode::LinearLow, {bell(1000.0, 0.0)});
    settle(eq);
    const int latency = eq.latencySamples();

    Stereo signal{sine(48000, 1000.0, 0.25), sine(48000, 1000.0, 0.25)};
    const int total = static_cast<int>(signal[0].size());
    int changes = 0;
    for (int pos = 0, step = 0; pos < total; pos += 128, ++step)
    {
        if (step % 10 == 0)
        {
            // Saltos bruscos de ganancia, como una automatización en escalones.
            eq.setBand(0, bell(1000.0, (step / 10) % 2 == 0 ? 12.0 : -12.0));
            ++changes;
        }
        eq.serviceDesigns();
        float* channels[2] = {signal[0].data() + pos, signal[1].data() + pos};
        eq.process(channels, 2, std::min(128, total - pos));
    }
    CHECK(changes > 30);
    // A +12 dB el seno llega a 1,0: cambia como mucho 0,13 por muestra. Un
    // cambio de FIR sin fundido daría saltos de hasta 0,9.
    CHECK(maxStep(signal[0], static_cast<std::size_t>(latency)) < 0.15f);
}

TEST_CASE("PhaseEqualizer: cambiar de modo baja a silencio, cambia la latencia y vuelve sin saltos")
{
    PhaseEqualizer eq;
    prepareIn(eq, PhaseMode::Natural, {bell(2000.0, 6.0)});
    settle(eq);
    CHECK(eq.latencySamples() == 0);

    const PhaseMode sequence[] = {PhaseMode::LinearLow, PhaseMode::LinearHigh, PhaseMode::Natural,
                                  PhaseMode::LinearMedium, PhaseMode::Natural};
    Stereo signal{sine(48000 * 5, 440.0, 0.4), sine(48000 * 5, 440.0, 0.4)};
    const int total = static_cast<int>(signal[0].size());
    int next = 0;
    std::vector<int> latencies;
    for (int pos = 0, block = 0; pos < total; pos += 256, ++block)
    {
        if (block % 180 == 10 && next < 5)
            eq.setPhaseMode(sequence[next++]);
        eq.serviceDesigns();
        float* channels[2] = {signal[0].data() + pos, signal[1].data() + pos};
        eq.process(channels, 2, std::min(256, total - pos));
        if (latencies.empty() || latencies.back() != eq.latencySamples())
            latencies.push_back(eq.latencySamples());
    }
    CHECK(eq.activePhaseMode() == PhaseMode::Natural);
    CHECK(latencies == std::vector<int>{0, 1280, 17408, 0, 4608, 0});
    // Un seno de 440 Hz a 0,4 con +6 dB en 2 kHz cambia ~0,02 por muestra; un
    // corte sin fundido saltaría hasta 0,4.
    CHECK(maxStep(signal[0]) < 0.04f);
}

TEST_CASE("PhaseEqualizer: hasta que el modo nuevo esta listo suena el anterior")
{
    PhaseEqualizer eq;
    prepareIn(eq, PhaseMode::Natural, {bell(2000.0, 6.0)});
    eq.setPhaseMode(PhaseMode::LinearHigh);
    Stereo signal{noise(4096), noise(4096)};
    run(eq, signal, 512); // sin serviceDesigns(): no hay nada diseñado
    CHECK(eq.activePhaseMode() == PhaseMode::Natural);
    CHECK(eq.latencySamples() == 0);
    CHECK(!eq.isSettled());

    eq.serviceDesigns();
    settle(eq);
    CHECK(eq.activePhaseMode() == PhaseMode::LinearHigh);
}

TEST_CASE("PhaseEqualizer: lo disenado para un modo que ya no se pide se descarta")
{
    PhaseEqualizer eq;
    prepareIn(eq, PhaseMode::Natural, {bell(2000.0, 6.0)});
    eq.setPhaseMode(PhaseMode::LinearLow);
    eq.serviceDesigns(); // diseña Low
    eq.setPhaseMode(PhaseMode::LinearMedium);
    settle(eq);
    CHECK(eq.activePhaseMode() == PhaseMode::LinearMedium);
    CHECK(eq.latencySamples() == latencySamples(PhaseMode::LinearMedium, kFs));

    // Ida y vuelta antes de que el hilo de diseño lo vea: se queda en Medium.
    eq.setPhaseMode(PhaseMode::Natural);
    eq.setPhaseMode(PhaseMode::LinearMedium);
    settle(eq);
    CHECK(eq.activePhaseMode() == PhaseMode::LinearMedium);
}

TEST_CASE("PhaseEqualizer: el canal de cada banda tambien en fase lineal")
{
    SECTION("Left: el canal derecho sale retrasado sin cambios")
    {
        PhaseEqualizer eq;
        prepareIn(eq, PhaseMode::LinearLow, {bell(1000.0, 9.0, 1.0, ChannelMode::Left)});
        settle(eq);
        const int latency = eq.latencySamples();
        Stereo signal{noise(latency + 4000, 1u), noise(latency + 4000, 2u)};
        const auto right = signal[1];
        run(eq, signal, 0);
        double worst = 0.0;
        for (std::size_t i = static_cast<std::size_t>(latency); i < right.size(); ++i)
            worst = std::max(
                worst, static_cast<double>(std::abs(signal[1][i] - right[i - static_cast<std::size_t>(latency)])));
        CHECK(worst < 1e-5);
    }
    SECTION("Mid y Side: la componente que no se procesa sale sin cambios")
    {
        for (const auto mode : {ChannelMode::Mid, ChannelMode::Side})
        {
            PhaseEqualizer eq;
            prepareIn(eq, PhaseMode::LinearLow, {bell(1000.0, 9.0, 1.0, mode)});
            settle(eq);
            const int latency = eq.latencySamples();
            Stereo signal{noise(latency + 4000, 1u), noise(latency + 4000, 2u)};
            const Stereo input = signal;
            run(eq, signal, 0);
            const float sign = mode == ChannelMode::Mid ? -1.0f : 1.0f; // la que no se procesa
            double worst = 0.0;
            for (std::size_t i = static_cast<std::size_t>(latency); i < input[0].size(); ++i)
            {
                const auto j = i - static_cast<std::size_t>(latency);
                const float out = 0.5f * (signal[0][i] + sign * signal[1][i]);
                const float in = 0.5f * (input[0][j] + sign * input[1][j]);
                worst = std::max(worst, static_cast<double>(std::abs(out - in)));
            }
            CHECK(worst < 1e-5);
        }
    }
    SECTION("Mono: una banda Right o Side no hace nada")
    {
        PhaseEqualizer eq;
        prepareIn(eq, PhaseMode::LinearLow,
                  {bell(1000.0, 9.0, 1.0, ChannelMode::Right), bell(3000.0, 9.0, 1.0, ChannelMode::Side)}, 1);
        settle(eq, 1);
        const int latency = eq.latencySamples();
        Stereo signal{noise(latency + 2000, 1u), std::vector<float>(static_cast<std::size_t>(latency + 2000), 7.0f)};
        const auto input = signal[0];
        run(eq, signal, 0, nullptr, 1);
        bool exact = true;
        for (std::size_t i = static_cast<std::size_t>(latency); i < input.size(); ++i)
            exact = exact && signal[0][i] == input[i - static_cast<std::size_t>(latency)];
        CHECK(exact);
        // El segundo canal no se toca en mono.
        CHECK(std::all_of(signal[1].begin(), signal[1].end(), [](float v) { return v == 7.0f; }));
    }
}

TEST_CASE("PhaseEqualizer: la dinamica en fase lineal actua igual que en Natural, retrasada")
{
    // De-esser: la banda dinámica escucha su propia banda. En fase lineal, el
    // detector escucha la entrada retrasada: la ganancia dinámica es la
    // misma, muestra a muestra, con la latencia de retraso.
    const BandSettings deEsser{true, {FilterType::Bell, 6000.0, 0.0, 2.0}, {true, -30.0, -9.0, 6.0, 1.0, 60.0, false}};
    const BandSettings bands[] = {bell(300.0, 3.0), deEsser};

    PhaseEqualizer natural, linear;
    prepareIn(natural, PhaseMode::Natural, {bands[0], bands[1]});
    prepareIn(linear, PhaseMode::LinearLow, {bands[0], bands[1]});
    settle(linear);
    const int latency = linear.latencySamples();

    // Ráfagas de un seno de 6 kHz: la dinámica entra y sale.
    auto burst = sine(30000, 6000.0, 0.5);
    for (std::size_t i = 0; i < burst.size(); ++i)
        if ((i / 4000) % 2 == 1)
            burst[i] *= 0.01f;
    Stereo a{burst, burst}, b = a;
    std::vector<double> gainNatural, gainLinear;
    for (std::size_t i = 0; i < burst.size(); ++i)
    {
        float* ca[2] = {a[0].data() + i, a[1].data() + i};
        float* cb[2] = {b[0].data() + i, b[1].data() + i};
        natural.process(ca, 2, 1);
        linear.process(cb, 2, 1);
        gainNatural.push_back(natural.dynamicGainDb(1));
        gainLinear.push_back(linear.dynamicGainDb(1));
    }
    const double deepest = *std::min_element(gainNatural.begin(), gainNatural.end());
    CHECK(deepest < -6.0);
    bool same = true;
    for (std::size_t i = static_cast<std::size_t>(latency); i < burst.size(); ++i)
        same = same && gainLinear[i] == gainNatural[i - static_cast<std::size_t>(latency)];
    CHECK(same);

    // Y la magnitud es la de Natural: el cambio dinámico H(g+d)·H(−g) sobre el
    // FIR con la parte estática. Se compara el nivel de las ráfagas.
    auto level = [](const std::vector<float>& x, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += static_cast<double>(x[i]) * x[i];
        return 10.0 * std::log10(sum / static_cast<double>(to - from));
    };
    const std::size_t from = 9000, to = 11500; // dentro de la tercera ráfaga, ya comprimida
    CHECK_THAT(level(b[0], from + static_cast<std::size_t>(latency), to + static_cast<std::size_t>(latency)),
               WithinAbs(level(a[0], from, to), 0.1));
}

TEST_CASE("PhaseEqualizer: una banda dinamica en reposo no cambia la fase lineal")
{
    // Por debajo del umbral, la parte dinámica es H(g)·H(−g) = 1: la salida es
    // la de la banda estática sola, salvo el redondeo.
    const BandSettings quiet{true, {FilterType::Bell, 2000.0, 5.0, 1.0}, {true, 0.0, -12.0, 0.0, 1.0, 60.0, false}};
    PhaseEqualizer dynamic, plain;
    prepareIn(dynamic, PhaseMode::LinearLow, {quiet});
    prepareIn(plain, PhaseMode::LinearLow, {bell(2000.0, 5.0)});
    settle(dynamic);
    settle(plain);
    Stereo a{noise(8000, 1u, 0.05f), noise(8000, 2u, 0.05f)}, b = a;
    run(dynamic, a, 0);
    run(plain, b, 0);
    double worst = 0.0;
    for (std::size_t i = 0; i < a[0].size(); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(a[0][i] - b[0][i])));
    CHECK(worst < 1e-6);
}

TEST_CASE("PhaseEqualizer: el sidechain externo tambien llega retrasado")
{
    const BandSettings ducker{true, {FilterType::Bell, 1000.0, 0.0, 1.0}, {true, -30.0, -12.0, 0.0, 1.0, 50.0, true}};
    PhaseEqualizer natural, linear;
    prepareIn(natural, PhaseMode::Natural, {ducker});
    prepareIn(linear, PhaseMode::LinearMedium, {ducker});
    settle(linear);
    const int latency = linear.latencySamples();

    std::vector<float> trigger(20000, 0.0f);
    for (std::size_t i = 6000; i < 9000; ++i)
        trigger[i] = 0.5f;
    const Stereo sc{trigger, trigger};
    Stereo a{noise(20000), noise(20000)}, b = a;
    std::vector<double> gainNatural, gainLinear;
    for (int i = 0; i < 20000; i += 100)
    {
        float* ca[2] = {a[0].data() + i, a[1].data() + i};
        float* cb[2] = {b[0].data() + i, b[1].data() + i};
        const float* s[2] = {sc[0].data() + i, sc[1].data() + i};
        natural.process(ca, 2, 100, s, 2);
        linear.process(cb, 2, 100, s, 2);
        gainNatural.push_back(natural.dynamicGainDb(0));
        gainLinear.push_back(linear.dynamicGainDb(0));
    }
    // Natural reacciona en cuanto llega el sidechain; la fase lineal, `latency` después.
    CHECK(gainNatural[61] < -6.0);
    CHECK(gainLinear[61] == 0.0);
    const auto shifted = static_cast<std::size_t>(61 + latency / 100 + 1);
    CHECK(gainLinear[shifted] < -6.0);
}

TEST_CASE("PhaseEqualizer: un NaN en la entrada no estropea la fase lineal")
{
    PhaseEqualizer eq;
    prepareIn(eq, PhaseMode::LinearLow, {bell(1000.0, 6.0)});
    settle(eq);
    Stereo signal{noise(10000), noise(10000)};
    signal[0][100] = std::numeric_limits<float>::quiet_NaN();
    signal[1][200] = std::numeric_limits<float>::infinity();
    run(eq, signal, 64);
    CHECK(std::all_of(signal[0].begin(), signal[0].end(), [](float v) { return std::isfinite(v); }));
    CHECK(std::all_of(signal[1].begin(), signal[1].end(), [](float v) { return std::isfinite(v); }));
}

TEST_CASE("PhaseEqualizer: preparar a otra frecuencia de muestreo redisena el modo")
{
    PhaseEqualizer eq;
    prepareIn(eq, PhaseMode::LinearHigh, {bell(1000.0, 6.0)});
    CHECK(eq.latencySamples() == 17408);
    eq.prepare(96000.0);
    CHECK(eq.activePhaseMode() == PhaseMode::Natural); // hasta que llegue el diseño nuevo
    eq.serviceDesigns();
    eq.update();
    CHECK(eq.activePhaseMode() == PhaseMode::LinearHigh);
    CHECK(eq.latencySamples() == latencySamples(PhaseMode::LinearHigh, 96000.0));
}

TEST_CASE("PhaseEqualizer: en mono el FIR es el de mono")
{
    // Una banda Mid en mono procesa el único canal entero (como en el IIR), no la mitad.
    PhaseEqualizer eq;
    prepareIn(eq, PhaseMode::LinearMedium, {bell(2000.0, 12.0, 1.0, ChannelMode::Mid)}, 1);
    settle(eq, 1);
    const int latency = eq.latencySamples();
    std::vector<float> left(static_cast<std::size_t>(latency + 8192), 0.0f), unused(left.size(), 0.0f);
    left[0] = 1.0f;
    Stereo signal{left, unused};
    run(eq, signal, 256, nullptr, 1);
    CHECK_THAT(measuredMagnitudeDb(signal[0], 2000.0, kFs), WithinAbs(12.0, 0.05));
}
