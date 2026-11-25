// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/control_thread.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
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

TEST_CASE("Hilo de control: avisa una vez de cada cambio de latencia y nunca sin cambios")
{
    // El plugin avisa al host con este aviso, no mirando con un temporizador:
    // sin cambios, el hilo de mensajes no recibe nada (ADR 0021).
    dsp::PhaseEqualizer eq;
    dsp::AutoGain autoGain;
    eq.setBand(0, bell(1000.0, 6.0));
    eq.prepare(kFs);
    std::vector<int> notified;
    core::ControlThread control(eq, autoGain, core::ControlThread::Threading::Manual,
                                [&](int latency) { notified.push_back(latency); });

    std::uint32_t seed = 1u;
    for (int i = 0; i < 5; ++i)
    {
        processBlock(eq, seed);
        control.flush();
    }
    CHECK(notified.empty());

    // Mientras el modo nuevo se prepara y hasta que el audio lo adopta, la
    // latencia es la de antes: no hay aviso.
    eq.setPhaseMode(dsp::PhaseMode::LinearLow);
    control.flush();
    CHECK(notified.empty());
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));
    control.flush();
    const int low = dsp::latencySamples(dsp::PhaseMode::LinearLow, kFs);
    CHECK(notified == std::vector<int>{low});
    control.flush();
    processBlock(eq, seed);
    control.flush();
    CHECK(notified == std::vector<int>{low});

    eq.setPhaseMode(dsp::PhaseMode::Iir);
    control.flush();
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));
    control.flush();
    CHECK(notified == std::vector<int>{low, 0});
}

TEST_CASE("Hilo de control: el hilo propio avisa del cambio de latencia")
{
    // Con ThreadSanitizer, comprueba también el aviso desde el hilo propio.
    dsp::PhaseEqualizer eq;
    dsp::AutoGain autoGain;
    eq.prepare(kFs);
    std::atomic<int> notified{-1};
    core::ControlThread control(eq, autoGain, core::ControlThread::Threading::Background,
                                [&](int latency) { notified.store(latency); });

    eq.setPhaseMode(dsp::PhaseMode::LinearMedium);
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));
    const int expected = dsp::latencySamples(dsp::PhaseMode::LinearMedium, kFs);
    const auto limit = Clock::now() + std::chrono::seconds(10);
    while (notified.load() != expected && Clock::now() < limit)
        std::this_thread::sleep_for(core::ControlThread::kPeriod);
    CHECK(notified.load() == expected);
}

namespace
{

/// Renderiza `numBlocks` bloques de `size` muestras sin tiempo real, como el
/// plugin: los ajustes del bloque (la ganancia de la banda 0, que cambia en
/// cada uno si `modulate`), renderBlock() y el audio. Devuelve la salida.
std::vector<float> render(dsp::PhaseEqualizer& eq, core::ControlThread& designer, int numBlocks, int size,
                          bool modulate)
{
    std::vector<float> out;
    std::uint32_t seed = 3u;
    std::vector<float> left(static_cast<std::size_t>(size)), right(static_cast<std::size_t>(size));
    for (int block = 0; block < numBlocks; ++block)
    {
        if (modulate)
            eq.setBand(0, bell(1000.0, 6.0 * std::sin(0.05 * block)));
        designer.renderBlock(size, kFs);
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            left[i] = right[i] = 0.3f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
        }
        float* channels[2] = {left.data(), right.data()};
        eq.process(channels, 2, size);
        out.insert(out.end(), left.begin(), left.end());
    }
    return out;
}

} // namespace

TEST_CASE("Hilo de control: al renderizar disena como mucho una vez por periodo")
{
    // PLAN.md 5.9 (bloque 7.1): el periodo es kPeriod (480 muestras a 48 kHz)
    // o el bloque de la convolución del modo, si es más largo.
    struct Case
    {
        dsp::PhaseMode mode;
        int period;
    };
    for (const auto [mode, period] : {Case{dsp::PhaseMode::LinearLow, 480}, Case{dsp::PhaseMode::LinearMedium, 512},
                                      Case{dsp::PhaseMode::LinearHigh, 1024}})
        for (const int size : {32, 64, 100, 256})
        {
            CAPTURE(static_cast<int>(mode), size);
            dsp::PhaseEqualizer eq;
            dsp::AutoGain autoGain;
            eq.setBand(0, bell(1000.0, 3.0));
            eq.setPhaseMode(mode);
            eq.prepare(kFs);
            core::ControlThread designer(eq, autoGain, core::ControlThread::Threading::Manual);
            designer.flush();
            eq.update();
            const auto before = designer.serviceCount();

            const int numBlocks = 48000 / size;
            (void)render(eq, designer, numBlocks, size, true);
            const auto services = designer.serviceCount() - before;
            // Uno en el primer bloque y después uno en cuanto pasa un periodo
            // entero: cada ceil(period / size) bloques.
            const int every = (period + size - 1) / size;
            CHECK(services == static_cast<std::uint64_t>((numBlocks + every - 1) / every));
        }
}

TEST_CASE("Hilo de control: al renderizar, un cambio tras un periodo sin cambios se disena en su bloque")
{
    // Lo que no se mueve sin parar sale como al diseñar en cada bloque.
    dsp::PhaseEqualizer eq;
    dsp::AutoGain autoGain;
    eq.setBand(0, bell(1000.0, 3.0));
    eq.setPhaseMode(dsp::PhaseMode::LinearHigh);
    eq.prepare(kFs);
    core::ControlThread designer(eq, autoGain, core::ControlThread::Threading::Manual);
    designer.flush();
    eq.update();

    for (int round = 0; round < 3; ++round)
    {
        (void)render(eq, designer, 4, 256, false); // 1024 muestras sin cambios
        const auto before = designer.serviceCount();
        eq.setBand(0, bell(1000.0, -2.0 - round));
        designer.renderBlock(256, kFs);
        CHECK(designer.serviceCount() == before + 1);
    }
    // Sin nada pendiente, no se cuenta nada.
    const auto before = designer.serviceCount();
    (void)render(eq, designer, 20, 256, false);
    CHECK(designer.serviceCount() == before);
}

TEST_CASE("Hilo de control: al renderizar con el hilo propio, dos renders dan lo mismo")
{
    // Mientras se renderiza, el hilo propio no diseña: el resultado no
    // depende de cuándo despierte.
    std::vector<float> first;
    for (int run = 0; run < 3; ++run)
    {
        dsp::PhaseEqualizer eq;
        dsp::AutoGain autoGain;
        eq.setBand(0, bell(1000.0, 3.0));
        eq.setPhaseMode(dsp::PhaseMode::LinearMedium);
        eq.prepare(kFs);
        core::ControlThread designer(eq, autoGain);
        designer.flush();
        eq.update();
        const auto out = render(eq, designer, 400, 96, true);
        if (run == 0)
            first = out;
        else
            CHECK(out == first);
    }
}

TEST_CASE("Hilo de control: al volver al tiempo real, el hilo propio disena otra vez")
{
    dsp::PhaseEqualizer eq;
    dsp::AutoGain autoGain;
    eq.setBand(0, bell(1000.0, 3.0));
    eq.setPhaseMode(dsp::PhaseMode::LinearLow);
    eq.prepare(kFs);
    core::ControlThread designer(eq, autoGain);
    designer.flush();
    eq.update();
    (void)render(eq, designer, 10, 256, true);

    designer.realtimeBlock();
    eq.setBand(0, bell(2000.0, -6.0));
    CHECK(processUntil(eq, [&] { return eq.isSettled(); }));
}
