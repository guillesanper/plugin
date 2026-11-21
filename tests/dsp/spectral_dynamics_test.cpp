// SPDX-License-Identifier: MIT
#include "esceq/dsp/spectral_dynamics.h"
#include "esceq/dsp/equalizer.h"

#include "filter_test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;

namespace {

constexpr double kFs = 48000.0;
constexpr double kPi = std::numbers::pi;

using Signal = std::vector<float>;

struct Stereo
{
    Signal left;
    Signal right;
};

Signal sine(int numSamples, double hz, double amplitude, double sampleRate = kFs, double phase = 0.0)
{
    Signal out(static_cast<std::size_t>(numSamples));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / sampleRate + phase));
    return out;
}

Signal noise(int numSamples, double rms, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(0.0, rms);
    Signal out(static_cast<std::size_t>(numSamples));
    for (float& x : out)
        x = static_cast<float>(dist(rng));
    return out;
}

Signal operator+(Signal a, const Signal& b)
{
    for (std::size_t i = 0; i < a.size(); ++i)
        a[i] += b[i];
    return a;
}

/// Procesa en bloques de `blockSize`, con sidechain y entrada del detector opcionales.
Stereo run(SpectralDynamics& engine, Stereo signal, int blockSize, const Stereo* sidechain = nullptr,
           int sidechainChannels = 2, const Stereo* detector = nullptr)
{
    const int total = static_cast<int>(signal.left.size());
    for (int pos = 0; pos < total; pos += blockSize)
    {
        const int length = std::min(blockSize, total - pos);
        float* channels[] = {signal.left.data() + pos, signal.right.data() + pos};
        const float* sc[] = {sidechain != nullptr ? sidechain->left.data() + pos : nullptr,
                             sidechain != nullptr ? sidechain->right.data() + pos : nullptr};
        const float* det[] = {detector != nullptr ? detector->left.data() + pos : nullptr,
                              detector != nullptr ? detector->right.data() + pos : nullptr};
        engine.process(channels, 2, length, sidechain != nullptr ? sc : nullptr,
                       sidechain != nullptr ? sidechainChannels : 0, detector != nullptr ? det : nullptr);
    }
    return signal;
}

Signal runMono(SpectralDynamics& engine, Signal signal, int blockSize)
{
    const int total = static_cast<int>(signal.size());
    for (int pos = 0; pos < total; pos += blockSize)
    {
        float* channel = signal.data() + pos;
        engine.process(&channel, 1, std::min(blockSize, total - pos));
    }
    return signal;
}

/// Amplitud de la componente de `hz` entre `from` y `to`, con ventana de Blackman.
double amplitude(const Signal& x, double hz, std::size_t from, std::size_t to, double sampleRate = kFs)
{
    double re = 0.0, im = 0.0, sum = 0.0;
    const double length = static_cast<double>(to - from);
    for (std::size_t i = from; i < to; ++i)
    {
        const double u = static_cast<double>(i - from) / length;
        const double w = 0.42 - 0.5 * std::cos(2.0 * kPi * u) + 0.08 * std::cos(4.0 * kPi * u);
        const double phase = 2.0 * kPi * hz * static_cast<double>(i) / sampleRate;
        re += w * x[i] * std::cos(phase);
        im -= w * x[i] * std::sin(phase);
        sum += w;
    }
    return 2.0 * std::hypot(re, im) / sum;
}

double toDb(double x)
{
    return 20.0 * std::log10(std::max(x, 1.0e-30));
}

/// Cambio, en dB, de la componente de `hz` de la entrada a la salida (con la latencia descontada).
double changeDb(const Signal& in, const Signal& out, double hz, int latency, std::size_t from, std::size_t to,
                double sampleRate = kFs)
{
    const auto lag = static_cast<std::size_t>(latency);
    return toDb(amplitude(out, hz, from + lag, to + lag, sampleRate)) - toDb(amplitude(in, hz, from, to, sampleRate));
}

/// Error de la salida frente a la entrada retrasada, en dB relativos a la energía de la entrada.
double delayErrorDb(const Signal& in, const Signal& out, int latency, std::size_t from)
{
    double error = 0.0, energy = 0.0;
    const auto lag = static_cast<std::size_t>(latency);
    for (std::size_t i = from; i + lag < out.size(); ++i)
    {
        const double e = static_cast<double>(out[i + lag]) - static_cast<double>(in[i]);
        error += e * e;
        energy += static_cast<double>(in[i]) * static_cast<double>(in[i]);
    }
    return 10.0 * std::log10(std::max(error, 1.0e-300) / energy);
}

BandSettings spectralBell(double hz, double q, double thresholdDb, double rangeDb,
                          ChannelMode channel = ChannelMode::Stereo, double attackMs = 1.0, double releaseMs = 100.0)
{
    BandSettings s;
    s.enabled = true;
    s.filter = {FilterType::Bell, hz, 0.0, q};
    s.dynamics = {true, thresholdDb, rangeDb, 0.0, attackMs, releaseMs};
    s.channel = channel;
    return s;
}

