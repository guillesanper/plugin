// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Bandas desde el espectro (PLAN.md 5.3, ADR 0019): detección de picos, su Q
// y los cambios que crean la banda. Los criterios de aceptación del plan se
// comprueban con espectros sintéticos (la forma exacta) y con el analizador
// de verdad (dsp::SpectrumAnalyzer, con su resolución).
#include "esceq/ui/spectrum_peaks.h"

#include "esceq/core/band_model.h"
#include "esceq/dsp/spectrum_analyzer.h"
#include "esceq/ui/curve_mapping.h"
#include "esceq/ui/meter_logic.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace esceq;
using namespace esceq::ui;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kPoints = 320;                  // los de la curva
constexpr double kTilt = 4.5;                 // la inclinación por defecto
constexpr double kSineTolerance = 1.0 / 48.0; // octavas

double octavesBetween(double a, double b)
{
    return std::abs(std::log2(a / b));
}

/// Niveles de la pantalla con una forma dada en el espectro real (en función
/// de log2(f)) y la inclinación del analizador sumada, como se pintan.
template <typename Shape>
std::vector<float> displayLevels(const std::vector<double>& frequencies, double tilt, Shape shape)
{
    std::vector<float> levels;
    for (const double f : frequencies)
        levels.push_back(static_cast<float>(shape(std::log2(f)) + dsp::SpectrumAnalyzer::tiltDb(f, tilt)));
    return levels;
}

/// Ruido gaussiano reproducible en cualquier plataforma (las distribuciones
/// de la biblioteca estándar no dan los mismos números en todas).
class Noise
{
public:
    double operator()()
    {
        // Box-Muller sobre un xorshift64.
        const double u1 = uniform();
        const double u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * std::numbers::pi * u2);
    }

private:
    double uniform()
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return (static_cast<double>(state_ >> 11) + 1.0) / 9007199254740993.0; // (0, 1]
    }
    std::uint64_t state_ = 0x9E3779B97F4A7C15ull;
};

/// Ruido rosa (filtro de Paul Kellet sobre ruido blanco), unos -3 dB/oct.
class PinkNoise
{
public:
    double operator()(double white)
    {
        b_[0] = 0.99886 * b_[0] + white * 0.0555179;
        b_[1] = 0.99332 * b_[1] + white * 0.0750759;
        b_[2] = 0.96900 * b_[2] + white * 0.1538520;
        b_[3] = 0.86650 * b_[3] + white * 0.3104856;
        b_[4] = 0.55000 * b_[4] + white * 0.5329522;
        b_[5] = -0.7616 * b_[5] - white * 0.0168980;
        const double out = b_[0] + b_[1] + b_[2] + b_[3] + b_[4] + b_[5] + b_[6] + white * 0.5362;
        b_[6] = white * 0.115926;
        return 0.11 * out;
    }

private:
    double b_[7]{};
};

/// Resonador: paso banda de segundo orden (0 dB en f0) con su Q.
class Resonator
{
public:
    Resonator(double f0, double q)
    {
        const double w = 2.0 * std::numbers::pi * f0 / kSampleRate;
        const double alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0_ = alpha / a0;
        a1_ = -2.0 * std::cos(w) / a0;
        a2_ = (1.0 - alpha) / a0;
    }
    double operator()(double x)
    {
        const double y = b0_ * x + z1_;
        z1_ = -a1_ * y + z2_;
        z2_ = -b0_ * x - a2_ * y;
        return y;
    }

private:
    double b0_ = 0.0, a1_ = 0.0, a2_ = 0.0, z1_ = 0.0, z2_ = 0.0;
};

/// Como la curva: el analizador real, un espectro cada 1600 muestras (30
/// por segundo a 48 kHz) y el promedio en el que se buscan los picos.
/// Devuelve los picos del último fotograma.
template <typename Source>
std::vector<SpectrumPeak> analyzedPeaks(int fftOrder, int frames, Source source)
{
    dsp::SpectrumAnalyzer analyzer(fftOrder);
    analyzer.setSampleRate(kSampleRate);
    analyzer.setTiltDbPerOctave(kTilt);
    const auto frequencies = logFrequencies(kPoints);
    std::vector<float> levels(frequencies.size());
    SpectrumAverage average(frequencies.size());
    std::vector<float> block(1600);
    for (int frame = 0; frame < frames; ++frame)
    {
        for (auto& sample : block)
            sample = static_cast<float>(source());
        analyzer.push(block.data(), static_cast<int>(block.size()));
        analyzer.compute(frequencies, levels);
        average.update(levels, 1.0 / 30.0);
    }
    return findPeaks(average.levels(), frequencies, {.tiltDbPerOctave = kTilt});
}

