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

TEST_CASE("PhaseEqualizer: IIR suena bit a bit como el Equalizer")
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
    prepareIn(eq, PhaseMode::Iir, {bell(2000.0, 6.0)});
    settle(eq);
    CHECK(eq.latencySamples() == 0);

    const PhaseMode sequence[] = {PhaseMode::LinearLow, PhaseMode::LinearHigh, PhaseMode::Iir,
                                  PhaseMode::LinearMedium, PhaseMode::Iir};
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
    CHECK(eq.activePhaseMode() == PhaseMode::Iir);
    CHECK(latencies == std::vector<int>{0, 1280, 17408, 0, 4608, 0});
    // Un seno de 440 Hz a 0,4 con +6 dB en 2 kHz cambia ~0,02 por muestra; un
    // corte sin fundido saltaría hasta 0,4.
    CHECK(maxStep(signal[0]) < 0.04f);
}

TEST_CASE("PhaseEqualizer: hasta que el modo nuevo esta listo suena el anterior")
{
    PhaseEqualizer eq;
    prepareIn(eq, PhaseMode::Iir, {bell(2000.0, 6.0)});
    eq.setPhaseMode(PhaseMode::LinearHigh);
    Stereo signal{noise(4096), noise(4096)};
    run(eq, signal, 512); // sin serviceDesigns(): no hay nada diseñado
    CHECK(eq.activePhaseMode() == PhaseMode::Iir);
    CHECK(eq.latencySamples() == 0);
    CHECK(!eq.isSettled());

    eq.serviceDesigns();
    settle(eq);
    CHECK(eq.activePhaseMode() == PhaseMode::LinearHigh);
}