Stereo same(const Signal& x)
{
    return {x, x};
}

/// Pico, en dB, de lo que hay por encima de 8 kHz desde la muestra `from`:
/// en una señal grave, lo que aparezca es el chasquido de un cambio brusco.
double clickDb(Signal signal, std::size_t from)
{
    BandFilter highPass;
    highPass.setDesign(designBand({FilterType::LowCut, 8000.0, 0.0, kButterworthQ, 48}, kFs));
    float* channel = signal.data();
    highPass.process(&channel, 1, static_cast<int>(signal.size()));
    float peak = 0.0f;
    for (std::size_t i = from; i < signal.size(); ++i)
        peak = std::max(peak, std::abs(signal[i]));
    return toDb(peak);
}

} // namespace

TEST_CASE("Dinamica espectral: tamano de la STFT y latencia segun la frecuencia de muestreo")
{
    CHECK(SpectralDynamics::fftSize(44100.0) == 2048);
    CHECK(SpectralDynamics::fftSize(48000.0) == 2048);
    CHECK(SpectralDynamics::fftSize(88200.0) == 4096);
    CHECK(SpectralDynamics::fftSize(96000.0) == 4096);
    CHECK(SpectralDynamics::fftSize(176400.0) == 8192);
    CHECK(SpectralDynamics::fftSize(192000.0) == 8192);
    CHECK(SpectralDynamics::latencySamples(48000.0) == 2048);
    CHECK_THAT(1000.0 * SpectralDynamics::latencySamples(48000.0) / 48000.0, WithinAbs(42.67, 0.01));
    CHECK_THAT(1000.0 * SpectralDynamics::latencySamples(192000.0) / 192000.0, WithinAbs(42.67, 0.01));

    SpectralDynamics engine;
    engine.prepare(96000.0);
    CHECK(engine.size() == 4096);
    CHECK(engine.hopSize() == 1024);
    CHECK(engine.numBins() == 2049);
    CHECK(engine.latency() == 4096);
}

TEST_CASE("Dinamica espectral: que bandas actuan")
{
    SpectralDynamics engine;
    CHECK_FALSE(engine.anyActive());

    BandSettings bell = spectralBell(1000.0, 1.0, -20.0, -6.0);
    engine.setBand(0, bell);
    CHECK(engine.isActive(0));
    CHECK(engine.anyActive());

    for (const FilterType type : {FilterType::LowShelf, FilterType::HighShelf, FilterType::Tilt})
    {
        bell.filter.type = type;
        engine.setBand(1, bell);
        CHECK(engine.isActive(1));
    }
    for (const FilterType type : {FilterType::LowCut, FilterType::HighCut, FilterType::Notch, FilterType::BandPass,
                                  FilterType::AllPass})
    {
        bell.filter.type = type;
        engine.setBand(1, bell);
        CHECK_FALSE(engine.isActive(1));
    }

    BandSettings off = spectralBell(1000.0, 1.0, -20.0, -6.0);
    off.dynamics.enabled = false;
    engine.setBand(2, off);
    CHECK_FALSE(engine.isActive(2));
    off = spectralBell(1000.0, 1.0, -20.0, -6.0);
    off.enabled = false;
    engine.setBand(2, off);
    CHECK_FALSE(engine.isActive(2));
    off = spectralBell(1000.0, 1.0, -20.0, 0.0); // sin rango no hace nada
    engine.setBand(2, off);
    CHECK_FALSE(engine.isActive(2));

    engine.setBand(-1, bell);
    engine.setBand(kMaxBands, bell);
    CHECK_FALSE(engine.isActive(kMaxBands));
    CHECK(engine.binGainsDb(kMaxBands).empty());
}

