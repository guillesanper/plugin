// SPDX-License-Identifier: MIT
#include "esceq/dsp/fft.h"
#include "esceq/dsp/spectrum_analyzer.h"
#include "esceq/dsp/spsc_fifo.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <thread>
#include <limits>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;

TEST_CASE("Fft: coincide con la DFT directa")
{
    for (int order = 1; order <= 10; ++order)
    {
        Fft fft(order);
        const int n = fft.size();
        REQUIRE(n == (1 << order));

        std::vector<std::complex<float>> data(static_cast<std::size_t>(n));
        std::uint32_t rng = 17u;
        for (auto& x : data)
        {
            rng = rng * 1664525u + 1013904223u;
            const float re = static_cast<float>(rng >> 8) / 8388608.0f - 1.0f;
            rng = rng * 1664525u + 1013904223u;
            const float im = static_cast<float>(rng >> 8) / 8388608.0f - 1.0f;
            x = {re, im};
        }

        auto result = data;
        fft.forward(result.data());

        for (int k = 0; k < n; ++k)
        {
            std::complex<double> expected{0.0, 0.0};
            for (int t = 0; t < n; ++t)
                expected += std::complex<double>(data[static_cast<std::size_t>(t)]) *
                            std::polar(1.0, -2.0 * std::numbers::pi * k * t / n);

            INFO("orden " << order << " bin " << k);
            CHECK_THAT(result[static_cast<std::size_t>(k)].real(), WithinAbs(expected.real(), 1e-3 * n));
            CHECK_THAT(result[static_cast<std::size_t>(k)].imag(), WithinAbs(expected.imag(), 1e-3 * n));
        }
    }
}

TEST_CASE("Fft: la inversa deshace la directa multiplicada por N")
{
    for (int order : {1, 5, 12})
    {
        Fft fft(order);
        const int n = fft.size();
        std::vector<std::complex<float>> data(static_cast<std::size_t>(n));
        std::uint32_t rng = 99u;
        for (auto& x : data)
        {
            rng = rng * 1664525u + 1013904223u;
            x = {static_cast<float>(rng >> 8) / 8388608.0f - 1.0f, static_cast<float>(rng & 0xff) / 128.0f - 1.0f};
        }
        auto result = data;
        fft.forward(result.data());
        fft.inverse(result.data());
        for (std::size_t i = 0; i < data.size(); ++i)
        {
            INFO("orden " << order << " muestra " << i);
            CHECK_THAT(result[i].real() / static_cast<float>(n), WithinAbs(data[i].real(), 1e-5));
            CHECK_THAT(result[i].imag() / static_cast<float>(n), WithinAbs(data[i].imag(), 1e-5));
        }
    }
}

TEST_CASE("Fft: la inversa usa el signo positivo del exponente")
{
    // Un solo bin en k = 1 da e^(+2πi·n/N) en el tiempo.
    FftDouble fft(4);
    std::vector<std::complex<double>> data(16);
    data[1] = 1.0;
    fft.inverse(data.data());
    for (int t = 0; t < 16; ++t)
    {
        const auto expected = std::polar(1.0, 2.0 * std::numbers::pi * t / 16.0);
        CHECK_THAT(data[static_cast<std::size_t>(t)].real(), WithinAbs(expected.real(), 1e-12));
        CHECK_THAT(data[static_cast<std::size_t>(t)].imag(), WithinAbs(expected.imag(), 1e-12));
    }
}

TEST_CASE("Fft: en double el error de una FFT larga es del orden del redondeo")
{
    // 2^16 puntos, como al diseñar el FIR de 32768 coeficientes.
    FftDouble fft(16);
    const int n = fft.size();
    std::vector<std::complex<double>> data(static_cast<std::size_t>(n));
    std::uint32_t rng = 7u;
    for (auto& x : data)
    {
        rng = rng * 1664525u + 1013904223u;
        x = static_cast<double>(rng >> 8) / 8388608.0 - 1.0;
    }
    auto result = data;
    fft.forward(result.data());
    fft.inverse(result.data());
    double worst = 0.0;
    for (std::size_t i = 0; i < data.size(); ++i)
        worst = std::max(worst, std::abs(result[i] / static_cast<double>(n) - data[i]));
    CHECK(worst < 1e-13);
}

namespace {

std::vector<float> sine(int numSamples, double hz, double amplitude, double fs)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / fs));
    return out;
}

float levelAt(SpectrumAnalyzer& analyzer, double hz)
{
    const double frequencies[] = {hz};
    float level = 0.0f;
    analyzer.compute(frequencies, std::span<float>(&level, 1));
    return level;
}

} // namespace