/// Picos de un seno de amplitud 0,5 en el analizador real (una sola FFT).
std::vector<SpectrumPeak> sinePeaks(dsp::SpectrumAnalyzer& analyzer, std::initializer_list<double> hz)
{
    std::vector<float> samples(static_cast<std::size_t>(analyzer.fftSize()), 0.0f);
    for (const double f : hz)
        for (std::size_t n = 0; n < samples.size(); ++n)
            samples[n] +=
                static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * f * static_cast<double>(n) / kSampleRate));
    analyzer.clear();
    analyzer.push(samples.data(), static_cast<int>(samples.size()));
    const auto frequencies = logFrequencies(kPoints);
    std::vector<float> levels(frequencies.size());
    analyzer.compute(frequencies, levels);
    return findPeaks(levels, frequencies, {.tiltDbPerOctave = kTilt});
}

} // namespace

TEST_CASE("Picos: un seno da un pico en su frecuencia, con el analizador")
{
    // Por encima de unos 450 Hz con 4096 puntos (y de 85 Hz con 16384), la
    // frecuencia queda a menos de 1/48 de octava. Más abajo manda la
    // resolución del analizador (medio bin), no la detección: ver docs/dsp/picos.md.
    for (const auto [order, lowest] : {std::pair{12, 500.0}, {14, 120.0}})
    {
        dsp::SpectrumAnalyzer analyzer(order);
        analyzer.setSampleRate(kSampleRate);
        analyzer.setTiltDbPerOctave(kTilt);
        for (int i = 0; i < 40; ++i)
        {
            const double f = lowest * std::pow(18000.0 / lowest, i / 39.0);
            CAPTURE(order, f);
            const auto peaks = sinePeaks(analyzer, {f});
            const auto peak = nearestPeak(peaks, f, 1.0);
            REQUIRE(peak.has_value());
            CHECK(octavesBetween(peak->frequencyHz, f) <= kSineTolerance);
            // Amplitud 0,5 = -6 dB, menos la pérdida de la ventana entre bins (hasta 1,42 dB).
            CHECK(peak->levelDb <= -6.0 + 0.01);
            CHECK(peak->levelDb >= -6.02 - 1.5);
            CHECK(peaks.size() == 1);
        }
    }
}

TEST_CASE("Picos: un seno da un pico en su frecuencia, entre puntos y en mesetas")
{
    const auto frequencies = logFrequencies(kPoints);
    for (int i = 0; i < 200; ++i)
    {
        // Frecuencias que caen en cualquier sitio entre dos puntos.
        const double f = 30.0 * std::pow(15000.0 / 30.0, i / 199.0);
        const double x0 = std::log2(f);
        CAPTURE(f);

        // Un lóbulo suave: lo resuelve la interpolación parabólica.
        const auto smooth =
            displayLevels(frequencies, kTilt, [x0](double x) { return -20.0 - 400.0 * (x - x0) * (x - x0); });
        auto peak = nearestPeak(findPeaks(smooth, frequencies, {.tiltDbPerOctave = kTilt}), f, 1.0);
        REQUIRE(peak.has_value());
        CHECK(octavesBetween(peak->frequencyHz, f) <= kSineTolerance);
        CHECK_THAT(peak->levelDb, WithinAbs(-20.0, 0.5));

        // Como lo pinta el analizador en agudos: el máximo de los bins en
        // ±1/24 de octava hace una meseta, y con la inclinación sería una
        // rampa. Sin quitarla, el pico se iría al borde de arriba.
        const auto plateau = displayLevels(frequencies, kTilt, [x0](double x) {
            const double outside = std::max(std::abs(x - x0) - 1.0 / 24.0, 0.0);
            return -20.0 - 600.0 * outside * outside;
        });
        peak = nearestPeak(findPeaks(plateau, frequencies, {.tiltDbPerOctave = kTilt}), f, 1.0);
        REQUIRE(peak.has_value());
        CHECK(octavesBetween(peak->frequencyHz, f) <= kSineTolerance);
        CHECK_THAT(peak->levelDb, WithinAbs(-20.0, 1e-3));
    }
}

