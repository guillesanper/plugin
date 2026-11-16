// SPDX-License-Identifier: MIT
#include "esceq/dsp/dynamics.h"

#include <algorithm>
#include <cmath>

namespace esceq::dsp {

bool supportsDynamics(FilterType type) noexcept
{
    return filterUsesGain(type);
}

double dynamicGainDb(double levelDb, double thresholdDb, double rangeDb, double kneeDb) noexcept ESCEQ_NONBLOCKING
{
    if (!std::isfinite(levelDb) || !std::isfinite(thresholdDb) || !std::isfinite(rangeDb) || rangeDb == 0.0)
        return 0.0;

    const double knee = std::isfinite(kneeDb) ? std::max(kneeDb, 0.0) : 0.0;
    const double over = levelDb - thresholdDb;

    double amount = 0.0;
    if (knee > 0.0 && std::abs(over) < 0.5 * knee)
    {
        const double x = over + 0.5 * knee;
        amount = x * x / (2.0 * knee);
    }
    else if (over > 0.0)
        amount = over;

    const double limit = std::abs(rangeDb);
    return std::copysign(std::min(amount, limit), rangeDb);
}

BandDesign designDetector(const FilterParams& params, double sampleRate) noexcept ESCEQ_NONBLOCKING
{
    FilterParams detector = params;
    detector.gainDb = 0.0;

    switch (params.type)
    {
    case FilterType::Bell:
        detector.type = FilterType::BandPass;
        break;
    case FilterType::LowShelf:
        detector.type = FilterType::HighCut;
        detector.q = kButterworthQ;
        detector.slopeDbPerOct = 12;
        break;
    case FilterType::HighShelf:
        detector.type = FilterType::LowCut;
        detector.q = kButterworthQ;
        detector.slopeDbPerOct = 12;
        break;
    default:
        return BandDesign{}; // banda completa
    }
    return designBand(detector, sampleRate);
}

EnvelopeTimes autoEnvelopeTimes(double frequencyHz) noexcept ESCEQ_NONBLOCKING
{
    if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0)
        frequencyHz = 1000.0;
    const double periodMs = 1000.0 / frequencyHz;

    EnvelopeTimes times;
    times.attackMs = std::clamp(kAutoAttackPeriods * periodMs, kAutoMinAttackMs, kAutoMaxAttackMs);
    times.releaseMs = std::max(kAutoReleasePeriods * periodMs, kAutoMinReleaseMs);
    times.slowReleaseMs =
        std::max(times.releaseMs, std::min(kAutoSlowReleaseFactor * times.releaseMs, kAutoMaxSlowReleaseMs));
    return times;
}

void EnvelopeFollower::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    attack_ = coefficient(std::max(times_.attackMs, kMinAttackMs));
    release_ = coefficient(std::max(times_.releaseMs, kMinReleaseMs));
    slowRelease_ = slowStage_ ? coefficient(std::max(times_.slowReleaseMs, kMinReleaseMs)) : 0.0;
}

void EnvelopeFollower::setTimes(const EnvelopeTimes& requested) noexcept ESCEQ_NONBLOCKING
{
    const EnvelopeTimes defaults{};
    EnvelopeTimes times = requested;
    if (!std::isfinite(times.attackMs))
        times.attackMs = defaults.attackMs;
    if (!std::isfinite(times.releaseMs))
        times.releaseMs = defaults.releaseMs;
    if (!std::isfinite(times.slowReleaseMs) || times.slowReleaseMs < 0.0)
        times.slowReleaseMs = 0.0;
    if (times == times_)
        return;

    const bool slowStage = times.slowReleaseMs > 0.0;
    if (slowStage && !slowStage_)
        slowEnvelope_ = envelope_; // empieza donde está: la salida no cambia
    else if (!slowStage && slowStage_)
        envelope_ = std::max(envelope_, slowEnvelope_); // sigue desde lo que sonaba

    times_ = times;
    slowStage_ = slowStage;
    prepare(sampleRate_);
}

double EnvelopeFollower::coefficient(double ms) const noexcept
{
    return std::exp(-1000.0 / (ms * sampleRate_));
}

} // namespace esceq::dsp
