// SPDX-License-Identifier: MIT
#include "esceq/dsp/saturation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
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

double fromDb(double db)
{
    return std::pow(10.0, db / 20.0);
}

double toDb(double ratio)
{
    return 10.0 * std::log10(ratio);
}

Signal sine(std::size_t numSamples, double hz, double amplitude, double sampleRate = kFs)
{
    Signal out(numSamples);
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * hz * static_cast<double>(i) / sampleRate));
    return out;
}

Signal noise(std::size_t numSamples, double rms, unsigned seed)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> dist(0.0, rms);
    Signal out(numSamples);
    for (float& x : out)
        x = static_cast<float>(dist(rng));
    return out;
}

/// Procesa en estéreo (L y R iguales salvo que se den las dos) en bloques de `blockSize`.
void run(Saturation& engine, Signal& left, Signal& right, int blockSize)
{
    for (std::size_t position = 0; position < left.size(); position += static_cast<std::size_t>(blockSize))
    {
        const int length = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(blockSize),
                                                                   left.size() - position));
        float* channels[] = {left.data() + position, right.data() + position};
        engine.process(channels, 2, length);
    }
}

Signal runMono(Saturation& engine, Signal signal, int blockSize = 256)
{
    for (std::size_t position = 0; position < signal.size(); position += static_cast<std::size_t>(blockSize))
    {
        const int length = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(blockSize),
                                                                  signal.size() - position));
        float* channels[] = {signal.data() + position};
        engine.process(channels, 1, length);
    }
    return signal;
}

/// Potencia (amplitud²/2; la media² en 0 Hz) del componente de `hz` en n
/// muestras desde `start`, que deben contener un número entero de sus periodos.
double powerAt(const Signal& x, std::size_t start, std::size_t n, double hz, double sampleRate = kFs)
{
    double a = 0.0;
    double b = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double phase = 2.0 * kPi * hz * static_cast<double>(i) / sampleRate;
        a += x[start + i] * std::cos(phase);
        b += x[start + i] * std::sin(phase);
    }
    if (hz == 0.0)
        return (a / static_cast<double>(n)) * (a / static_cast<double>(n));
    a *= 2.0 / static_cast<double>(n);
    b *= 2.0 / static_cast<double>(n);
    return 0.5 * (a * a + b * b);
}

double meanSquare(const Signal& x, std::size_t start, std::size_t n)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        sum += static_cast<double>(x[start + i]) * x[start + i];
    return sum / static_cast<double>(n);
}

/// Aliasing de un seno de `hz` saturado, relativo a su fundamental (en dB de
/// potencia): todo lo que no es la continua ni un armónico por debajo de
/// Nyquist (por Parseval: la potencia total menos la de esos componentes).
/// Las n muestras deben contener un número entero de periodos de todo.
double aliasingDb(const Signal& y, std::size_t start, std::size_t n, double hz)
{
    double rest = meanSquare(y, start, n) - powerAt(y, start, n, 0.0);
    for (double h = hz; h < kFs / 2.0; h += hz)
        rest -= powerAt(y, start, n, h);
    return toDb(std::max(rest, 1e-30) / powerAt(y, start, n, hz));
}

Saturation makeEngine(SaturationMode mode, double driveDb, double mix = 1.0,
                      OversamplingFilter filter = OversamplingFilter::Iir, double sampleRate = kFs)
{
    Saturation engine;
    engine.setFilter(filter);
    engine.setMode(mode);
    engine.setDriveDb(driveDb);
    engine.setMix(mix);
    engine.prepare(sampleRate); // Sin rampas ni fundidos.
    return engine;
}

} // namespace