TEST_CASE("Picos: dos senos separados media octava dan dos picos")
{
    dsp::SpectrumAnalyzer analyzer(12);
    analyzer.setSampleRate(kSampleRate);
    analyzer.setTiltDbPerOctave(kTilt);
    for (const double low : {700.0, 1000.0, 2500.0, 6000.0})
    {
        const double high = low * std::sqrt(2.0);
        CAPTURE(low);
        const auto peaks = sinePeaks(analyzer, {low, high});
        REQUIRE(peaks.size() == 2);
        CHECK(octavesBetween(peaks[0].frequencyHz, low) <= kSineTolerance);
        CHECK(octavesBetween(peaks[1].frequencyHz, high) <= kSineTolerance);
    }

    // Y con la forma exacta, en cualquier sitio del espectro.
    const auto frequencies = logFrequencies(kPoints);
    for (const double low : {60.0, 200.0, 1000.0, 9000.0})
    {
        const double x1 = std::log2(low);
        const double x2 = x1 + 0.5;
        const auto levels = displayLevels(frequencies, kTilt, [x1, x2](double x) {
            return -15.0 - 400.0 * std::min((x - x1) * (x - x1), (x - x2) * (x - x2));
        });
        const auto peaks = findPeaks(levels, frequencies, {.tiltDbPerOctave = kTilt});
        REQUIRE(peaks.size() == 2);
        CHECK(octavesBetween(peaks[0].frequencyHz, low) <= kSineTolerance);
        CHECK(octavesBetween(peaks[1].frequencyHz, low * std::sqrt(2.0)) <= kSineTolerance);
    }
}

TEST_CASE("Picos: el rizado del ruido rosa por debajo de la prominencia no da ninguno")
{
    // Ruido rosa (-3 dB/oct) con un rizado de 4,8 dB de pico a pico: con lo
    // que baja el rosa en 1/3 de octava (1 dB), por debajo de los 6 dB de
    // prominencia. Ningún pico, en ningún sitio.
    const auto frequencies = logFrequencies(kPoints);
    Noise noise;
    for (int trial = 0; trial < 50; ++trial)
    {
        std::vector<double> ripple;
        for (int i = 0; i < kPoints; ++i)
            ripple.push_back(2.4 * std::clamp(noise() / 2.0, -1.0, 1.0));
        std::size_t i = 0;
        const auto levels = displayLevels(frequencies, kTilt, [&](double x) {
            return -30.0 - 3.0 * (x - std::log2(1000.0)) + ripple[i++];
        });
        CHECK(findPeaks(levels, frequencies, {.tiltDbPerOctave = kTilt}).empty());
    }

    // Un pico de 7 dB sobre un fondo plano: ese sí.
    std::size_t i = 0;
    std::vector<double> flat(kPoints, 0.0);
    flat[160] = 7.0;
    const auto levels = displayLevels(frequencies, kTilt, [&](double) { return -30.0 + flat[i++]; });
    const auto peaks = findPeaks(levels, frequencies, {.tiltDbPerOctave = kTilt});
    REQUIRE(peaks.size() == 1);
    CHECK_THAT(peaks[0].frequencyHz, WithinRel(frequencies[160], 1e-9));
}

TEST_CASE("Picos: el ruido rosa en el analizador no da picos en el promedio")
{
    // Ruido rosa de verdad por el analizador (4096 puntos, la inclinación por
    // defecto) durante 6 s. En el espectro que se pinta aparecen picos de
    // azar (docs/dsp/picos.md); en el promedio de potencia, ninguno.
    Noise noise;
    PinkNoise pink;
    const auto peaks = analyzedPeaks(12, 180, [&] { return 0.3 * pink(noise()); });
    CHECK(peaks.empty());
}