TEST_CASE("Dinamica espectral: por debajo del umbral la salida es la entrada retrasada")
{
    // Criterio de PLAN.md 5.7: error menor de −90 dB. Con bandas en los cuatro
    // dominios (el camino que separa L, R, M y S) y sin ninguna.
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        for (const bool withBands : {false, true})
        {
            SpectralDynamics engine;
            engine.prepare(rate);
            if (withBands)
            {
                engine.setBand(0, spectralBell(1000.0, 1.0, 0.0, -12.0, ChannelMode::Stereo));
                engine.setBand(1, spectralBell(3000.0, 2.0, 0.0, -12.0, ChannelMode::Left));
                engine.setBand(2, spectralBell(300.0, 0.7, 0.0, 6.0, ChannelMode::Mid));
                engine.setBand(3, spectralBell(8000.0, 1.0, 0.0, -12.0, ChannelMode::Side));
            }
            const int total = static_cast<int>(rate * 0.5);
            const Stereo in{noise(total, 0.05, 1) + sine(total, 440.0, 0.3, rate), noise(total, 0.05, 2)};
            const Stereo out = run(engine, in, 512);
            const int latency = engine.latency();

            // Desde la primera muestra: antes, la entrada era silencio, y eso también se reconstruye.
            const double errorLeft = delayErrorDb(in.left, out.left, latency, 0);
            const double errorRight = delayErrorDb(in.right, out.right, latency, 0);
            INFO("fs " << rate << (withBands ? " con bandas" : " sin bandas") << ": L " << errorLeft << " dB, R "
                       << errorRight << " dB");
            CHECK(errorLeft < -90.0);
            CHECK(errorRight < -90.0);
            // Antes de la latencia solo sale el redondeo de la FFT.
            float early = 0.0f;
            for (int i = 0; i < latency; ++i)
                early = std::max(early, std::abs(out.left[static_cast<std::size_t>(i)]));
            CHECK(early < 1.0e-6f);
        }
    }

    SECTION("en mono")
    {
        SpectralDynamics engine;
        engine.setBand(0, spectralBell(1000.0, 1.0, 0.0, -12.0));
        const Signal in = noise(24000, 0.1, 3);
        const Signal out = runMono(engine, in, 100);
        CHECK(delayErrorDb(in, out, engine.latency(), 0) < -90.0);
    }
}

TEST_CASE("Dinamica espectral: solo baja el tono que pasa del umbral")
{
    // Criterio de PLAN.md 5.7. Un tono fuerte en el centro de la banda, 20 dB
    // por encima del umbral, y otro débil a 1/3 de octava, por debajo.
    const int total = 48000;
    const double loudHz = 1000.0;
    const double quietHz = loudHz * std::exp2(1.0 / 3.0);
    const Signal in = sine(total, loudHz, 0.5) + sine(total, quietHz, 0.01);
    const BandSettings band = spectralBell(loudHz, 2.0, -26.0, -6.0);

    SpectralDynamics engine;
    engine.setBand(0, band);
    const Stereo out = run(engine, same(in), 256);
    const int latency = engine.latency();
    const double loud = changeDb(in, out.left, loudHz, latency, 20000, 44000);
    const double quiet = changeDb(in, out.left, quietHz, latency, 20000, 44000);
    INFO("tono fuerte " << loud << " dB, tono débil " << quiet << " dB");
    CHECK_THAT(loud, WithinAbs(-6.0, 0.5));
    CHECK(std::abs(quiet) < 0.2);
    CHECK_THAT(changeDb(in, out.right, loudHz, latency, 20000, 44000), WithinAbs(loud, 1e-3));

    // El medidor: la reducción máxima de la máscara.
    CHECK_THAT(engine.gainDb(0), WithinAbs(-6.0, 0.01));
    const auto gains = engine.binGainsDb(0);
    REQUIRE(gains.size() == static_cast<std::size_t>(engine.numBins()));
    const auto bin = static_cast<std::size_t>(std::lround(loudHz * engine.size() / kFs));
    CHECK_THAT(gains[bin], WithinAbs(-6.0, 0.01));
    const auto quietBin = static_cast<std::size_t>(std::lround(quietHz * engine.size() / kFs));
    CHECK(gains[quietBin] == 0.0f);

    // La banda dinámica normal baja toda la campana: también el tono débil.
    Equalizer eq;
    eq.prepare(kFs);
    eq.setBand(0, band);
    Signal normal = in;
    float* channel = normal.data();
    eq.process(&channel, 1, total);
    const double normalQuiet = changeDb(in, normal, quietHz, 0, 20000, 44000);
    INFO("banda dinámica normal: tono débil " << normalQuiet << " dB");
    CHECK(normalQuiet < -1.0);
}

TEST_CASE("Dinamica espectral: el tono baja lo que marca el rango en todo el espectro")
{
    // Tonos entre dos bins (el peor caso del detector) y en varias zonas.
    for (const double hz : {315.0, 1011.7, 2500.0, 5011.0, 12345.0})
    {
        SpectralDynamics engine;
        engine.setBand(0, spectralBell(hz, 1.0, -30.0, -9.0));
        const Signal in = sine(48000, hz, 0.4);
        const Stereo out = run(engine, same(in), 1024);
        const double change = changeDb(in, out.left, hz, engine.latency(), 20000, 44000);
        INFO(hz << " Hz: " << change << " dB");
        CHECK_THAT(change, WithinAbs(-9.0, 0.5));
    }

    SECTION("con rango positivo, sube")
    {
        SpectralDynamics engine;
        engine.setBand(0, spectralBell(2000.0, 1.0, -40.0, 4.0));
        const Signal in = sine(48000, 2000.0, 0.1);
        const Stereo out = run(engine, same(in), 1024);
        CHECK_THAT(changeDb(in, out.left, 2000.0, engine.latency(), 20000, 44000), WithinAbs(4.0, 0.5));
        CHECK_THAT(engine.gainDb(0), WithinAbs(4.0, 0.01));
    }

    SECTION("a 44,1, 96 y 192 kHz")
    {
        for (const double rate : {44100.0, 96000.0, 192000.0})
        {
            SpectralDynamics engine;
            engine.prepare(rate);
            engine.setBand(0, spectralBell(3000.0, 1.0, -30.0, -6.0));
            const auto total = static_cast<int>(rate);
            const Signal in = sine(total, 3000.0, 0.4, rate);
            const Stereo out = run(engine, same(in), 1024);
            const auto from = static_cast<std::size_t>(rate * 0.4);
            const auto to = static_cast<std::size_t>(rate * 0.9);
            const double change = changeDb(in, out.left, 3000.0, engine.latency(), from, to, rate);
            INFO(rate << " Hz: " << change << " dB");
            CHECK_THAT(change, WithinAbs(-6.0, 0.5));
        }
    }
}

