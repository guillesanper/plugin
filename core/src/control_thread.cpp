// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/control_thread.h"

#include "esceq/dsp/linear_phase.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace esceq::core
{

ControlThread::ControlThread(dsp::PhaseEqualizer& equalizer, dsp::AutoGain& autoGain, Threading threading,
                             LatencyCallback onLatencyChange)
    : equalizer_(equalizer), autoGain_(autoGain), onLatencyChange_(std::move(onLatencyChange)),
      seenLatency_(equalizer.latencySamples())
{
    if (threading == Threading::Background)
        thread_ = std::thread([this] { run(); });
}

ControlThread::~ControlThread()
{
    {
        const std::lock_guard lock(wakeMutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void ControlThread::flush()
{
    const std::lock_guard lock(serviceMutex_);
    serviceAll();
    samplesSinceService_.store(kDue, std::memory_order_relaxed);
}

void ControlThread::renderBlock(int numSamples, double sampleRate)
{
    // El primer bloque del render: lo pendiente, ya. Al tomar serviceMutex_,
    // el hilo propio ha terminado lo que estuviera haciendo, y ya no empieza nada.
    if (!rendering_.exchange(true))
        samplesSinceService_.store(kDue, std::memory_order_relaxed);

    const auto periodSamples = static_cast<std::int64_t>(
        std::lround(std::chrono::duration<double>(kPeriod).count() * sampleRate));
    const std::int64_t period =
        std::max<std::int64_t>(periodSamples, dsp::linearPhaseSize(equalizer_.phaseMode(), sampleRate).blockSize);
    std::int64_t since = samplesSinceService_.load(std::memory_order_relaxed);
    if (since >= period)
    {
        const std::lock_guard lock(serviceMutex_);
        if (serviceAll())
            since = 0;
    }
    samplesSinceService_.store(std::min(since + std::max(numSamples, 0), kDue), std::memory_order_relaxed);
}

void ControlThread::realtimeBlock() noexcept ESCEQ_NONBLOCKING
{
    if (rendering_.load(std::memory_order_relaxed))
        rendering_.store(false);
}

bool ControlThread::serviceAll()
{
    // Un cambio de modo entrega primero el camino y, si mientras tanto han
    // cambiado los ajustes, después un FIR: se sigue hasta que no quede nada.
    bool worked = false;
    while (equalizer_.serviceDesigns())
        worked = true;
    // La compensación solo calcula la última petición.
    worked = autoGain_.service() || worked;
    if (worked)
        serviceCount_.fetch_add(1, std::memory_order_relaxed);

    // La latencia cambia en el hilo de audio, al adoptar el modo entregado
    // (en esta vuelta o en una anterior).
    const int latency = equalizer_.latencySamples();
    if (latency != seenLatency_)
    {
        seenLatency_ = latency;
        if (onLatencyChange_)
            onLatencyChange_(latency);
    }
    return worked;
}

void ControlThread::run()
{
    std::unique_lock lock(wakeMutex_);
    while (!wake_.wait_for(lock, kPeriod, [this] { return stopping_; }))
    {
        lock.unlock();
        {
            const std::lock_guard service(serviceMutex_);
            // Al renderizar, solo trabaja renderBlock() (o flush()).
            if (!rendering_.load())
                serviceAll();
        }
        lock.lock();
    }
}

} // namespace esceq::core