TEST_CASE("PhaseEqualizer: lo disenado para un modo que ya no se pide se descarta")
{
    PhaseEqualizer eq;
    prepareIn(eq, PhaseMode::Iir, {bell(2000.0, 6.0)});
    eq.setPhaseMode(PhaseMode::LinearLow);
    eq.serviceDesigns(); // diseña Low
    eq.setPhaseMode(PhaseMode::LinearMedium);
    settle(eq);
    CHECK(eq.activePhaseMode() == PhaseMode::LinearMedium);
    CHECK(eq.latencySamples() == latencySamples(PhaseMode::LinearMedium, kFs));

    // Ida y vuelta antes de que el hilo de diseño lo vea: se queda en Medium.
    eq.setPhaseMode(PhaseMode::Iir);
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

TEST_CASE("PhaseEqualizer: la dinamica en fase lineal actua igual que en IIR, retrasada")
{
    // De-esser: la banda dinámica escucha su propia banda. En fase lineal, el
    // detector escucha la entrada retrasada: la ganancia dinámica es la
    // misma, muestra a muestra, con la latencia de retraso.
    const BandSettings deEsser{true, {FilterType::Bell, 6000.0, 0.0, 2.0}, {true, -30.0, -9.0, 6.0, 1.0, 60.0, false}};
    const BandSettings bands[] = {bell(300.0, 3.0), deEsser};

    PhaseEqualizer natural, linear;
    prepareIn(natural, PhaseMode::Iir, {bands[0], bands[1]});
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

    // Y la magnitud es la de IIR: el cambio dinámico H(g+d)·H(−g) sobre el
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
    prepareIn(natural, PhaseMode::Iir, {ducker});
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
    // IIR reacciona en cuanto llega el sidechain; la fase lineal, `latency` después.
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
    CHECK(eq.activePhaseMode() == PhaseMode::Iir); // hasta que llegue el diseño nuevo
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

// ---------------------------------------------------------------------------
// Dinámica espectral (PLAN.md 5.7, ADR 0024 y 0025)

namespace
{

/// Campana dinámica espectral, sin ganancia estática.
BandSettings spectralBell(double hz, double thresholdDb, double rangeDb, double q = 2.0,
                          ChannelMode channel = ChannelMode::Stereo)
{
    BandSettings band{true, {FilterType::Bell, hz, 0.0, q}, {true, thresholdDb, rangeDb, 0.0, 5.0, 100.0}, channel};
    band.dynamics.spectral = true;
    return band;
}

/// Amplitud de la componente de `hz` entre `from` y `to`, con ventana de Blackman.
double toneAmplitude(const std::vector<float>& x, double hz, std::size_t from, std::size_t to)
{
    double re = 0.0, im = 0.0, sum = 0.0;
    const double length = static_cast<double>(to - from);
    for (std::size_t i = from; i < to; ++i)
    {
        const double u = static_cast<double>(i - from) / length;
        const double w =
            0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * u) + 0.08 * std::cos(4.0 * std::numbers::pi * u);
        const double phase = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / kFs;
        re += w * x[i] * std::cos(phase);
        im -= w * x[i] * std::sin(phase);
        sum += w;
    }
    return 2.0 * std::hypot(re, im) / sum;
}

/// Cambio en dB de la componente de `hz`, de la entrada a la salida retrasada `latency`.
double toneChangeDb(const std::vector<float>& in, const std::vector<float>& out, double hz, int latency,
                    std::size_t from, std::size_t to)
{
    const std::vector<float> aligned(out.begin() + latency, out.end());
    return 20.0 * std::log10(toneAmplitude(aligned, hz, from, to) / toneAmplitude(in, hz, from, to));
}

constexpr PhaseMode kAllModes[] = {PhaseMode::Iir, PhaseMode::LinearLow, PhaseMode::LinearMedium,
                                   PhaseMode::LinearHigh};

} // namespace

TEST_CASE("PhaseEqualizer: sin bandas espectrales, la latencia y el sonido de siempre")
{
    // Criterio de PLAN.md 5.7: con el interruptor apagado en todas las bandas,
    // bit a bit lo de antes y la misma latencia, en los cuatro modos. Aquí: el
    // campo no cambia nada mientras esté apagado, y apagar la última banda
    // espectral deja el ecualizador como si nunca se hubiera encendido.
    const BandSettings dynamicBell{true, {FilterType::Bell, 3000.0, 2.0, 1.5}, {true, -30.0, -8.0, 6.0, 2.0, 80.0}};
    const BandSettings bands[] = {bell(300.0, 4.0), dynamicBell};
    const Stereo input{noise(30000, 31u), noise(30000, 32u)};
    for (const auto mode : kAllModes)
    {
        INFO("modo " << static_cast<int>(mode));
        PhaseEqualizer plain, toggled;
        prepareIn(plain, mode, {bands[0], bands[1]});
        prepareIn(toggled, mode, {bands[0], bands[1]});
        CHECK(plain.latencySamples() == linearPhaseSize(mode, kFs).latency());
        CHECK_FALSE(plain.spectralRequested());

        BandSettings spectral = dynamicBell;
        spectral.dynamics.spectral = true;
        toggled.setBand(1, spectral);
        CHECK(toggled.spectralRequested());
        settle(toggled);
        CHECK(toggled.latencySamples() == linearPhaseSize(mode, kFs).latency() + SpectralDynamics::latencySamples(kFs));
        Stereo warm = input;
        run(toggled, warm, 256);
        toggled.setBand(1, dynamicBell);
        settle(toggled);
        CHECK_FALSE(toggled.spectralActive());
        CHECK(toggled.latencySamples() == plain.latencySamples());

        settle(plain);
        Stereo a = input, b = input;
        run(plain, a, 0);
        run(toggled, b, 0);
        CHECK(a == b);
    }
}

TEST_CASE("PhaseEqualizer: la dinamica espectral suma su latencia a la del modo")
{
    // Criterio de PLAN.md 5.7: la latencia comunicada es la medida, en los
    // cuatro modos y a 44,1, 48, 96 y 192 kHz. Un impulso, por debajo del
    // umbral, sale entero en la latencia (la banda no tiene parte estática).
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
        for (const auto mode : kAllModes)
            for (const int channels : {2, 1})
            {
                INFO("fs " << rate << ", modo " << static_cast<int>(mode) << ", canales " << channels);
                PhaseEqualizer eq;
                eq.setPhaseMode(mode);
                eq.setBand(0, spectralBell(2000.0, 0.0, -12.0));
                eq.prepare(rate, channels);
                eq.serviceDesigns();
                eq.update();
                const int expected = linearPhaseSize(mode, rate).latency() + SpectralDynamics::latencySamples(rate);
                CHECK(eq.latencySamples() == expected);
                CHECK(eq.spectralActive());

                const int length = expected + 4096;
                Stereo signal{std::vector<float>(static_cast<std::size_t>(length)),
                              std::vector<float>(static_cast<std::size_t>(length))};
                signal[0][0] = 0.5f;
                signal[1][0] = 0.5f;
                run(eq, signal, 512, nullptr, channels);
                const auto peak = std::max_element(signal[0].begin(), signal[0].end(),
                                                   [](float a, float b) { return std::abs(a) < std::abs(b); });
                CHECK(peak - signal[0].begin() == expected);
                CHECK_THAT(*peak, WithinAbs(0.5, 1.0e-4));
            }
}

TEST_CASE("PhaseEqualizer: en una banda espectral la mascara sustituye a la dinamica")
{
    // Un tono fuerte por encima del umbral y otro débil a 1/3 de octava: con
    // la banda espectral solo baja el fuerte, lo que marca el rango; con la
    // dinámica normal baja también el débil. En IIR y en fase lineal.
    const int total = 48000;
    std::vector<float> in = sine(total, 2000.0, 0.5);
    const std::vector<float> quiet = sine(total, 2000.0 * std::exp2(1.0 / 3.0), 0.01);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] += quiet[i];
    const double quietHz = 2000.0 * std::exp2(1.0 / 3.0);
    for (const auto mode : {PhaseMode::Iir, PhaseMode::LinearLow})
    {
        INFO("modo " << static_cast<int>(mode));
        for (const bool spectral : {true, false})
        {
            BandSettings band = spectralBell(2000.0, -30.0, -6.0);
            band.dynamics.spectral = spectral;
            PhaseEqualizer eq;
            prepareIn(eq, mode, {band});
            Stereo signal{in, in};
            run(eq, signal, 256);
            const int latency = eq.latencySamples();
            const double loud = toneChangeDb(in, signal[0], 2000.0, latency, 20000, 40000);
            const double weak = toneChangeDb(in, signal[0], quietHz, latency, 20000, 40000);
            INFO((spectral ? "espectral" : "normal") << ": fuerte " << loud << " dB, debil " << weak << " dB");
            if (spectral)
            {
                CHECK_THAT(loud, WithinAbs(-6.0, 0.5));
                CHECK(std::abs(weak) < 0.2);
                // El medidor de la banda es el de la máscara: el bin que más baja.
                CHECK_THAT(eq.dynamicGainDb(0), WithinAbs(-6.0, 0.5));
                CHECK(eq.spectralDynamics().isActive(0));
            }
            else
            {
                CHECK(weak < -1.0);
                CHECK_FALSE(eq.spectralDynamics().isActive(0));
            }
        }
    }
}