TEST_CASE("Dinamica espectral: la forma del detector pondera la ganancia")
{
    // Campana: lejos de su centro, la ganancia cae como su paso banda.
    SpectralDynamics engine;
    engine.setBand(0, spectralBell(1000.0, 4.0, -70.0, -12.0));
    const Signal in = noise(48000, 0.2, 7);
    run(engine, same(in), 4096);
    const auto gains = engine.binGainsDb(0);
    const auto binOf = [&](double hz) { return static_cast<std::size_t>(std::lround(hz * engine.size() / kFs)); };
    const double bandPassAtOctave = std::abs(response(designDetector({FilterType::Bell, 1000.0, 0.0, 4.0}, kFs),
                                                      2000.0, kFs));
    CHECK_THAT(gains[binOf(1000.0)], WithinAbs(-12.0, 0.01));
    CHECK_THAT(gains[binOf(2000.0)], WithinAbs(-12.0 * bandPassAtOctave, 0.05));
    CHECK(std::abs(gains[binOf(8000.0)]) < 0.5);

    // Tilt: todo el espectro con el mismo peso.
    BandSettings tilt = spectralBell(1000.0, 1.0, -70.0, -12.0);
    tilt.filter.type = FilterType::Tilt;
    engine.setBand(0, tilt);
    run(engine, same(in), 4096);
    const auto all = engine.binGainsDb(0);
    CHECK_THAT(all[binOf(100.0)], WithinAbs(-12.0, 0.01));
    CHECK_THAT(all[binOf(10000.0)], WithinAbs(-12.0, 0.01));

    // Shelf de graves: todo por debajo de su frecuencia, poco por encima.
    BandSettings shelf = tilt;
    shelf.filter.type = FilterType::LowShelf;
    shelf.filter.frequencyHz = 500.0;
    engine.setBand(0, shelf);
    run(engine, same(in), 4096);
    const auto low = engine.binGainsDb(0);
    CHECK_THAT(low[binOf(100.0)], WithinAbs(-12.0, 0.1));
    CHECK(std::abs(low[binOf(8000.0)]) < 0.1);
}

