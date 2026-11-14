// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/phase_design_thread.h"

namespace esceq::core
{

PhaseDesignThread::PhaseDesignThread(dsp::PhaseEqualizer& equalizer, Threading threading) : equalizer_(equalizer)
{
    if (threading == Threading::Background)
        thread_ = std::thread([this] { run(); });
}

PhaseDesignThread::~PhaseDesignThread()
{
    {
        const std::lock_guard lock(wakeMutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void PhaseDesignThread::flush()
{
    const std::lock_guard lock(serviceMutex_);
    serviceAll();
}

void PhaseDesignThread::serviceAll()
{
    // Un cambio de modo entrega primero el camino y, si mientras tanto han
    // cambiado los ajustes, después un FIR: se sigue hasta que no quede nada.
    while (equalizer_.serviceDesigns())
    {
    }
}

void PhaseDesignThread::run()
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