TEST_CASE("Saturacion: latencia de cada filtro")
{
    // Sobremuestreo hasta unos 384 kHz: ×8 hasta 50 kHz, ×4 hasta 100 kHz y ×2 por encima.
    CHECK(Saturation::stagesFor(44100.0) == 3);
    CHECK(Saturation::stagesFor(48000.0) == 3);
    CHECK(Saturation::stagesFor(88200.0) == 2);
    CHECK(Saturation::stagesFor(96000.0) == 2);
    CHECK(Saturation::stagesFor(192000.0) == 1);
    CHECK(Saturation::stagesFor(384000.0) == 1);
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
        CHECK(Saturation::latencySamples(OversamplingFilter::Iir, rate) == 0);
    CHECK(Saturation::latencySamples(OversamplingFilter::LinearPhase, 48000.0) == 47);
    CHECK(Saturation::latencySamples(OversamplingFilter::LinearPhase, 96000.0) == 43);
    CHECK(Saturation::latencySamples(OversamplingFilter::LinearPhase, 192000.0) == 35);
    CHECK(Oversampler::latencySamples(OversamplingFilter::Iir, 3) == 0);

    Saturation engine;
    engine.prepare(48000.0);
    CHECK(engine.latency() == 0);
    engine.setFilter(OversamplingFilter::LinearPhase);
    CHECK(engine.latency() == 47);
    CHECK(engine.filter() == OversamplingFilter::LinearPhase);
    engine.prepare(96000.0);
    CHECK(engine.latency() == 43);
}

TEST_CASE("Saturacion: con Off la salida es la entrada bit a bit")
{
    const Signal input = noise(20000, 0.3, 1);
    Saturation engine;
    engine.prepare(kFs);
    engine.setDriveDb(18.0);
    engine.setMix(0.5);
    CHECK_FALSE(engine.isActive());
    Signal left = input;
    Signal right = input;
    run(engine, left, right, 256);
    CHECK(left == input);
    CHECK(right == input);

    // Encendida y apagada: terminado el fundido, otra vez bit a bit.
    engine.setMode(SaturationMode::Warm);
    CHECK(engine.isActive());
    left = input;
    right = input;
    run(engine, left, right, 256);
    CHECK(left != input);
    engine.setMode(SaturationMode::Off);
    const auto fade = static_cast<std::size_t>(Saturation::kRampSeconds * kFs);
    left = input;
    right = input;
    run(engine, left, right, 100);
    CHECK_FALSE(engine.isActive());
    CHECK(std::equal(left.begin() + static_cast<std::ptrdiff_t>(fade), left.end(),
                     input.begin() + static_cast<std::ptrdiff_t>(fade)));
    left = input;
    right = input;
    run(engine, left, right, 256);
    CHECK(left == input);
}

TEST_CASE("Saturacion: Soft solo da armonicos impares y Warm anade el segundo")
{
    const auto filter = GENERATE(OversamplingFilter::Iir, OversamplingFilter::LinearPhase);
    constexpr std::size_t kStart = 24000;
    constexpr std::size_t kLength = 4800;
    constexpr double kHz = 1000.0;
    for (const double levelDb : {-18.0, 0.0})
    {
        CAPTURE(filter, levelDb);
        Saturation soft = makeEngine(SaturationMode::Soft, 12.0, 1.0, filter);
        const Signal y = runMono(soft, sine(kStart + kLength, kHz, fromDb(levelDb)));
        const double fundamental = powerAt(y, kStart, kLength, kHz);
        CHECK(toDb(powerAt(y, kStart, kLength, 3.0 * kHz) / fundamental) > -60.0);
        for (int h = 2; h <= 22; h += 2)
        {
            CAPTURE(h);
            CHECK(toDb(powerAt(y, kStart, kLength, h * kHz) / fundamental) < -100.0);
        }
    }

    Saturation warm = makeEngine(SaturationMode::Warm, 12.0, 1.0, filter);
    const Signal y = runMono(warm, sine(kStart + kLength, kHz, fromDb(Saturation::kCalibrationDbfs)));
    const double second = toDb(powerAt(y, kStart, kLength, 2.0 * kHz) / powerAt(y, kStart, kLength, kHz));
    CAPTURE(second);
    CHECK(second > -60.0);
    CHECK(second < -20.0);
}

TEST_CASE("Saturacion: aliasing de un seno de 10 kHz con 12 dB de drive a 48 kHz")
{
    // Por Parseval: lo que no es la fundamental ni un armónico por debajo de
    // Nyquist. Con 4800 muestras todo cae en un número entero de periodos.
    const auto filter = GENERATE(OversamplingFilter::Iir, OversamplingFilter::LinearPhase);
    const auto mode = GENERATE(SaturationMode::Soft, SaturationMode::Warm);
    constexpr std::size_t kStart = 24000;
    constexpr std::size_t kLength = 4800;
    for (const double levelDb : {-18.0, -6.0, 0.0})
    {
        CAPTURE(filter, mode, levelDb);
        Saturation engine = makeEngine(mode, 12.0, 1.0, filter);
        const Signal y = runMono(engine, sine(kStart + kLength, 10000.0, fromDb(levelDb)));
        const double aliasing = aliasingDb(y, kStart, kLength, 10000.0);
        CAPTURE(aliasing);
        CHECK(aliasing < -80.0);
    }
}

