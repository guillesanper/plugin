// SPDX-License-Identifier: MIT
#include "esceq/dsp/gain_ramp.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using esceq::dsp::GainRamp;

namespace {

std::vector<float> testSignal(int numSamples)
{
    std::vector<float> signal(static_cast<std::size_t>(numSamples));
    for (std::size_t i = 0; i < signal.size(); ++i)
        signal[i] = std::sin(0.05f * static_cast<float>(i)) + 0.25f;
    return signal;
}

/// Procesa `signal` en mono troceándolo en bloques de `blockSize`.
std::vector<float> processInBlocks(GainRamp ramp, std::vector<float> signal, int blockSize)
{
    const int total = static_cast<int>(signal.size());
    for (int pos = 0; pos < total; pos += blockSize)
    {
        float* channel = signal.data() + pos;
        ramp.process(&channel, 1, std::min(blockSize, total - pos));
    }
    return signal;
}

} // namespace

TEST_CASE("GainRamp: con ganancia 1 el audio pasa bit a bit")
{
    GainRamp ramp;
    ramp.prepare(48000.0, 0.02);

    const auto input = testSignal(1024);
    CHECK(processInBlocks(ramp, input, 64) == input);
}

TEST_CASE("GainRamp: llega exactamente al objetivo al acabar la rampa")
{
    GainRamp ramp;
    ramp.prepare(1000.0, 0.1); // rampa de 100 muestras
    ramp.setTarget(0.5f);
    REQUIRE(ramp.isRamping());

    std::vector<float> ones(100, 1.0f);
    float* channel = ones.data();
    ramp.process(&channel, 1, 100);

    CHECK_FALSE(ramp.isRamping());
    CHECK(ramp.current() == 0.5f);
    CHECK(ones.back() == 0.5f);

    // Monótona y sin saltos mayores que un paso de rampa.
    for (std::size_t i = 1; i < ones.size(); ++i)
    {
        CHECK(ones[i] <= ones[i - 1]);
        CHECK(ones[i - 1] - ones[i] < 0.0051f);
    }
}

TEST_CASE("GainRamp: el resultado no depende del tamaño de bloque")
{
    GainRamp ramp;
    ramp.prepare(48000.0, 0.02);
    ramp.setTarget(0.25f);

    const auto input = testSignal(4096);
    const auto reference = processInBlocks(ramp, input, 4096);

    for (int blockSize : {1, 7, 64, 512, 1000})
        CHECK(processInBlocks(ramp, input, blockSize) == reference);
}

TEST_CASE("GainRamp: aplica la misma ganancia a todos los canales")
{
    GainRamp ramp;
    ramp.prepare(48000.0, 0.02);
    ramp.setTarget(2.0f);

    auto left = testSignal(2048);
    auto right = left;
    float* channels[] = {left.data(), right.data()};
    ramp.process(channels, 2, 2048);

    CHECK(left == right);
    CHECK_THAT(left.back(), WithinAbs(2.0f * testSignal(2048).back(), 1e-6));
}

TEST_CASE("GainRamp: reset salta sin rampa")
{
    GainRamp ramp;
    ramp.prepare(48000.0, 0.02);
    ramp.reset(0.5f);

    CHECK_FALSE(ramp.isRamping());
    std::vector<float> ones(4, 1.0f);
    float* channel = ones.data();
    ramp.process(&channel, 1, 4);
    CHECK(ones == std::vector<float>(4, 0.5f));
}
