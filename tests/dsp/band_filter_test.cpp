// SPDX-License-Identifier: MIT
#include "esceq/dsp/band_filter.h"

#include "filter_test_utils.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;
using namespace esceq::test;

namespace {

/// Ruido blanco determinista en [-1, 1].
std::vector<float> noise(int numSamples, std::uint32_t seed = 12345u)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples));
    std::uint32_t state = seed;
    for (auto& sample : out)
    {
        state = state * 1664525u + 1013904223u;
        sample = static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
    return out;
}

std::vector<float> run(BandFilter& filter, std::vector<float> signal, int blockSize)
{
    const int total = static_cast<int>(signal.size());
    for (int pos = 0; pos < total; pos += blockSize)
    {
        float* channel = signal.data() + pos;
        filter.process(&channel, 1, std::min(blockSize, total - pos));
    }
    return signal;
}

} // namespace

// Criterio de salida de la fase 1: lo que se oye (medido sobre el audio
// procesado) coincide con la curva que se dibuja (calculada), con ±0,1 dB.
TEST_CASE("BandFilter: la respuesta medida coincide con la teorica en +-0,1 dB")
{
    const double probes[] = {20.0, 55.0, 150.0, 400.0, 1000.0, 2500.0, 6000.0, 12000.0, 19000.0};
    double worst = 0.0;
    int comparisons = 0;

    for (auto type : kAllTypes)
    {
        const bool isCut = type == FilterType::LowCut || type == FilterType::HighCut;
        const std::vector<double> gains = isCut ? std::vector<double>{0.0} : std::vector<double>{-12.0, 9.0};
        const std::vector<int> slopes = isCut ? std::vector<int>{6, 18, 48, 96} : std::vector<int>{12};

        for (double sampleRate : {44100.0, 48000.0, 96000.0, 192000.0})
            for (double frequency : {40.0, 1000.0, 12000.0})
                for (double q : {0.4, 0.7071, 3.0})
                    for (double gain : gains)
                        for (int slope : slopes)
                        {
                            // A 192 kHz basta una muestra reducida de casos.
                            if (sampleRate > 100000.0 && (frequency < 100.0 || q < 0.5))
                                continue;
                            // Los casos graves necesitan respuestas largas: solo a las frecuencias de muestreo bajas.
                            if (sampleRate > 50000.0 && frequency < 100.0)
                                continue;

                            // Los filtros graves y resonantes tardan más en extinguirse.
                            const double seconds = frequency < 100.0 ? 4.0 : 0.5;
                            const auto design = designBand({type, frequency, gain, q, slope}, sampleRate);
                            const auto ir = impulseResponse(design, static_cast<int>(sampleRate * seconds));

                            INFO(typeName(type) << " fs " << sampleRate << " f " << frequency << " q " << q << " gain "
                                                << gain << " slope " << slope);

                            // La respuesta al impulso tiene que haberse extinguido para que la medida valga.
                            REQUIRE(std::abs(ir.back()) < 1.0e-6f);

                            for (double probe : probes)
                            {
                                const double expected = magnitudeDb(design, probe, sampleRate);
                                if (expected < -60.0)
                                    continue; // por debajo de la precisión de una medida en float
                                const double measured = measuredMagnitudeDb(ir, probe, sampleRate);
                                worst = std::max(worst, std::abs(measured - expected));
                                ++comparisons;

                                INFO("a " << probe << " Hz");
                                REQUIRE_THAT(measured, WithinAbs(expected, 0.1));
                            }
                        }
    }

    INFO("peor desviación: " << worst << " dB en " << comparisons << " comparaciones");
    CHECK(comparisons > 3000);
    CHECK(worst < 0.1);
    std::printf("respuesta medida vs teorica: peor desviacion %.6f dB en %d comparaciones\n", worst, comparisons);
}

TEST_CASE("BandFilter: una campana a 0 dB deja pasar el audio bit a bit")
{
    BandFilter filter;
    filter.setDesign(designBand({FilterType::Bell, 1000.0, 0.0, 1.0}, 48000.0));

    const auto input = noise(4096);
    CHECK(run(filter, input, 128) == input);
}

TEST_CASE("BandFilter: sin diseno asignado no altera el audio")
{
    BandFilter filter;
    const auto input = noise(1024);
    CHECK(run(filter, input, 64) == input);
}

