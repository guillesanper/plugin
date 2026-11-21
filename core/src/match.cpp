// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/match.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace esceq::core
{

namespace
{

/// Puntos de la FFT de los análisis (los de la sesión).
constexpr double kFftPoints = static_cast<double>(1 << SpectrumSession::kFftOrder);

bool finite(std::span<const float> levels) noexcept
{
    return std::all_of(levels.begin(), levels.end(), [](float db) { return std::isfinite(db); });
}

} // namespace

double smoothingOctaves(MatchSmoothing smoothing) noexcept
{
    switch (smoothing)
    {
    case MatchSmoothing::SixthOctave:
        return 1.0 / 6.0;
    case MatchSmoothing::ThirdOctave:
        return 1.0 / 3.0;
    case MatchSmoothing::Octave:
        return 1.0;
    }
    return 1.0 / 3.0;
}

MatchSettings sanitize(const MatchSettings& settings) noexcept
{
    const MatchSettings defaults;
    MatchSettings out = settings;
    out.amount = std::isfinite(settings.amount) ? std::clamp(settings.amount, 0.0, 1.0) : defaults.amount;
    switch (settings.smoothing)
    {
    case MatchSmoothing::SixthOctave:
    case MatchSmoothing::ThirdOctave:
    case MatchSmoothing::Octave:
        break;
    default:
        out.smoothing = defaults.smoothing;
    }
    out.maxBands = std::clamp(settings.maxBands, 1, kNumBands);

    const double minRatio = std::exp2(MatchSettings::kMinRangeOctaves);
    double low = std::isfinite(settings.lowHz) ? settings.lowHz : defaults.lowHz;
    double high = std::isfinite(settings.highHz) ? settings.highHz : defaults.highHz;
    low = std::clamp(low, MatchSettings::kMinHz, MatchSettings::kMaxHz / minRatio);
    high = std::clamp(high, MatchSettings::kMinHz, MatchSettings::kMaxHz);
    if (high < low * minRatio)
        high = low * minRatio; // cabe: low ≤ kMaxHz / minRatio
    out.lowHz = low;
    out.highHz = high;
    return out;
}

int effectiveMaxBands(const MatchSettings& settings, int freeBands) noexcept
{
    return std::clamp(sanitize(settings).maxBands, 0, std::max(freeBands, 0));
}

dsp::CurveMatchSettings toCurveMatchSettings(const MatchSettings& settings, int freeBands) noexcept
{
    const MatchSettings sane = sanitize(settings);
    dsp::CurveMatchSettings out;
    out.amount = sane.amount;
    out.smoothingOctaves = smoothingOctaves(sane.smoothing);
    out.maxBands = effectiveMaxBands(sane, freeBands);
    out.lowHz = sane.lowHz;
    out.highHz = sane.highHz;
    return out;
}

// --- MatchLearner ---

MatchLearner::MatchLearner(std::size_t points) : trackPower_(points, 0.0), referencePower_(points, 0.0) {}

bool MatchLearner::add(const MatchObservation& o)
{
    if (!o.trackHasSignal || !o.referenceHasSignal)
        return false;
    if (!(o.dtSeconds > 0.0) || !std::isfinite(o.dtSeconds))
        return false;
    if (o.trackDb.size() != trackPower_.size() || o.referenceDb.size() != referencePower_.size())
        return false;
    if (!finite(o.trackDb) || !finite(o.referenceDb))
        return false;

    const double dt = std::min(o.dtSeconds, kMaxStepSeconds);
    for (std::size_t i = 0; i < trackPower_.size(); ++i)
    {
        trackPower_[i] += dt * std::pow(10.0, 0.1 * static_cast<double>(o.trackDb[i]));
        referencePower_[i] += dt * std::pow(10.0, 0.1 * static_cast<double>(o.referenceDb[i]));
    }
    seconds_ += dt;
    for (const double rate : {o.trackSampleRate, o.referenceSampleRate})
        if (std::isfinite(rate))
            maxSampleRate_ = std::max(maxSampleRate_, rate);
    ++revision_;
    return true;
}

void MatchLearner::reset() noexcept
{
    std::fill(trackPower_.begin(), trackPower_.end(), 0.0);
    std::fill(referencePower_.begin(), referencePower_.end(), 0.0);
    seconds_ = 0.0;
    maxSampleRate_ = 0.0;
    ++revision_;
}

std::vector<float> MatchLearner::average(const std::vector<double>& power) const
{
    std::vector<float> out(power.size(), kSessionFloorDb);
    if (empty())
        return out;
    for (std::size_t i = 0; i < power.size(); ++i)
    {
        const double mean = power[i] / seconds_;
        out[i] = mean > 0.0 ? std::max(static_cast<float>(10.0 * std::log10(mean)), kSessionFloorDb)
                            : kSessionFloorDb;
    }
    return out;
}

std::vector<float> MatchLearner::trackDb() const
{
    return average(trackPower_);
}

std::vector<float> MatchLearner::referenceDb() const
{
    return average(referencePower_);
}

double MatchLearner::resolutionHz() const noexcept
{
    return maxSampleRate_ > 0.0 ? maxSampleRate_ / kFftPoints : 0.0;
}

// --- MatchRequest ---

dsp::CurveMatchInput MatchRequest::input() const noexcept
{
    dsp::CurveMatchInput in;
    in.frequencies = frequencies;
    in.trackDb = trackDb;
    in.referenceDb = referenceDb;
    in.resolutionHz = resolutionHz;
    in.currentBands = currentBands;
    in.numChannels = numChannels;
    in.sampleRate = sampleRate;
    in.algorithm = algorithm;
    return in;
}

// --- MatchWorker ---

MatchWorker::MatchWorker(Threading threading, Observer observer)
    : threading_(threading), observer_(std::move(observer))
{
}

MatchWorker::~MatchWorker()
{
    std::thread thread;
    {
        const std::scoped_lock lock(mutex_);
        stop_.store(true, std::memory_order_release);
        pending_.reset();
        if (cancel_)
            cancel_->store(true, std::memory_order_relaxed);
        thread = std::move(thread_);
    }
    wake_.notify_all();
    if (thread.joinable())
        thread.join();
}

std::uint64_t MatchWorker::submit(MatchRequest request)
{
    std::uint64_t id = 0;
    {
        const std::scoped_lock lock(mutex_);
        id = nextId_++;
        pending_ = std::move(request);
        pendingId_ = id;
        if (cancel_)
            cancel_->store(true, std::memory_order_relaxed);
        if (threading_ == Threading::Background && !thread_.joinable() && !stopping())
            thread_ = std::thread([this] { run(); });
    }
    wake_.notify_all();
    return id;
}

void MatchWorker::cancel()
{
    const std::scoped_lock lock(mutex_);
    pending_.reset();
    pendingId_ = 0;
    if (cancel_)
        cancel_->store(true, std::memory_order_relaxed);
}

std::shared_ptr<const MatchProposal> MatchWorker::latest() const
{
    const std::scoped_lock lock(mutex_);
    return latest_;
}

bool MatchWorker::busy() const
{
    const std::scoped_lock lock(mutex_);
    return pending_.has_value() || runningId_ != 0;
}

std::uint64_t MatchWorker::lastSubmitted() const
{
    const std::scoped_lock lock(mutex_);
    return nextId_ - 1;
}

int MatchWorker::runPending()
{
    if (threading_ != Threading::Manual)
        return 0;
    int count = 0;
    while (runOne())
        ++count;
    return count;
}

bool MatchWorker::runOne()
{
    MatchRequest request;
    std::uint64_t id = 0;
    std::shared_ptr<std::atomic<bool>> cancel;
    {
        const std::scoped_lock lock(mutex_);
        if (!pending_ || stopping())
            return false;
        request = std::move(*pending_);
        pending_.reset();
        id = pendingId_;
        pendingId_ = 0;
        cancel = std::make_shared<std::atomic<bool>>(false);
        cancel_ = cancel;
        runningId_ = id;
    }

    if (observer_.started)
        observer_.started(id);

    auto proposal = std::make_shared<MatchProposal>();
    proposal->id = id;
    proposal->result = dsp::matchCurve(request.input(), request.settings, cancel.get());
    // Si la cancelaron después de su última iteración, el resultado está
    // completo, pero ya hay otra petición: tampoco se publica.
    const bool cancelled = proposal->result.cancelled || cancel->load(std::memory_order_relaxed);
    {
        const std::scoped_lock lock(mutex_);
        if (!cancelled)
            latest_ = proposal;
        runningId_ = 0;
        cancel_.reset();
    }
    if (observer_.finished)
        observer_.finished(*proposal);
    return true;
}

void MatchWorker::run()
{
    while (true)
    {
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping() || pending_.has_value(); });
            if (stopping())
                return;
        }
        runOne();
    }
}

// --- MatchSidechainTap ---

void MatchSidechainTap::setEnabled(bool on) noexcept
{
    if (on && !enabled())
    {
        // Lo que quedara es de la vez anterior. Solo el lector llama aquí.
        std::array<float, 1024> scratch{};
        while (fifo_.read(scratch.data(), static_cast<int>(scratch.size())) > 0)
        {
        }
    }
    enabled_.store(on, std::memory_order_release);
}

void MatchSidechainTap::write(const float* const* channels, int numChannels,
                              int numSamples) noexcept ESCEQ_NONBLOCKING
{
    if (!enabled_.load(std::memory_order_acquire) || channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;
    fifo_.writeMix(channels, numChannels, numSamples);
}

} // namespace esceq::core
