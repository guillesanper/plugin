// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/phase_design_thread.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace esceq;
using Clock = std::chrono::steady_clock;

namespace
{

constexpr double kFs = 48000.0;

dsp::BandSettings bell(double hz, double gain)
{
    return {true, {dsp::FilterType::Bell, hz, gain, 1.0}};
}

/// Procesa un bloque de ruido en el "hilo de audio" (el del test).
void processBlock(dsp::PhaseEqualizer& eq, std::uint32_t& seed, int size = 256)
{
    std::vector<float> left(static_cast<std::size_t>(size)), right(static_cast<std::size_t>(size));
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        left[i] = right[i] = 0.3f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
    float* channels[2] = {left.data(), right.data()};
    eq.process(channels, 2, size);
    for (std::size_t i = 0; i < left.size(); ++i)
        REQUIRE(std::isfinite(left[i]));
}

/// Procesa hasta que `done()` o pasen `seconds`. true si `done()`.
template <typename Done> bool processUntil(dsp::PhaseEqualizer& eq, Done done, double seconds = 10.0)
{
    std::uint32_t seed = 1u;
    const auto limit = Clock::now() + std::chrono::duration<double>(seconds);
    while (!done())
    {
        if (Clock::now() > limit)
            return false;
        processBlock(eq, seed);
    }
    return true;
}

} // namespace

TEST_CASE("Hilo de diseno: en manual, solo flush disena")
{
    dsp::PhaseEqualizer eq;
    eq.setBand(0, bell(1000.0, 6.0));
    eq.setPhaseMode(dsp::PhaseMode::LinearMedium);
    eq.prepare(kFs);
    core::PhaseDesignThread designer(eq, core::PhaseDesignThread::Threading::Manual);

    std::uint32_t seed = 1u;
    for (int i = 0; i < 20; ++i)
        processBlock(eq, seed);
    CHECK(eq.activePhaseMode() == dsp::PhaseMode::Natural);

    designer.flush();
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));
    CHECK(eq.activePhaseMode() == dsp::PhaseMode::LinearMedium);
    CHECK(eq.latencySamples() == dsp::latencySamples(dsp::PhaseMode::LinearMedium, kFs));
}

TEST_CASE("Hilo de diseno: al preparar, flush deja el modo listo sin procesar audio")
{
    // Lo que hace el plugin en prepareToPlay: así informa al host de la latencia
    // correcta antes del primer bloque.
    dsp::PhaseEqualizer eq;
    eq.setPhaseMode(dsp::PhaseMode::LinearHigh);
    core::PhaseDesignThread designer(eq);
    eq.prepare(kFs);
    designer.flush();
    eq.update();
    CHECK(eq.activePhaseMode() == dsp::PhaseMode::LinearHigh);
    CHECK(eq.latencySamples() == 17408);
}

TEST_CASE("Hilo de diseno: el hilo propio entrega los cambios de modo y de ajustes")
{
    dsp::PhaseEqualizer eq;
    eq.setBand(0, bell(1000.0, 6.0));
    eq.prepare(kFs);
    core::PhaseDesignThread designer(eq);

    eq.setPhaseMode(dsp::PhaseMode::LinearLow);
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));
    CHECK(eq.activePhaseMode() == dsp::PhaseMode::LinearLow);

    eq.setBand(0, bell(3000.0, -4.0));
    CHECK(!eq.isSettled());
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));

    eq.setPhaseMode(dsp::PhaseMode::Natural);
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));
    CHECK(eq.latencySamples() == 0);
}

TEST_CASE("Hilo de diseno: audio y disenos a la vez, con ajustes cambiando en cada bloque")
{
    // Con ThreadSanitizer, este test comprueba que el intercambio entre los
    // dos hilos no tiene carreras (ver DEV.md).
    dsp::PhaseEqualizer eq;
    eq.setPhaseMode(dsp::PhaseMode::LinearLow);
    eq.prepare(kFs);
    core::PhaseDesignThread designer(eq);

    std::uint32_t seed = 7u;
    const dsp::PhaseMode modes[] = {dsp::PhaseMode::LinearLow, dsp::PhaseMode::LinearMedium, dsp::PhaseMode::Natural,
                                    dsp::PhaseMode::LinearHigh};
    for (int block = 0; block < 1500; ++block)
    {
        eq.setBand(block % 4, bell(200.0 + 10.0 * block, (block % 7) - 3.0));
        if (block % 300 == 0)
            eq.setPhaseMode(modes[(block / 300) % 4]);
        processBlock(eq, seed, 64 + (block * 13) % 300);
    }
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));
}

TEST_CASE("Hilo de diseno: se para enseguida al destruirlo")
{
    dsp::PhaseEqualizer eq;
    const auto start = Clock::now();
    {
        core::PhaseDesignThread designer(eq);
    }
    CHECK(Clock::now() - start < std::chrono::seconds(1));
}