TEST_CASE("Saturacion: la continua desaparece en 1 s")
{
    // Warm con el drive máximo, un seno grave y fuerte: lo que más continua da.
    const auto filter = GENERATE(OversamplingFilter::Iir, OversamplingFilter::LinearPhase);
    Saturation engine = makeEngine(SaturationMode::Warm, Saturation::kMaxDriveDb, 1.0, filter);
    const Signal y = runMono(engine, sine(static_cast<std::size_t>(1.1 * kFs), 100.0, fromDb(-6.0)));
    // La media en 0,1 s (10 periodos) después del primer segundo.
    const double dc = std::sqrt(powerAt(y, static_cast<std::size_t>(kFs), static_cast<std::size_t>(0.1 * kFs), 0.0));
    CAPTURE(20.0 * std::log10(dc));
    CHECK(20.0 * std::log10(dc) < -100.0);

    // Sin bloqueador, la curva sí daría continua.
    const double mean = std::tanh(Saturation::kWarmBias + 1.0) + std::tanh(Saturation::kWarmBias - 1.0) -
                        2.0 * std::tanh(Saturation::kWarmBias);
    CHECK(std::abs(mean) > 0.01);
}

TEST_CASE("Saturacion: un seno de -18 dBFS a 1 kHz sale al mismo nivel con cualquier drive")
{
    const auto filter = GENERATE(OversamplingFilter::Iir, OversamplingFilter::LinearPhase);
    const auto mode = GENERATE(SaturationMode::Soft, SaturationMode::Warm);
    constexpr std::size_t kStart = 24000;
    constexpr std::size_t kLength = 4800;
    const Signal x = sine(kStart + kLength, 1000.0, fromDb(Saturation::kCalibrationDbfs));
    const double inputLevel = meanSquare(x, kStart, kLength);
    double worst = 0.0;
    for (double driveDb = 0.0; driveDb <= Saturation::kMaxDriveDb; driveDb += 1.5)
    {
        Saturation engine = makeEngine(mode, driveDb, 1.0, filter);
        const Signal y = runMono(engine, x);
        const double levelDb = toDb(meanSquare(y, kStart, kLength) / inputLevel);
        CAPTURE(filter, mode, driveDb, levelDb);
        CHECK(std::abs(levelDb) < 0.5);
        worst = std::max(worst, std::abs(levelDb));
    }
    // El cálculo es exacto: lo que queda es el filtro de sobremuestreo.
    CHECK(worst < 0.01);
}

TEST_CASE("Saturacion: la compensacion del drive")
{
    CHECK(Saturation::compensationGain(SaturationMode::Off, 12.0) == 1.0);
    // Con un periodo muy fino como referencia.
    for (const auto mode : {SaturationMode::Soft, SaturationMode::Warm})
        for (const double driveDb : {0.0, 6.0, 24.0})
        {
            const double drive = fromDb(driveDb);
            const double bias = mode == SaturationMode::Warm ? Saturation::kWarmBias : 0.0;
            const double amplitude = fromDb(Saturation::kCalibrationDbfs);
            constexpr int kPoints = 100000;
            double sum = 0.0;
            double sumSquares = 0.0;
            for (int k = 0; k < kPoints; ++k)
            {
                const double v =
                    std::tanh(drive * amplitude * std::sin(2.0 * kPi * k / kPoints) + bias) - std::tanh(bias);
                sum += v;
                sumSquares += v * v;
            }
            const double rms = std::sqrt(sumSquares / kPoints - (sum / kPoints) * (sum / kPoints));
            CAPTURE(mode, driveDb);
            const double expected = rms / (amplitude / std::sqrt(2.0));
            CHECK_THAT(Saturation::compensationGain(mode, driveDb), WithinAbs(expected, 1e-12));
        }
    // Fuera de rango, se limita.
    CHECK(Saturation::compensationGain(SaturationMode::Soft, 100.0) ==
          Saturation::compensationGain(SaturationMode::Soft, Saturation::kMaxDriveDb));
    CHECK(Saturation::compensationGain(SaturationMode::Soft, -5.0) ==
          Saturation::compensationGain(SaturationMode::Soft, 0.0));
}