TEST_CASE("Picos: el Q de una resonancia sintetica de Q conocido, dentro de un 25 %")
{
    // La forma exacta: un paso banda de Q conocido 24 dB sobre un suelo
    // plano, en frecuencias que caen entre puntos.
    const auto frequencies = logFrequencies(kPoints);
    for (const double f0 : {95.0, 1030.0, 7900.0})
        for (const double q : {4.0, 6.0, 10.0, 20.0})
        {
            CAPTURE(f0, q);
            const auto levels = displayLevels(frequencies, kTilt, [f0, q](double x) {
                const double ratio = std::exp2(x) / f0 - f0 / std::exp2(x);
                const double resonance = 1.0 / (1.0 + q * q * ratio * ratio);
                return 10.0 * std::log10(std::pow(10.0, -4.8) + std::pow(10.0, -2.4) * resonance);
            });
            const auto peak = nearestPeak(findPeaks(levels, frequencies, {.tiltDbPerOctave = kTilt}), f0);
            REQUIRE(peak.has_value());
            CHECK(peak->q >= 0.75 * q);
            CHECK(peak->q <= 1.25 * q);
        }
}

TEST_CASE("Picos: el Q de una resonancia en ruido, con el analizador, dentro de un 25 %")
{
    // Ruido blanco por un resonador, más un fondo de ruido rosa, por el
    // analizador real y el promedio (5 s). Q medio, donde el analizador lo
    // resuelve: el error de cada caso está en docs/dsp/picos.md.
    for (const double f0 : {1000.0, 3000.0})
        for (const double q : {6.0, 10.0})
        {
            CAPTURE(f0, q);
            Noise noise;
            PinkNoise pink;
            Resonator resonator(f0, q);
            const auto peaks = analyzedPeaks(12, 150, [&] { return 0.02 * pink(noise()) + 0.3 * resonator(noise()); });
            const auto peak = nearestPeak(peaks, f0);
            REQUIRE(peak.has_value());
            CHECK(octavesBetween(peak->frequencyHz, f0) <= 1.0 / 24.0); // el ruido la mueve algo más que a un seno
            CHECK(peak->q >= 0.75 * q);
            CHECK(peak->q <= 1.25 * q);
        }
}

TEST_CASE("Picos: prominencia, nivel minimo e inclinacion")
{
    const auto frequencies = logFrequencies(kPoints);
    const double x0 = std::log2(5000.0);
    auto bump = [&](double top, double depth, double tilt) {
        return displayLevels(frequencies, tilt, [=](double x) {
            return top - std::min(depth, 400.0 * (x - x0) * (x - x0));
        });
    };

    // 6 dB de prominencia en ±1/3 de octava: un pico de 5 dB no cuenta.
    CHECK(findPeaks(bump(-20.0, 6.5, 0.0), frequencies).size() == 1);
    CHECK(findPeaks(bump(-20.0, 5.0, 0.0), frequencies).empty());
    CHECK(findPeaks(bump(-20.0, 5.0, 0.0), frequencies, {.prominenceDb = 4.0}).size() == 1);

    // Por encima de -60 dB, medido sin la inclinación: a 5 kHz, con 4,5
    // dB/oct, -50 dB en la pantalla son -60,4 dB de verdad.
    CHECK(findPeaks(bump(-59.0, 20.0, 0.0), frequencies).size() == 1);
    CHECK(findPeaks(bump(-61.0, 20.0, 0.0), frequencies).empty());
    const auto tilted = bump(-50.0 - dsp::SpectrumAnalyzer::tiltDb(5000.0, kTilt), 20.0, kTilt);
    CHECK(findPeaks(tilted, frequencies, {.tiltDbPerOctave = kTilt}).empty());
    CHECK(findPeaks(tilted, frequencies).size() == 1); // sin quitarla, contaría
}

