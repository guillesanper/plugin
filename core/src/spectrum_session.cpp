// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/spectrum_session.h"

#include "esceq/dsp/spectrum_analyzer.h"
#include "esceq/dsp/spectrum_collision.h"
#include "esceq/dsp/spsc_fifo.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace esceq::core
{

namespace
{

/// Capacidad de la cola de cada instancia: más de 150 ms a 192 kHz, de sobra
/// para los 50 ms entre dos pasos del trabajador.
constexpr int kFifoCapacity = 1 << 15;
constexpr int kReadChunk = 4096;
/// Un espectro con algún punto por encima de esto tiene señal (es el fondo de
/// la escala del analizador de la interfaz).
constexpr float kSignalThresholdDb = -90.0f;

} // namespace

const std::vector<double>& sessionFrequencies()
{
    static const std::vector<double> frequencies = []
    {
        std::vector<double> f(static_cast<std::size_t>(kSessionPoints));
        const double ratio = std::log(kSessionMaxHz / kSessionMinHz);
        for (std::size_t i = 0; i < f.size(); ++i)
            f[i] = kSessionMinHz * std::exp(ratio * static_cast<double>(i) / static_cast<double>(f.size() - 1));
        return f;
    }();
    return frequencies;
}

std::string InstanceSpectrum::displayName() const
{
    return trackName.empty() ? "esceQ " + std::to_string(number) : trackName;
}

struct SpectrumSession::Slot
{
    Slot(InstanceId id_, int number_)
        : id(id_), number(number_),
          averager(static_cast<std::size_t>(kSessionPoints), kAverageSeconds, kSessionFloorDb),
          published(static_cast<std::size_t>(kSessionPoints), kSessionFloorDb)
    {
        analyzer.setTiltDbPerOctave(kTiltDbPerOctave);
    }

    const InstanceId id;
    const int number;

    // Hilo de audio (productor) y trabajador (consumidor).
    dsp::SpscFifo fifo{kFifoCapacity};
    std::atomic<double> sampleRate{0.0};

    // Solo el trabajador.
    dsp::SpectrumAnalyzer analyzer{kFftOrder};
    dsp::SpectrumAverager averager;
    double silentSeconds = std::numeric_limits<double>::infinity();    ///< Sin audio nuevo.
    double inaudibleSeconds = std::numeric_limits<double>::infinity(); ///< Sin nivel visible.

    // Publicado: protegido por SpectrumSession::mutex_.
    std::string trackName;
    std::optional<std::uint32_t> trackColour;
    bool hasSignal = false;
    std::vector<float> published;
};

std::shared_ptr<SpectrumSession> SpectrumSession::shared()
{
    static std::mutex mutex;
    static std::weak_ptr<SpectrumSession> current;
    const std::scoped_lock lock(mutex);
    auto session = current.lock();
    if (!session)
    {
        session = create(Threading::Background);
        current = session;
    }
    return session;
}

std::shared_ptr<SpectrumSession> SpectrumSession::create(Threading threading)
{
    return std::make_shared<SpectrumSession>(Key{}, threading);
}

SpectrumSession::SpectrumSession(Key, Threading threading)
{
    if (threading == Threading::Background)
        worker_ = std::thread([this] { run(); });
}

SpectrumSession::~SpectrumSession()
{
    {
        const std::scoped_lock lock(wakeMutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

std::unique_ptr<SpectrumSession::Member> SpectrumSession::join()
{
    std::shared_ptr<Slot> slot;
    {
        const std::scoped_lock lock(mutex_);
        int number = 1;
        while (std::any_of(slots_.begin(), slots_.end(), [number](const auto& s) { return s->number == number; }))
            ++number;
        slot = std::make_shared<Slot>(nextId_++, number);
        slots_.push_back(slot);
    }
    bumpRevision();
    return std::unique_ptr<Member>(new Member(shared_from_this(), std::move(slot)));
}

void SpectrumSession::leave(const std::shared_ptr<Slot>& slot)
{
    {
        const std::scoped_lock lock(mutex_);
        std::erase(slots_, slot);
    }
    bumpRevision();
}

SpectrumSession::Viewer SpectrumSession::watch()
{
    addViewer();
    return Viewer(shared_from_this());
}

void SpectrumSession::addViewer() noexcept
{
    if (viewers_.fetch_add(1, std::memory_order_acq_rel) == 0)
        watchGeneration_.fetch_add(1, std::memory_order_acq_rel);
    {
        // Con el cerrojo, el trabajador no puede perder el aviso entre que
        // comprueba watched() y se pone a esperar.
        const std::scoped_lock lock(wakeMutex_);
    }
    wake_.notify_all();
}

void SpectrumSession::removeViewer() noexcept
{
    viewers_.fetch_sub(1, std::memory_order_acq_rel);
}

std::size_t SpectrumSession::size() const
{
    const std::scoped_lock lock(mutex_);
    return slots_.size();
}

void SpectrumSession::snapshot(std::vector<InstanceSpectrum>& out) const
{
    const std::scoped_lock lock(mutex_);
    out.resize(slots_.size());
    for (std::size_t i = 0; i < slots_.size(); ++i)
    {
        const Slot& slot = *slots_[i];
        InstanceSpectrum& entry = out[i];
        entry.id = slot.id;
        entry.number = slot.number;
        entry.trackName = slot.trackName;
        entry.trackColour = slot.trackColour;
        entry.hasSignal = slot.hasSignal;
        entry.levelsDb.assign(slot.published.begin(), slot.published.end());
    }
}

void SpectrumSession::update(double dtSeconds)
{
    if (!watched())
        return;

    // Al empezar a mirar, lo que haya en las colas es de la vez anterior.
    const auto generation = watchGeneration_.load(std::memory_order_acquire);
    const bool restart = generation != seenGeneration_;
    seenGeneration_ = generation;

    std::vector<std::shared_ptr<Slot>> slots;
    {
        const std::scoped_lock lock(mutex_);
        slots = slots_;
    }

    const auto& frequencies = sessionFrequencies();
    std::vector<float> chunk(static_cast<std::size_t>(kReadChunk));
    std::vector<float> levels(frequencies.size());
    const std::vector<float> silence(frequencies.size(), kSessionFloorDb);

    bool changed = false;
    for (const auto& slot : slots)
    {
        int received = 0;
        for (int n; (n = slot->fifo.read(chunk.data(), kReadChunk)) > 0;)
        {
            if (!restart)
                slot->analyzer.push(chunk.data(), n);
            received += n;
        }
        if (restart)
        {
            slot->analyzer.clear();
            slot->averager.reset();
            slot->silentSeconds = std::numeric_limits<double>::infinity();
            slot->inaudibleSeconds = std::numeric_limits<double>::infinity();
            received = 0;
        }

        if (received > 0)
        {
            const double sampleRate = slot->sampleRate.load(std::memory_order_relaxed);
            slot->analyzer.setSampleRate(sampleRate > 0.0 ? sampleRate : 48000.0);
            slot->analyzer.compute(frequencies, levels);
            slot->averager.update(levels, dtSeconds);
            slot->silentSeconds = 0.0;
            // La señal se juzga por el espectro de ahora, no por el promedio,
            // que tarda segundos en apagarse.
            const bool audible =
                std::any_of(levels.begin(), levels.end(), [](float db) { return db > kSignalThresholdDb; });
            slot->inaudibleSeconds = audible ? 0.0 : slot->inaudibleSeconds + dtSeconds;
        }
        else
        {
            slot->silentSeconds += dtSeconds;
            slot->inaudibleSeconds += dtSeconds;
            // Sin audio (transporte parado, pista sin uso), el espectro se apaga
            // poco a poco en lugar de quedarse congelado.
            if (slot->silentSeconds > kSignalHoldSeconds && !slot->averager.empty())
                slot->averager.update(silence, dtSeconds);
        }

        const auto& averaged = slot->averager.levels();
        const bool hasSignal = slot->inaudibleSeconds <= kSignalHoldSeconds;
        {
            const std::scoped_lock lock(mutex_);
            if (slot->published != averaged || slot->hasSignal != hasSignal)
            {
                slot->published.assign(averaged.begin(), averaged.end());
                slot->hasSignal = hasSignal;
                changed = true;
            }
        }
    }
    if (changed || restart)
        bumpRevision();
}

void SpectrumSession::run()
{
    using Clock = std::chrono::steady_clock;
    const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(kUpdateSeconds));
    auto last = Clock::now();

    std::unique_lock lock(wakeMutex_);
    while (!stopping_)
    {
        if (!watched())
        {
            wake_.wait(lock, [this] { return stopping_ || watched(); });
            last = Clock::now();
            continue;
        }
        if (wake_.wait_for(lock, period, [this] { return stopping_; }))
            break;

        lock.unlock();
        const auto now = Clock::now();
        // Si el sistema ha tenido parado el hilo un rato, no se da un salto enorme.
        const double dt = std::min(std::chrono::duration<double>(now - last).count(), 0.25);
        last = now;
        update(dt);
        lock.lock();
    }
}

// --- Member ---

SpectrumSession::Member::Member(std::shared_ptr<SpectrumSession> session, std::shared_ptr<Slot> slot)
    : session_(std::move(session)), slot_(std::move(slot))
{
}

SpectrumSession::Member::~Member()
{
    session_->leave(slot_);
}

InstanceId SpectrumSession::Member::id() const noexcept
{
    return slot_->id;
}

void SpectrumSession::Member::setSampleRate(double sampleRate) noexcept
{
    if (sampleRate > 0.0)
        slot_->sampleRate.store(sampleRate, std::memory_order_relaxed);
}

void SpectrumSession::Member::setTrackName(std::string name)
{
    {
        const std::scoped_lock lock(session_->mutex_);
        if (slot_->trackName == name)
            return;
        slot_->trackName = std::move(name);
    }
    session_->bumpRevision();
}

void SpectrumSession::Member::setTrackColour(std::optional<std::uint32_t> argb)
{
    {
        const std::scoped_lock lock(session_->mutex_);
        if (slot_->trackColour == argb)
            return;
        slot_->trackColour = argb;
    }
    session_->bumpRevision();
}

void SpectrumSession::Member::feed(const float* const* channels, int numChannels,
                                   int numSamples) noexcept ESCEQ_NONBLOCKING
{
    if (!session_->watched() || numChannels <= 0 || numSamples <= 0)
        return;
    slot_->fifo.writeMix(channels, numChannels, numSamples);
}

// --- Viewer ---

SpectrumSession::Viewer& SpectrumSession::Viewer::operator=(Viewer&& other) noexcept
{
    if (this != &other)
    {
        release();
        session_ = std::move(other.session_);
    }
    return *this;
}

void SpectrumSession::Viewer::release() noexcept
{
    if (session_)
    {
        session_->removeViewer();
        session_.reset();
    }
}

} // namespace esceq::core

namespace esceq::core
{

bool CompareSelection::contains(InstanceId id) const noexcept
{
    return std::find(shown.begin(), shown.end(), id) != shown.end();
}

void CompareSelection::toggle(InstanceId id)
{
    if (const auto it = std::find(shown.begin(), shown.end(), id); it != shown.end())
        shown.erase(it);
    else
        shown.push_back(id);
}

bool CompareSelection::retainExisting(std::span<const InstanceSpectrum> instances)
{
    const auto removed = std::erase_if(
        shown, [&](InstanceId id)
        { return std::none_of(instances.begin(), instances.end(), [id](const auto& i) { return i.id == id; }); });
    return removed > 0;
}

} // namespace esceq::core