TEST_CASE("SpectrumAnalyzer: un seno de amplitud 1 se lee a 0 dB")
{
    constexpr double fs = 48000.0;
    SpectrumAnalyzer analyzer(12);
    analyzer.setSampleRate(fs);

    // Justo en un bin: lectura exacta.
    const double onBin = fs * 100.0 / 4096.0;
    analyzer.push(sine(8192, onBin, 1.0, fs).data(), 8192);
    CHECK_THAT(levelAt(analyzer, onBin), WithinAbs(0.0, 0.05));

    // Amplitud 0,5 = -6 dB, en una frecuencia cualquiera (pérdida de Hann <= 1,42 dB).
    analyzer.push(sine(8192, 1234.5, 0.5, fs).data(), 8192);
    CHECK_THAT(levelAt(analyzer, 1234.5), WithinAbs(-6.02, 1.5));

    // Una octava más allá apenas hay nada.
    CHECK(levelAt(analyzer, 2469.0) < -60.0f);
    CHECK(levelAt(analyzer, 617.0) < -60.0f);
}

TEST_CASE("SpectrumAnalyzer: inclinacion y silencio")
{
    SpectrumAnalyzer analyzer(11);
    analyzer.setSampleRate(44100.0);
    analyzer.push(sine(4096, 2000.0, 1.0, 44100.0).data(), 4096);

    const float flat = levelAt(analyzer, 2000.0);
    analyzer.setTiltDbPerOctave(4.5);
    CHECK_THAT(levelAt(analyzer, 2000.0), WithinAbs(flat + 4.5, 1e-3)); // una octava por encima de 1 kHz

    analyzer.clear();
    CHECK(levelAt(analyzer, 2000.0) == SpectrumAnalyzer::kFloorDb);
}

TEST_CASE("SpscFifo: escribe, lee y descarta lo que no cabe")
{
    SpscFifo fifo(10); // se redondea a 16
    CHECK(fifo.capacity() == 16);

    std::vector<float> in(20);
    for (std::size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<float>(i);

    CHECK(fifo.write(in.data(), 20) == 16);
    CHECK(fifo.available() == 16);

    std::vector<float> out(20, -1.0f);
    CHECK(fifo.read(out.data(), 10) == 10);
    CHECK(out[0] == 0.0f);
    CHECK(out[9] == 9.0f);

    CHECK(fifo.write(in.data(), 5) == 5); // da la vuelta al búfer
    CHECK(fifo.read(out.data(), 20) == 11);
    CHECK(out[5] == 15.0f);
    CHECK(out[6] == 0.0f);
    CHECK(out[10] == 4.0f);
    CHECK(fifo.available() == 0);
}

TEST_CASE("SpscFifo: writeMix promedia los canales")
{
    SpscFifo fifo(8);
    const float left[] = {1.0f, 0.0f};
    const float right[] = {0.0f, -1.0f};
    const float* channels[] = {left, right};
    CHECK(fifo.writeMix(channels, 2, 2) == 2);

    float out[2] = {};
    CHECK(fifo.read(out, 2) == 2);
    CHECK(out[0] == 0.5f);
    CHECK(out[1] == -0.5f);
}

TEST_CASE("SpscFifo: un productor y un consumidor en hilos distintos")
{
    SpscFifo fifo(1024);
    constexpr int total = 200000;

    std::thread producer([&fifo] {
        int next = 0;
        float block[64];
        while (next < total)
        {
            int count = 0;
            for (; count < 64 && next + count < total; ++count)
                block[count] = static_cast<float>(next + count);
            next += fifo.write(block, count);
        }
    });

    int expected = 0;
    bool ordered = true;
    float block[100];
    while (expected < total)
    {
        const int n = fifo.read(block, 100);
        for (int i = 0; i < n; ++i)
            ordered = ordered && block[i] == static_cast<float>(expected++);
    }
    producer.join();

    CHECK(ordered);
    CHECK(fifo.available() == 0);
}

TEST_CASE("Analizador: una muestra no finita cuenta como silencio")
{
    SpectrumAnalyzer analyzer(12);
    analyzer.setSampleRate(48000.0);
    std::vector<float> samples(4096, 0.0f);
    samples[10] = std::numeric_limits<float>::quiet_NaN();
    samples[20] = std::numeric_limits<float>::infinity();
    analyzer.push(samples.data(), static_cast<int>(samples.size()));
    const std::vector<double> frequencies{100.0, 1000.0, 10000.0};
    std::vector<float> levels(frequencies.size());
    analyzer.compute(frequencies, levels);
    CHECK(std::all_of(levels.begin(), levels.end(), [](float x) { return std::isfinite(x); }));
}
