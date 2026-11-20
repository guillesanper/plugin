// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/control_thread.h"

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
}

void ControlThread::serviceAll()
{
    // Un cambio de modo entrega primero el camino y, si mientras tanto han
    // cambiado los ajustes, después un FIR: se sigue hasta que no quede nada.
    while (equalizer_.serviceDesigns())
    {
    }
    // La compensación solo calcula la última petición.
    autoGain_.service();

    // La latencia cambia en el hilo de audio, al adoptar el modo entregado
    // (en esta vuelta o en una anterior).
    const int latency = equalizer_.latencySamples();
    if (latency != seenLatency_)
    {
        seenLatency_ = latency;
        if (onLatencyChange_)
            onLatencyChange_(latency);
    }
}

void ControlThread::run()
{
    std::unique_lock lock(wakeMutex_);
    while (!wake_.wait_for(lock, kPeriod, [this] { return stopping_; }))
    {
        lock.unlock();
        {
            const std::lock_guard service(serviceMutex_);
            serviceAll();
        }
        lock.lock();
    }
}

} // namespace esceq::core