TEST_CASE("BandFilter: el resultado no depende del tamano de bloque")
{
    for (auto type : kAllTypes)
    {
        const auto design = designBand({type, 2000.0, 6.0, 2.0, 42}, 48000.0);
        const auto input = noise(8192);

        BandFilter reference;
        reference.setDesign(design);
        const auto expected = run(reference, input, 8192);

        for (int blockSize : {1, 3, 64, 500, 1024})
        {
            BandFilter filter;
            filter.setDesign(design);
            INFO(typeName(type) << " bloque " << blockSize);
            CHECK(run(filter, input, blockSize) == expected);
        }
    }
}

TEST_CASE("BandFilter: los canales son independientes")
{
    BandFilter filter;
    filter.setDesign(designBand({FilterType::LowCut, 500.0, 0.0, 1.0, 24}, 48000.0));

    auto left = noise(2048, 1u);
    auto right = noise(2048, 2u);
    float* channels[] = {left.data(), right.data()};
    filter.process(channels, 2, 2048);

    BandFilter mono;
    mono.setDesign(filter.design());
    CHECK(run(mono, noise(2048, 1u), 2048) == left);
    mono.reset();
    CHECK(run(mono, noise(2048, 2u), 2048) == right);
}

TEST_CASE("BandFilter: reset borra la memoria del filtro")
{
    BandFilter filter;
    filter.setDesign(designBand({FilterType::Bell, 200.0, 12.0, 8.0}, 48000.0));

    const auto first = run(filter, noise(1024), 256);
    filter.reset();
    CHECK(run(filter, noise(1024), 256) == first);
}

TEST_CASE("BandFilter: tras el silencio la salida se extingue y no hay NaN")
{
    BandFilter filter;
    filter.setDesign(designBand({FilterType::Bell, 30.0, 18.0, 5.0}, 44100.0));
    (void) run(filter, noise(44100), 512);

    const auto tail = run(filter, std::vector<float>(44100 * 4, 0.0f), 512);
    CHECK(std::all_of(tail.begin(), tail.end(), [](float v) { return std::isfinite(v); }));
    CHECK(std::abs(tail.back()) < 1.0e-6f);
}

// Un EQ dinámico mueve sus filtros a velocidad de audio. La topología SVF
// debe aguantarlo sin dispararse, a diferencia de un biquad en forma directa.
TEST_CASE("BandFilter: es estable cambiando los coeficientes en cada muestra")
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 48000;

    for (auto type : kAllTypes)
    {
        BandFilter filter;
        float peak = 0.0f;
        std::uint32_t rng = 99u;

        for (int i = 0; i < numSamples; ++i)
        {
            // Barrido rápido de frecuencia y ganancia, con saltos bruscos de Q.
            const double t = static_cast<double>(i) / numSamples;
            const double frequency = 40.0 * std::pow(400.0, 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * 37.0 * t));
            const double gain = 18.0 * std::sin(2.0 * std::numbers::pi * 53.0 * t);
            rng = rng * 1664525u + 1013904223u;
            const double q = (rng >> 31) != 0u ? 0.3 : 12.0;

            filter.setDesign(designBand({type, frequency, gain, q, 48}, sampleRate));

            float sample = static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * i / sampleRate));
            float* channel = &sample;
            filter.process(&channel, 1, 1);

            REQUIRE(std::isfinite(sample));
            peak = std::max(peak, std::abs(sample));
        }

        INFO(typeName(type) << " pico " << peak);
        // Entrada de pico 1 con hasta +18 dB (x8) y resonancias de Q 12:
        // cualquier valor muy por encima indica inestabilidad.
        CHECK(peak < 200.0f);
    }
}

TEST_CASE("BandFilter: cambiar el numero de secciones no deja estado residual")
{
    const auto steep = designBand({FilterType::HighCut, 1000.0, 0.0, 1.0, 96}, 48000.0);
    const auto gentle = designBand({FilterType::HighCut, 1000.0, 0.0, 1.0, 12}, 48000.0);

    BandFilter filter;
    filter.setDesign(steep);
    (void) run(filter, noise(2048), 512);

    // Pasa a una sección y la deja extinguirse: no debe quedar memoria en
    // ninguna de las ocho al volver a la pendiente máxima.
    filter.setDesign(gentle);
    (void) run(filter, std::vector<float>(48000, 0.0f), 512);
    filter.setDesign(steep);

    const auto out = run(filter, std::vector<float>(256, 0.0f), 64);
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return std::abs(v) < 1.0e-6f; }));
}