TEST_CASE("PhaseEqualizer: encender la primera banda espectral baja a silencio y vuelve sin saltos")
{
    // Criterio de PLAN.md 5.7: sin chasquidos al encender la primera banda
    // espectral. La primera y la última cambian la latencia con el fundido del
    // cambio de modo; las demás, no, y no dejan hueco.
    for (const auto mode : {PhaseMode::Iir, PhaseMode::LinearLow})
    {
        INFO("modo " << static_cast<int>(mode));
        BandSettings first = spectralBell(2000.0, -30.0, -6.0);
        BandSettings second = spectralBell(6000.0, -30.0, -6.0);
        first.dynamics.spectral = false;
        second.dynamics.spectral = false;
        PhaseEqualizer eq;
        prepareIn(eq, mode, {first, second, bell(500.0, 6.0)});
        settle(eq);
        const int base = eq.latencySamples();

        Stereo signal{sine(48000 * 4, 440.0, 0.4), sine(48000 * 4, 440.0, 0.4)};
        const int total = static_cast<int>(signal[0].size());
        std::vector<int> latencies{base};
        std::vector<int> changes;
        for (int pos = 0, block = 0; pos < total; pos += 256, ++block)
        {
            const bool on[] = {true, true, false, false};
            const int toggles[] = {40, 120, 200, 280};
            for (int t = 0; t < 4; ++t)
                if (block == toggles[t])
                {
                    BandSettings& target = t == 0 || t == 3 ? first : second;
                    target.dynamics.spectral = on[t];
                    eq.setBand(t == 0 || t == 3 ? 0 : 1, target);
                    changes.push_back(pos);
                }
            eq.serviceDesigns();
            float* channels[2] = {signal[0].data() + pos, signal[1].data() + pos};
            eq.process(channels, 2, std::min(256, total - pos));
            if (latencies.back() != eq.latencySamples())
                latencies.push_back(eq.latencySamples());
        }
        CHECK(latencies == std::vector<int>{base, base + SpectralDynamics::latencySamples(kFs), base});
        CHECK_FALSE(eq.spectralActive());
        // Un seno de 440 Hz a 0,4 (con +6 dB a 500 Hz, ~0,75) cambia ~0,04 por
        // muestra; un corte sin fundido saltaría hasta 0,75.
        CHECK(maxStep(signal[0]) < 0.06f);

        // Entre el primer cambio y el último, la otra banda entra y sale sin
        // hueco: el nivel no baja en ninguna ventana de 5 ms desde que vuelve
        // el sonido tras el primer cambio hasta el último.
        const int latency = base + SpectralDynamics::latencySamples(kFs);
        const auto from = static_cast<std::size_t>(changes[0] + latency + 4800);
        const auto to = static_cast<std::size_t>(changes[3]);
        double lowest = 1.0e9;
        for (std::size_t at = from; at + 240 < to; at += 240)
        {
            double energy = 0.0;
            for (std::size_t i = at; i < at + 240; ++i)
                energy += static_cast<double>(signal[0][i]) * signal[0][i];
            lowest = std::min(lowest, std::sqrt(energy / 240.0));
        }
        CHECK(lowest > 0.3);
    }
}