TEST_CASE("Saturacion: con la mezcla a 0 la salida es la entrada por los filtros")
{
    // Respuesta en magnitud de 20 Hz a 18 kHz, con frecuencias enteras (un
    // número entero de periodos en 1 s); en fase lineal, además, la entrada
    // retrasada la latencia.
    const auto filter = GENERATE(OversamplingFilter::Iir, OversamplingFilter::LinearPhase);
    const double rate = GENERATE(44100.0, 48000.0, 96000.0, 192000.0);
    const auto window = static_cast<std::size_t>(rate);
    const std::size_t start = window / 4;
    double worst = 0.0;
    for (const double hz : {20.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 17000.0, 18000.0})
    {
        Saturation engine = makeEngine(SaturationMode::Warm, 24.0, 0.0, filter, rate);
        const Signal x = sine(start + window, hz, 0.5, rate);
        const Signal y = runMono(engine, x);
        const double gainDb = toDb(powerAt(y, start, window, hz, rate) / powerAt(x, start, window, hz, rate));
        CAPTURE(filter, rate, hz, gainDb);
        CHECK(std::abs(gainDb) < 0.1);
        worst = std::max(worst, std::abs(gainDb));
        if (filter == OversamplingFilter::LinearPhase)
        {
            const auto latency = static_cast<std::size_t>(engine.latency());
            double error = 0.0;
            for (std::size_t i = start; i < start + window; ++i)
                error = std::max(error, static_cast<double>(std::abs(y[i] - x[i - latency])));
            CHECK(20.0 * std::log10(error / 0.5) < -80.0);
        }
    }
    CHECK(worst < 0.001);
}

TEST_CASE("Saturacion: no depende del tamano de bloque")
{
    const auto filter = GENERATE(OversamplingFilter::Iir, OversamplingFilter::LinearPhase);
    const Signal input = noise(20000, 0.25, 7);
    const Signal other = noise(20000, 0.25, 8);
    const auto render = [&](int blockSize) {
        Saturation engine;
        engine.setFilter(filter);
        engine.prepare(kFs);
        // Rampas y fundido en curso al empezar.
        engine.setMode(SaturationMode::Warm);
        engine.setDriveDb(15.0);
        engine.setMix(0.7);
        Signal left = input;
        Signal right = other;
        run(engine, left, right, blockSize);
        return std::pair(left, right);
    };
    const auto reference = render(4096);
    for (const int blockSize : {1, 7, 31, 64, 65, 333, 1024})
    {
        CAPTURE(blockSize);
        CHECK(render(blockSize) == reference);
    }
}

TEST_CASE("Saturacion: los cambios van con rampa y sin chasquidos")
{
    // Un seno grave: cualquier salto se ve en la diferencia entre muestras.
    constexpr std::size_t kSegment = 4800;
    const Signal x = sine(12 * kSegment, 50.0, 0.5);
    const auto maxStep = [](const Signal& y, std::size_t from, std::size_t to) {
        float step = 0.0f;
        for (std::size_t i = from + 1; i < to; ++i)
            step = std::max(step, std::abs(y[i] - y[i - 1]));
        return step;
    };
    Saturation engine;
    engine.prepare(kFs);
    Signal y = x;
    const auto segment = [&](std::size_t index) {
        float* channels[] = {y.data() + index * kSegment};
        engine.process(channels, 1, static_cast<int>(kSegment));
    };
    segment(0); // Off.
    engine.setMode(SaturationMode::Soft);
    segment(1);
    segment(2);
    engine.setDriveDb(24.0);
    segment(3);
    segment(4);
    engine.setMode(SaturationMode::Warm);
    segment(5);
    engine.setMix(0.3);
    segment(6);
    engine.setDriveDb(0.0);
    segment(7);
    engine.setMix(1.0);
    engine.setMode(SaturationMode::Soft);
    segment(8);
    engine.setMode(SaturationMode::Off);
    segment(9);
    segment(10);
    CHECK_FALSE(engine.isActive());

    // El paso más grande en un tramo estable (la saturación con 24 dB de drive
    // da los flancos más empinados) acota el de los cambios.
    const float steady = std::max({maxStep(y, 0, kSegment), maxStep(y, 2 * kSegment, 3 * kSegment),
                                   maxStep(y, 4 * kSegment, 5 * kSegment), maxStep(y, 10 * kSegment, 11 * kSegment)});
    for (std::size_t k = 0; k < 11; ++k)
        UNSCOPED_INFO("tramo " << k << ": " << maxStep(y, k * kSegment, (k + 1) * kSegment));
    CHECK(maxStep(y, 0, 11 * kSegment) <= 1.05f * steady);
}