TEST_CASE("Picos: los extremos, los valores no finitos y las entradas invalidas no dan picos")
{
    const auto frequencies = logFrequencies(kPoints);

    // Lo más alto en un extremo no es un máximo local.
    auto rising = displayLevels(frequencies, 0.0, [](double x) { return -80.0 + 6.0 * x; });
    CHECK(findPeaks(rising, frequencies).empty());

    // Un punto no finito no es un pico ni rompe los demás.
    auto levels = displayLevels(frequencies, 0.0, [](double x) {
        const double d = x - std::log2(1000.0);
        return -20.0 - std::min(30.0, 400.0 * d * d);
    });
    levels[40] = std::numeric_limits<float>::infinity();
    levels[41] = std::numeric_limits<float>::quiet_NaN();
    const auto peaks = findPeaks(levels, frequencies);
    REQUIRE(peaks.size() == 1);
    CHECK(octavesBetween(peaks[0].frequencyHz, 1000.0) <= kSineTolerance);

    // Tamaños distintos, muy pocos puntos o frecuencias que no crecen.
    CHECK(findPeaks(std::span(levels).first(100), frequencies).empty());
    CHECK(findPeaks(std::vector<float>{-10.0f, 0.0f}, std::vector<double>{100.0, 200.0}).empty());
    CHECK(findPeaks(std::vector<float>{-20.0f, 0.0f, -20.0f}, std::vector<double>{100.0, 100.0, 200.0}).empty());
    CHECK(findPeaks(std::vector<float>{-20.0f, 0.0f, -20.0f}, std::vector<double>{0.0, 100.0, 200.0}).empty());
    CHECK(findPeaks(std::vector<float>{-20.0f, 0.0f, -20.0f}, std::vector<double>{100.0, 110.0, 120.0}).size() == 1);
}

TEST_CASE("Picos: el Q sale del ancho a -3 dB y se queda entre 0,5 y 30")
{
    CHECK_THAT(qForBandwidth(1.0), WithinRel(std::sqrt(2.0), 1e-12));
    CHECK_THAT(qForBandwidth(2.0), WithinRel(2.0 / 3.0, 1e-12));
    CHECK(std::isinf(qForBandwidth(0.0)));
    CHECK(std::isinf(qForBandwidth(-1.0)));

    // Un pico de un solo punto: tan estrecho como se ve, Q 30 como mucho.
    const auto frequencies = logFrequencies(kPoints);
    std::vector<float> spike(frequencies.size(), -40.0f);
    spike[200] = -10.0f;
    auto peaks = findPeaks(spike, frequencies);
    REQUIRE(peaks.size() == 1);
    CHECK(peaks[0].q == kPeakMaxQ);

    // Si por un lado vuelve a subir antes de bajar 3 dB (otro pico), el ancho
    // sale del otro lado, como si fuera simétrico: el Q no se va a un límite.
    std::vector<float> shoulder(frequencies.size(), -40.0f);
    for (const auto [i, level] : {std::pair{195, -30.0f}, {196, -11.0f}, {197, -10.0f}, {198, -12.0f}, {199, -12.5f},
                                  {200, -11.5f}})
        shoulder[static_cast<std::size_t>(i)] = level;
    peaks = findPeaks(shoulder, frequencies);
    REQUIRE(peaks.size() == 2);
    CHECK(octavesBetween(peaks[0].frequencyHz, frequencies[197]) < 0.5 * (std::log2(frequencies[1] / frequencies[0])));
    CHECK(peaks[0].q > kPeakMinQ);
    CHECK(peaks[0].q < kPeakMaxQ);
}

TEST_CASE("Picos: se marca el mas cercano a menos de 1/6 de octava")
{
    const std::vector<SpectrumPeak> peaks{{100.0, -20.0, 4.0}, {1000.0, -30.0, 8.0}, {1300.0, -25.0, 6.0}};
    CHECK(nearestPeak(peaks, 1100.0)->frequencyHz == 1000.0);
    CHECK(nearestPeak(peaks, 1200.0)->frequencyHz == 1300.0);
    CHECK(nearestPeak(peaks, 100.0 * std::exp2(1.0 / 6.0) * 0.999)->frequencyHz == 100.0);
    CHECK_FALSE(nearestPeak(peaks, 100.0 * std::exp2(1.0 / 6.0) * 1.01).has_value());
    CHECK_FALSE(nearestPeak(peaks, 5000.0).has_value());
    CHECK_FALSE(nearestPeak({}, 1000.0).has_value());
    CHECK_FALSE(nearestPeak(peaks, 0.0).has_value());
}