TEST_CASE("Dinamica espectral: cada banda actua en su canal")
{
    const int total = 48000;
    const int latency = SpectralDynamics::latencySamples(kFs);
    const Signal tone = sine(total, 2000.0, 0.5);
    const Signal silence(static_cast<std::size_t>(total), 0.0f);

    SECTION("Left: solo el canal izquierdo")
    {
        SpectralDynamics engine;
        engine.setBand(0, spectralBell(2000.0, 1.0, -30.0, -6.0, ChannelMode::Left));
        const Stereo out = run(engine, same(tone), 333);
        CHECK_THAT(changeDb(tone, out.left, 2000.0, latency, 20000, 44000), WithinAbs(-6.0, 0.5));
        CHECK(std::abs(changeDb(tone, out.right, 2000.0, latency, 20000, 44000)) < 0.01);
    }
    SECTION("Right: un tono solo a la izquierda no la mueve")
    {
        SpectralDynamics engine;
        engine.setBand(0, spectralBell(2000.0, 1.0, -30.0, -6.0, ChannelMode::Right));
        const Stereo out = run(engine, {tone, silence}, 333);
        CHECK(std::abs(changeDb(tone, out.left, 2000.0, latency, 20000, 44000)) < 0.01);
        CHECK(engine.gainDb(0) == 0.0);
    }
    SECTION("Mid y Side: un tono centrado lo baja Mid y no Side")
    {
        SpectralDynamics mid;
        mid.setBand(0, spectralBell(2000.0, 1.0, -30.0, -6.0, ChannelMode::Mid));
        const Stereo outMid = run(mid, same(tone), 333);
        CHECK_THAT(changeDb(tone, outMid.left, 2000.0, latency, 20000, 44000), WithinAbs(-6.0, 0.5));
        CHECK_THAT(changeDb(tone, outMid.right, 2000.0, latency, 20000, 44000), WithinAbs(-6.0, 0.5));

        SpectralDynamics side;
        side.setBand(0, spectralBell(2000.0, 1.0, -30.0, -6.0, ChannelMode::Side));
        const Stereo outSide = run(side, same(tone), 333);
        CHECK(delayErrorDb(tone, outSide.left, latency, 0) < -90.0);
        CHECK(side.gainDb(0) == 0.0);
    }
    SECTION("Side baja solo la diferencia")
    {
        // L = m + s, R = m − s, con m a 500 Hz y s a 2 kHz.
        const Signal m = sine(total, 500.0, 0.3);
        const Signal s = sine(total, 2000.0, 0.3);
        Signal minus = s;
        for (float& x : minus)
            x = -x;
        SpectralDynamics engine;
        engine.setBand(0, spectralBell(1000.0, 0.5, -40.0, -6.0, ChannelMode::Side));
        const Stereo out = run(engine, {m + s, m + minus}, 333);
        CHECK(std::abs(changeDb(m, out.left, 500.0, latency, 20000, 44000)) < 0.05);
        const BandDesign detector = designDetector({FilterType::Bell, 1000.0, 0.0, 0.5}, kFs);
        const double weight = std::abs(response(detector, 2000.0, kFs));
        CHECK_THAT(changeDb(s, out.left, 2000.0, latency, 20000, 44000), WithinAbs(-6.0 * weight, 0.5));
        CHECK_THAT(changeDb(s, out.right, 2000.0, latency, 20000, 44000), WithinAbs(-6.0 * weight, 0.5));
    }
    SECTION("Stereo enlaza los dos canales: un tono solo a la izquierda baja los dos")
    {
        SpectralDynamics engine;
        engine.setBand(0, spectralBell(2000.0, 1.0, -30.0, -6.0));
        const Signal quiet = sine(total, 2000.0, 0.01);
        const Stereo out = run(engine, {tone, quiet}, 333);
        CHECK_THAT(changeDb(tone, out.left, 2000.0, latency, 20000, 44000), WithinAbs(-6.0, 0.5));
        CHECK_THAT(changeDb(quiet, out.right, 2000.0, latency, 20000, 44000), WithinAbs(-6.0, 0.5));
    }
    SECTION("en mono, Right y Side no hacen nada")
    {
        for (const ChannelMode mode : {ChannelMode::Right, ChannelMode::Side})
        {
            SpectralDynamics engine;
            engine.setBand(0, spectralBell(2000.0, 1.0, -30.0, -6.0, mode));
            const Signal out = runMono(engine, tone, 333);
            CHECK(delayErrorDb(tone, out, latency, 0) < -90.0);
        }
        SpectralDynamics engine;
        engine.setBand(0, spectralBell(2000.0, 1.0, -30.0, -6.0, ChannelMode::Mid));
        const Signal out = runMono(engine, tone, 333);
        CHECK_THAT(changeDb(tone, out, 2000.0, latency, 20000, 44000), WithinAbs(-6.0, 0.5));
    }
}

TEST_CASE("Dinamica espectral: varias bandas del mismo dominio se suman en dB")
{
    const Signal tone = sine(48000, 2000.0, 0.5);
    SpectralDynamics engine;
    BandSettings tilt = spectralBell(2000.0, 1.0, -30.0, -3.0);
    tilt.filter.type = FilterType::Tilt;
    engine.setBand(0, spectralBell(2000.0, 1.0, -30.0, -4.0));
    engine.setBand(5, tilt);
    engine.setBand(9, spectralBell(2000.0, 1.0, -30.0, -2.0, ChannelMode::Mid));
    const Stereo out = run(engine, same(tone), 500);
    CHECK_THAT(changeDb(tone, out.left, 2000.0, engine.latency(), 20000, 44000), WithinAbs(-9.0, 0.5));
}

TEST_CASE("Dinamica espectral: sidechain externo y entrada del detector")
{
    const int total = 48000;
    const int latency = SpectralDynamics::latencySamples(kFs);
    const Signal tone = sine(total, 3000.0, 0.05); // por debajo del umbral
    BandSettings band = spectralBell(3000.0, 1.0, -20.0, -6.0);
    band.dynamics.external = true;

    SECTION("el sidechain controla la banda")
    {
        SpectralDynamics engine;
        engine.setBand(0, band);
        const Stereo key = same(sine(total, 3000.0, 0.5));
        const Stereo out = run(engine, same(tone), 256, &key);
        CHECK_THAT(changeDb(tone, out.left, 3000.0, latency, 20000, 44000), WithinAbs(-6.0, 0.5));

        SpectralDynamics mono;
        mono.setBand(0, band);
        const Stereo outMono = run(mono, same(tone), 256, &key, 1); // un sidechain mono se escucha tal cual
        CHECK_THAT(changeDb(tone, outMono.left, 3000.0, latency, 20000, 44000), WithinAbs(-6.0, 0.5));
    }
    SECTION("sin sidechain no actúa")
    {
        SpectralDynamics engine;
        engine.setBand(0, band);
        const Stereo loud = same(sine(total, 3000.0, 0.5));
        const Stereo out = run(engine, loud, 256);
        CHECK(delayErrorDb(loud.left, out.left, latency, 0) < -90.0);
    }
    SECTION("con detección interna, el sidechain se ignora y manda la entrada del detector")
    {
        band.dynamics.external = false;
        const Stereo key = same(sine(total, 3000.0, 0.5));
        SpectralDynamics engine;
        engine.setBand(0, band);
        const Stereo out = run(engine, same(tone), 256, &key);
        CHECK(delayErrorDb(tone, out.left, latency, 0) < -90.0);

        SpectralDynamics withDetector;
        withDetector.setBand(0, band);
        const Stereo outDetector = run(withDetector, same(tone), 256, nullptr, 0, &key);
        CHECK_THAT(changeDb(tone, outDetector.left, 3000.0, latency, 20000, 44000), WithinAbs(-6.0, 0.5));
    }
}

