// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/control_thread.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>
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

TEST_CASE("Hilo de control: en manual, solo flush disena")
{
    dsp::PhaseEqualizer eq;
    dsp::AutoGain autoGain;
    eq.setBand(0, bell(1000.0, 6.0));
    eq.setPhaseMode(dsp::PhaseMode::LinearMedium);
    eq.prepare(kFs);
    core::ControlThread designer(eq, autoGain, core::ControlThread::Threading::Manual);

    std::uint32_t seed = 1u;
    for (int i = 0; i < 20; ++i)
        processBlock(eq, seed);
    CHECK(eq.activePhaseMode() == dsp::PhaseMode::Iir);

    designer.flush();
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));
    CHECK(eq.activePhaseMode() == dsp::PhaseMode::LinearMedium);
    CHECK(eq.latencySamples() == dsp::latencySamples(dsp::PhaseMode::LinearMedium, kFs));
}

TEST_CASE("Hilo de control: al preparar, flush deja el modo listo sin procesar audio")
{
    // Lo que hace el plugin en prepareToPlay: así informa al host de la latencia
    // correcta antes del primer bloque.
    dsp::PhaseEqualizer eq;
    dsp::AutoGain autoGain;
    eq.setPhaseMode(dsp::PhaseMode::LinearHigh);
    core::ControlThread designer(eq, autoGain);
    eq.prepare(kFs);
    designer.flush();
    eq.update();
    CHECK(eq.activePhaseMode() == dsp::PhaseMode::LinearHigh);
    CHECK(eq.latencySamples() == 17408);
}

TEST_CASE("Hilo de control: el hilo propio entrega los cambios de modo y de ajustes")
{
    dsp::PhaseEqualizer eq;
    dsp::AutoGain autoGain;
    eq.setBand(0, bell(1000.0, 6.0));
    eq.prepare(kFs);
    core::ControlThread designer(eq, autoGain);

    eq.setPhaseMode(dsp::PhaseMode::LinearLow);
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));
    CHECK(eq.activePhaseMode() == dsp::PhaseMode::LinearLow);

    eq.setBand(0, bell(3000.0, -4.0));
    CHECK(!eq.isSettled());
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));

    eq.setPhaseMode(dsp::PhaseMode::Iir);
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));
    CHECK(eq.latencySamples() == 0);
}

TEST_CASE("Hilo de control: audio y disenos a la vez, con ajustes cambiando en cada bloque")
{
    // Con ThreadSanitizer, este test comprueba que el intercambio entre los
    // dos hilos no tiene carreras (ver DEV.md).
    dsp::PhaseEqualizer eq;
    dsp::AutoGain autoGain;
    eq.setPhaseMode(dsp::PhaseMode::LinearLow);
    eq.prepare(kFs);
    core::ControlThread designer(eq, autoGain);

    std::uint32_t seed = 7u;
    const dsp::PhaseMode modes[] = {dsp::PhaseMode::LinearLow, dsp::PhaseMode::LinearMedium, dsp::PhaseMode::Iir,
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

TEST_CASE("Hilo de control: se para enseguida al destruirlo")
{
    dsp::PhaseEqualizer eq;
    dsp::AutoGain autoGain;
    const auto start = Clock::now();
    {
        core::ControlThread designer(eq, autoGain);
    }
    CHECK(Clock::now() - start < std::chrono::seconds(1));
}

TEST_CASE("Hilo de control: en manual, flush calcula la compensacion pendiente")
{
    dsp::PhaseEqualizer eq;
    dsp::AutoGain autoGain;
    core::ControlThread control(eq, autoGain, core::ControlThread::Threading::Manual);
    const dsp::BandSettings bands[] = {bell(1000.0, 6.0)};
    autoGain.request(bands, kFs, 2);
    CHECK_FALSE(autoGain.latestDb().has_value());
    control.flush();
    REQUIRE(autoGain.latestDb().has_value());
    CHECK(*autoGain.latestDb() == static_cast<float>(dsp::autoGainDb(bands, kFs, 2)));
}

TEST_CASE("Hilo de control: el hilo propio calcula la compensacion")
{
    // Con ThreadSanitizer, comprueba también la entrega de la compensación.
    dsp::PhaseEqualizer eq;
    dsp::AutoGain autoGain;
    core::ControlThread control(eq, autoGain);
    const auto limit = Clock::now() + std::chrono::seconds(10);
    for (int i = 0; i < 200 && Clock::now() < limit; ++i)
    {
        const dsp::BandSettings bands[] = {bell(500.0 + 10.0 * i, 6.0)};
        autoGain.request(bands, kFs, 2);
        (void)autoGain.latestDb();
    }
    const dsp::BandSettings last[] = {bell(1000.0, -6.0)};
    autoGain.request(last, kFs, 2);
    const float expected = static_cast<float>(dsp::autoGainDb(last, kFs, 2));
    while (autoGain.latestDb() != expected && Clock::now() < limit)
        std::this_thread::yield();
    CHECK(autoGain.latestDb() == expected);
}