TEST_CASE("Picos: el marcador dice la frecuencia, la nota y el nivel")
{
    CHECK(peakMarkerText({440.0, -18.44, 4.0}, 440.0) == "440 Hz \xC2\xB7 A4 +0 ct \xC2\xB7 -18.4 dB");
    CHECK(peakMarkerText({1200.0, -6.0, 4.0}, 440.0) == "1.20 kHz \xC2\xB7 D6 +37 ct \xC2\xB7 -6.0 dB");
    CHECK(peakMarkerText({432.0, -0.04, 4.0}, 432.0) == "432 Hz \xC2\xB7 A4 +0 ct \xC2\xB7 0.0 dB");
}

TEST_CASE("Picos: la banda del pico es una campana en su frecuencia con su Q y la ganancia arrastrada")
{
    auto values = core::defaultValues();
    const int band = 3;
    // La banda guardaba ajustes de cuando estuvo activa: no los hereda.
    values[core::dynamicParameter(band, core::DynParam::Enabled)] = 1.0f;
    values[core::channelParameter(band)] = 2.0f;

    const SpectrumPeak peak{2345.0, -21.0, 7.5};
    const auto changes = peakBandChanges(values, band, peak, -4.5);
    REQUIRE_FALSE(changes.empty());
    CHECK(changes.back() == ParameterChange{core::bandParameter(band, core::BandParam::Enabled), 1.0f});
    for (const auto& change : changes)
        values[change.index] = change.value;

    const auto settings = core::bandSettings(values, band);
    CHECK(settings.enabled);
    CHECK(settings.filter.type == dsp::FilterType::Bell);
    CHECK_THAT(settings.filter.frequencyHz, WithinRel(2345.0, 1e-6));
    CHECK_THAT(settings.filter.q, WithinRel(7.5, 1e-6));
    CHECK(settings.filter.gainDb == -4.5);
    CHECK_FALSE(settings.dynamics.enabled);
    CHECK(settings.channel == dsp::ChannelMode::Stereo);

    // El Q del pico se ajusta al rango del parámetro.
    const auto wide = peakBandChanges(core::defaultValues(), 0, {500.0, -20.0, 0.01}, 0.0);
    const auto qIndex = core::bandParameter(0, core::BandParam::Q);
    for (const auto& change : wide)
        if (change.index == qIndex)
            CHECK(change.value == core::parameters()[qIndex].min);
}

TEST_CASE("Picos: con Alt la banda queda dinamica, 3 dB por debajo del pico y con el arrastre como rango")
{
    auto values = core::defaultValues();
    const int band = 5;
    const SpectrumPeak peak{800.0, -18.0, 5.0};
    for (const auto& change : peakBandChanges(values, band, peak, -7.5))
        values[change.index] = change.value;

    auto apply = [](core::ParameterValues v, const std::vector<ParameterChange>& changes) {
        for (const auto& change : changes)
            v[change.index] = change.value;
        return v;
    };
    const auto changes = dynamicPeakChanges(values, band, peak);
    REQUIRE_FALSE(changes.empty());
    CHECK(changes.back() == ParameterChange{core::dynamicParameter(band, core::DynParam::Enabled), 1.0f});
    auto settings = core::bandSettings(apply(values, changes), band);
    CHECK(settings.dynamics.enabled);
    CHECK(settings.dynamics.thresholdDb == -21.0);
    CHECK(settings.dynamics.rangeDb == -7.5);
    CHECK(settings.filter.gainDb == 0.0);
    CHECK_THAT(settings.filter.frequencyHz, WithinRel(800.0, 1e-6));
    CHECK_THAT(settings.filter.q, WithinRel(5.0, 1e-6));

    // Sin ganancia arrastrada, el rango por defecto; el umbral, en su rango.
    values[core::bandParameter(band, core::BandParam::Gain)] = 0.04f;
    settings = core::bandSettings(apply(values, dynamicPeakChanges(values, band, {800.0, -59.0, 5.0})), band);
    const auto range = core::dynamicParameter(band, core::DynParam::Range);
    CHECK(settings.dynamics.rangeDb == core::parameters()[range].defaultValue);
    const auto threshold = core::dynamicParameter(band, core::DynParam::Threshold);
    CHECK(settings.dynamics.thresholdDb == core::parameters()[threshold].min);
    CHECK(settings.filter.gainDb == 0.0);
}