TEST_CASE("Dinamica espectral: no depende del tamano de bloque")
{
    // Criterio de PLAN.md 5.7: bloques de 1 a 4096 muestras, bit a bit. Con
    // bandas en los cuatro dominios, tiempos automáticos y sidechain.
    const int total = 30000;
    const Stereo in{noise(total, 0.1, 11) + sine(total, 1500.0, 0.4), noise(total, 0.1, 12)};
    const Stereo key{sine(total, 6000.0, 0.5), noise(total, 0.2, 13)};
    const auto configure = [](SpectralDynamics& engine) {
        engine.setBand(0, spectralBell(1500.0, 1.0, -30.0, -8.0));
        BandSettings automatic = spectralBell(400.0, 0.7, -35.0, 5.0, ChannelMode::Mid);
        automatic.dynamics.autoTiming = true;
        automatic.dynamics.kneeDb = 6.0;
        engine.setBand(1, automatic);
        BandSettings external = spectralBell(6000.0, 1.0, -30.0, -10.0, ChannelMode::Left);
        external.dynamics.external = true;
        engine.setBand(2, external);
        BandSettings shelf = spectralBell(8000.0, 0.7, -40.0, -6.0, ChannelMode::Side);
        shelf.filter.type = FilterType::HighShelf;
        engine.setBand(3, shelf);
    };

    SpectralDynamics reference;
    configure(reference);
    const Stereo expected = run(reference, in, total, &key);
    for (const int block : {1, 7, 32, 100, 511, 4096})
    {
        SpectralDynamics engine;
        configure(engine);
        const Stereo out = run(engine, in, block, &key);
        INFO("bloque de " << block);
        CHECK(out.left == expected.left);
        CHECK(out.right == expected.right);
    }
}

namespace {

/// Tiempos de reacción ante una ráfaga de `burstMs` a −6 dBFS
/// sobre un tono de prueba débil de la misma frecuencia (por debajo del
/// umbral): cuánto tarda la salida en llegar al 90 % de la reducción y, tras
/// la ráfaga, en recuperar el 90 % (le quede menos de una décima parte).
/// Se mide la ganancia de la salida frente a la entrada en ventanas de un periodo.
struct Reaction
{
    double attackMs = -1.0;
    double releaseMs = -1.0;
    double preDuckMs = 0.0; ///< Cuánto antes de la ráfaga baja ya más de 1 dB.
};

Reaction reaction(const BandSettings& band, double hz, int burstMs = 200)
{
    const int total = 72000;
    const int onset = 24000;
    const int offset = onset + 48 * burstMs;
    Signal in = sine(total, hz, 0.01);
    const Signal burst = sine(total, hz, 0.5);
    for (int i = onset; i < offset; ++i)
        in[static_cast<std::size_t>(i)] += burst[static_cast<std::size_t>(i)];

    SpectralDynamics engine;
    engine.setBand(0, band);
    const Stereo out = run(engine, same(in), 256);
    const auto lag = static_cast<std::size_t>(engine.latency());
    // Ventanas de un periodo, de 1 ms como mínimo, cada milisegundo.
    const int window = std::max(48, static_cast<int>(std::lround(kFs / hz)));
    const auto gainAt = [&](int at) {
        double num = 0.0, den = 0.0;
        for (int i = at; i < at + window; ++i)
        {
            const auto j = static_cast<std::size_t>(i);
            num += static_cast<double>(out.left[j + lag]) * out.left[j + lag];
            den += static_cast<double>(in[j]) * in[j];
        }
        return 10.0 * std::log10(num / den);
    };
    // La referencia: el cambio mayor durante la ráfaga (el rango, si se agota).
    double range = 0.0;
    for (int at = onset; at < offset - 480 - window; at += 48)
        if (std::abs(gainAt(at)) > std::abs(range))
            range = gainAt(at);
    Reaction r;
    for (int at = onset - 4800; at < onset; at += 48)
        if (std::abs(gainAt(at)) > 1.0)
        {
            r.preDuckMs = (onset - at) / 48.0;
            break;
        }
    // Desde cuándo se queda en el 90 % de la reducción hasta casi el final de
    // la ráfaga (sin sus últimos 10 ms y una ventana, donde se nota su corte: ver abajo).
    for (int at = offset - 480 - window; at >= onset; at -= 48)
        if (gainAt(at) > 0.9 * range)
        {
            r.attackMs = (at + 48 - onset) / 48.0;
            break;
        }
    // Desde cuándo le queda menos de una décima parte. Justo al acabar la
    // ráfaga, el corte brusco se reparte unos milisegundos en la salida
    // (la máscara es un filtro, y su respuesta dura): por eso se busca hacia atrás.
    for (int at = total - static_cast<int>(lag) - 2 * window; at >= offset; at -= 48)
        if (std::abs(gainAt(at)) > std::abs(0.1 * range))
        {
            r.releaseMs = (at + window - offset) / 48.0;
            break;
        }
    return r;
}

} // namespace