TEST_CASE("Saturacion: un NaN o un infinito no la estropea")
{
    const auto filter = GENERATE(OversamplingFilter::Iir, OversamplingFilter::LinearPhase);
    const auto bad = GENERATE(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
    Saturation engine = makeEngine(SaturationMode::Warm, 12.0, 0.8, filter);
    Signal x = sine(9600, 1000.0, 0.3);
    x[1000] = bad;
    const Signal y = runMono(engine, x, 64);
    CHECK(std::all_of(y.begin(), y.end(), [](float v) { return std::isfinite(v); }));
    // Silencio solo en el tramo que lo trae (64 muestras) y luego suena.
    CHECK(std::all_of(y.begin() + 960, y.begin() + 1024, [](float v) { return v == 0.0f; }));
    CHECK(meanSquare(y, 4800, 4800) > 0.01);
}

TEST_CASE("Saturacion: reset vuelve a empezar de cero")
{
    const Signal input = noise(5000, 0.3, 3);
    Saturation engine = makeEngine(SaturationMode::Soft, 9.0, 0.5, OversamplingFilter::LinearPhase);
    const Signal first = runMono(engine, input);
    engine.reset();
    CHECK(runMono(engine, input) == first);
}

TEST_CASE("Oversampler: las imagenes al subir quedan por debajo de -100 dB")
{
    // Un seno de 18 kHz a 48 kHz subido ×2, ×4 y ×8: todo lo que no es él son imágenes.
    const auto filter = GENERATE(OversamplingFilter::Iir, OversamplingFilter::LinearPhase);
    const int stages = GENERATE(1, 2, 3);
    Oversampler oversampler;
    oversampler.setup(filter, stages);
    const auto factor = static_cast<std::size_t>(oversampler.factor());
    REQUIRE(factor == (1u << stages));
    const Signal x = sine(48000, 18000.0, 0.5);
    Signal up(x.size() * factor);
    for (std::size_t position = 0; position < x.size(); position += 48)
    {
        const float* in[] = {x.data() + position};
        float* out[] = {up.data() + factor * position};
        oversampler.upsample(in, out, 1, 48);
    }
    const std::size_t start = 24000 * factor;
    const std::size_t length = 4800 * factor; // 1800 periodos de 18 kHz.
    const double tone = powerAt(up, start, length, 18000.0, static_cast<double>(factor) * kFs);
    const double images = meanSquare(up, start, length) - tone;
    CAPTURE(filter, stages, toDb(images / tone));
    CHECK(toDb(images / tone) < -100.0);
}

TEST_CASE("Saturacion: las llamadas del hilo de audio no asignan")
{
    // Con RealtimeSanitizer (job rtsan del CI), cualquier asignación o
    // bloqueo dentro de estas llamadas aborta el test.
    Saturation engine;
    engine.prepare(96000.0);
    Signal left = noise(3000, 0.3, 4);
    Signal right = noise(3000, 0.3, 5);
    float* channels[] = {left.data(), right.data()};
    for (const auto filter : {OversamplingFilter::Iir, OversamplingFilter::LinearPhase})
    {
        engine.setFilter(filter);
        engine.setMode(SaturationMode::Soft);
        engine.setDriveDb(20.0);
        engine.setMix(0.4);
        engine.process(channels, 2, 1000);
        engine.setMode(SaturationMode::Warm);
        engine.process(channels, 2, 1000);
        engine.setMode(SaturationMode::Off);
        engine.process(channels, 2, 3000);
        engine.reset();
    }
    CHECK(Saturation::compensationGain(SaturationMode::Warm, 3.0) > 0.0);
}