TEST_CASE("PhaseEqualizer: con dinamica espectral el resultado no depende del tamano de bloque")
{
    // Criterio de PLAN.md 5.7: bloques de 1 a 4096 muestras, bit a bit, con
    // una banda espectral escuchando el sidechain y una dinámica normal.
    BandSettings keyed = spectralBell(5000.0, -35.0, -9.0, 1.0, ChannelMode::Mid);
    keyed.dynamics.external = true;
    const BandSettings bands[] = {spectralBell(1500.0, -30.0, -8.0), keyed,
                                  {true, {FilterType::Bell, 300.0, 3.0, 1.0}, {true, -30.0, 4.0, 6.0, 5.0, 80.0}}};
    const Stereo input{noise(20000, 41u), noise(20000, 42u)};
    const Stereo key{noise(20000, 43u, 0.8f), sine(20000, 5000.0, 0.5)};
    for (const auto mode : {PhaseMode::Iir, PhaseMode::LinearMedium})
    {
        INFO("modo " << static_cast<int>(mode));
        PhaseEqualizer reference;
        prepareIn(reference, mode, {bands[0], bands[1], bands[2]});
        Stereo expected = input;
        run(reference, expected, 4096, &key);
        for (const int block : {1, 0, 64, 1000})
        {
            INFO("bloque " << block);
            PhaseEqualizer eq;
            prepareIn(eq, mode, {bands[0], bands[1], bands[2]});
            Stereo signal = input;
            run(eq, signal, block, &key);
            CHECK(signal == expected);
        }
    }
}