TEST_CASE("Dinamica espectral: ataque y relajacion, con un salto como minimo")
{
    // Los tiempos menores que un salto (10,7 ms) cuentan como un salto. Las
    // cifras medidas están en docs/dsp/dinamica-espectral.md.
    const Reaction fastest = reaction(spectralBell(7000.0, 1.0, -30.0, -12.0, ChannelMode::Stereo, 0.05, 1.0), 7000.0);
    const Reaction hop = reaction(spectralBell(7000.0, 1.0, -30.0, -12.0, ChannelMode::Stereo, 10.0, 10.0), 7000.0);
    INFO("mínimos: ataque " << fastest.attackMs << " ms, relajación " << fastest.releaseMs << " ms, antes "
                            << fastest.preDuckMs << " ms");
    CHECK(fastest.attackMs >= 0.0);
    CHECK(fastest.attackMs < 15.0);
    CHECK(fastest.releaseMs >= 0.0);
    CHECK(fastest.releaseMs < 40.0);
    CHECK(fastest.preDuckMs < 25.0);
    // Por debajo de un salto, el tiempo pedido no cambia nada.
    CHECK(fastest.attackMs <= hop.attackMs + 1.0);
    CHECK(fastest.releaseMs <= hop.releaseMs + 1.0);

    const Reaction slow = reaction(spectralBell(7000.0, 1.0, -30.0, -12.0, ChannelMode::Stereo, 30.0, 300.0), 7000.0);
    INFO("30 y 300 ms: ataque " << slow.attackMs << " ms, relajación " << slow.releaseMs << " ms");
    CHECK(slow.attackMs > fastest.attackMs);
    CHECK(slow.releaseMs > 600.0); // 24 dB a −1,2 dB con τ = 300 ms: unos 790 ms

    for (const double hz : {7000.0, 1000.0, 100.0})
    {
        // Una ráfaga aislada de 50 ms, 12 dB por encima del umbral, como en los
        // tiempos automáticos de la banda dinámica (ADR 0016): se suelta en menos de 150 ms.
        BandSettings automatic = spectralBell(hz, 1.5, -18.0, -24.0);
        automatic.dynamics.autoTiming = true;
        const Reaction isolated = reaction(automatic, hz, 50);
        const Reaction sustained = reaction(automatic, hz, 200);
        INFO("automáticos a " << hz << " Hz: ataque " << sustained.attackMs << " ms, relajación tras 50 ms "
                              << isolated.releaseMs << " ms");
        CHECK(sustained.attackMs >= 0.0);
        CHECK(sustained.attackMs < 40.0);
        CHECK(isolated.releaseMs >= 0.0);
        CHECK(isolated.releaseMs < 150.0);
    }
}

TEST_CASE("Dinamica espectral: el ruido musical queda contenido")
{
    // Ruido blanco con el umbral en mitad de su nivel: la máscara baja unos
    // 3 dB de media. Se mide cuánto se mueve de una trama a otra (desviación
    // típica en el tiempo) y de un bin al siguiente, entre 2 y 8 kHz.
    const int total = 96000;
    const Signal in = noise(total, 0.1, 21);
    SpectralDynamics engine;
    BandSettings band = spectralBell(4000.0, 1.0, -36.0, -12.0);
    band.filter.type = FilterType::Tilt;
    engine.setBand(0, band);

    const int first = static_cast<int>(2000.0 * engine.size() / kFs);
    const int last = static_cast<int>(8000.0 * engine.size() / kFs);
    const auto width = static_cast<std::size_t>(last - first + 1);
    std::vector<double> sum(width, 0.0), sumSquares(width, 0.0);
    double neighbour = 0.0, mean = 0.0;
    int frames = 0;
    Stereo buffer = same(in);
    const int hop = engine.hopSize();
    for (int pos = 0; pos + hop <= total; pos += hop)
    {
        float* channels[] = {buffer.left.data() + pos, buffer.right.data() + pos};
        engine.process(channels, 2, hop);
        if (pos < 24000)
            continue; // que se asiente
        const auto gains = engine.binGainsDb(0);
        for (std::size_t k = 0; k < width; ++k)
        {
            const double g = gains[static_cast<std::size_t>(first) + k];
            sum[k] += g;
            sumSquares[k] += g * g;
            mean += g;
            if (k > 0)
                neighbour += std::abs(g - gains[static_cast<std::size_t>(first) + k - 1]);
        }
        ++frames;
    }
    double deviation = 0.0;
    for (std::size_t k = 0; k < width; ++k)
    {
        const double m = sum[k] / frames;
        deviation += std::sqrt(std::max(sumSquares[k] / frames - m * m, 0.0));
    }
    deviation /= static_cast<double>(width);
    mean /= static_cast<double>(width) * frames;
    neighbour /= static_cast<double>(width - 1) * frames;
    INFO("media " << mean << " dB, desviación en el tiempo " << deviation << " dB, entre bins " << neighbour << " dB");
    CHECK(mean < -1.5);
    CHECK(mean > -6.0);
    CHECK(deviation < 1.0);
    CHECK(neighbour < 0.3);
}