TEST_CASE("PhaseEqualizer: el bypass con dinamica espectral mantiene la latencia")
{
    for (const auto mode : {PhaseMode::Iir, PhaseMode::LinearLow})
    {
        INFO("modo " << static_cast<int>(mode));
        PhaseEqualizer eq;
        prepareIn(eq, mode, {spectralBell(1000.0, -40.0, -12.0), bell(3000.0, 6.0)});
        settle(eq);
        const int latency = eq.latencySamples();
        CHECK(latency == linearPhaseSize(mode, kFs).latency() + SpectralDynamics::latencySamples(kFs));

        SECTION("bypass completo: la entrada retrasada, bit a bit")
        {
            eq.setBypassed(true);
            settle(eq);
            const auto input = noise(latency + 4000, 5u);
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
            Stereo signal{sine(latency + 30000, 1000.0, 0.3), sine(latency + 30000, 1000.0, 0.3)};
            const int total = static_cast<int>(signal[0].size());
            for (int pos = 0; pos < total; pos += 64)
            {
                if (pos == latency + 6400)
                    eq.setBypassed(true);
                if (pos == latency + 19200)
                    eq.setBypassed(false);
                float* channels[2] = {signal[0].data() + pos, signal[1].data() + pos};
                eq.process(channels, 2, std::min(64, total - pos));
            }
            // Un seno de 1 kHz a 0,3 cambia como mucho 0,04 por muestra.
            CHECK(maxStep(signal[0], static_cast<std::size_t>(latency) + 10) < 0.05f);
            CHECK(eq.latencySamples() == latency);
        }
    }
}

TEST_CASE("PhaseEqualizer: en fase lineal el detector espectral escucha la entrada retrasada")
{
    // Con la parte estática plana, el FIR es la identidad retrasada: la salida
    // es la de SpectralDynamics con la entrada retrasada la latencia del FIR,
    // y su detector escuchando esa misma entrada retrasada (no la de ahora,
    // que adelantaría la reducción 26,7 ms). No se compara con IIR: la STFT no
    // es invariante en el tiempo, y 1280 muestras no son un número de saltos.
    auto burst = sine(40000, 3000.0, 0.5);
    for (std::size_t i = 0; i < burst.size(); ++i)
        if ((i / 6000) % 2 == 1)
            burst[i] *= 0.01f;
    const BandSettings band = spectralBell(3000.0, -30.0, -9.0);
    PhaseEqualizer eq;
    prepareIn(eq, PhaseMode::LinearLow, {band});
    const int firLatency = linearPhaseSize(PhaseMode::LinearLow, kFs).latency();
    Stereo out{burst, burst};
    run(eq, out, 256);

    const auto lag = static_cast<std::size_t>(firLatency);
    std::vector<float> delayed(burst.size(), 0.0f);
    const auto shift = static_cast<std::ptrdiff_t>(lag);
    std::copy(burst.begin(), burst.end() - shift, delayed.begin() + shift);
    auto engineOutput = [&](const std::vector<float>& detector)
    {
        SpectralDynamics engine;
        BandSettings plain = band;
        plain.dynamics.spectral = false;
        engine.setBand(0, plain);
        std::vector<float> left = delayed, right = delayed;
        for (std::size_t pos = 0; pos < left.size(); pos += 256)
        {
            const int length = static_cast<int>(std::min<std::size_t>(256, left.size() - pos));
            float* channels[2] = {left.data() + pos, right.data() + pos};
            const float* det[2] = {detector.data() + pos, detector.data() + pos};
            engine.process(channels, 2, length, nullptr, 0, det);
        }
        return left;
    };
    CHECK(out[0] == engineOutput(delayed));
    CHECK(out[0] != engineOutput(burst)); // el test distingue un detector sin retrasar

    // Y la máscara actúa: las ráfagas bajan unos 9 dB.
    const auto latency = static_cast<std::size_t>(eq.latencySamples());
    CHECK_THAT(toneChangeDb(burst, out[0], 3000.0, static_cast<int>(latency), 13000, 17000), WithinAbs(-9.0, 1.0));
}