TEST_CASE("Dinamica espectral: cambiar los ajustes de golpe no produce chasquidos")
{
    // Un tono de 200 Hz comprimido 12 dB: encender y apagar la banda y mover el
    // umbral de golpe. El solapamiento de las tramas funde cada cambio.
    const int total = 72000;
    const Signal in = sine(total, 200.0, 0.5);
    const BandSettings band = spectralBell(200.0, 1.0, -30.0, -12.0, ChannelMode::Stereo, 1.0, 20.0);

    SpectralDynamics engine;
    Stereo buffer = same(in);
    const auto step = [&](int from, int to) {
        float* channels[] = {buffer.left.data() + from, buffer.right.data() + from};
        engine.process(channels, 2, to - from);
    };
    step(0, 12000);
    engine.setBand(0, band); // encender
    step(12000, 30000);
    BandSettings moved = band;
    moved.dynamics.thresholdDb = 0.0; // de −12 dB a 0 de golpe
    engine.setBand(0, moved);
    step(30000, 45000);
    engine.setBand(0, band);
    step(45000, 60000);
    BandSettings off = band;
    off.enabled = false; // apagar
    engine.setBand(0, off);
    step(60000, total);

    const double click = clickDb(buffer.left, 4096);
    INFO("agudos espurios: " << click << " dB");
    CHECK(click < -60.0);
}

TEST_CASE("Dinamica espectral: un NaN o un infinito no la estropea")
{
    const int total = 48000;
    const int latency = SpectralDynamics::latencySamples(kFs);
    SECTION("en la señal")
    {
        SpectralDynamics engine;
        engine.setBand(0, spectralBell(1000.0, 1.0, -30.0, -6.0));
        Signal in = sine(total, 1000.0, 0.5);
        in[10000] = std::numeric_limits<float>::quiet_NaN();
        in[10001] = std::numeric_limits<float>::infinity();
        const Stereo out = run(engine, same(in), 512);
        CHECK(std::all_of(out.left.begin(), out.left.end(), [](float x) { return std::isfinite(x); }));
        CHECK(std::all_of(out.right.begin(), out.right.end(), [](float x) { return std::isfinite(x); }));
        // Tras las tramas que lo contienen, vuelve a actuar.
        CHECK_THAT(changeDb(in, out.left, 1000.0, latency, 24000, 44000), WithinAbs(-6.0, 0.5));
    }
    SECTION("en el sidechain")
    {
        SpectralDynamics engine;
        BandSettings band = spectralBell(1000.0, 1.0, -30.0, -6.0);
        band.dynamics.external = true;
        engine.setBand(0, band);
        Stereo key = same(sine(total, 1000.0, 0.5));
        key.left[5000] = std::numeric_limits<float>::quiet_NaN();
        key.right[5000] = -std::numeric_limits<float>::infinity();
        const Signal in = sine(total, 1000.0, 0.05);
        const Stereo out = run(engine, same(in), 512, &key);
        CHECK(std::all_of(out.left.begin(), out.left.end(), [](float x) { return std::isfinite(x); }));
        CHECK(std::all_of(out.right.begin(), out.right.end(), [](float x) { return std::isfinite(x); }));
        CHECK_THAT(changeDb(in, out.left, 1000.0, latency, 24000, 44000), WithinAbs(-6.0, 0.5));
    }
}

TEST_CASE("Dinamica espectral: reset vuelve a empezar de cero")
{
    SpectralDynamics engine;
    engine.setBand(0, spectralBell(1000.0, 1.0, -30.0, -6.0));
    const Stereo in = same(sine(20000, 1000.0, 0.5));
    const Stereo first = run(engine, in, 300);
    engine.reset();
    CHECK(engine.framesProcessed() == 0);
    const Stereo second = run(engine, in, 300);
    CHECK(first.left == second.left);
    CHECK(first.right == second.right);
}
