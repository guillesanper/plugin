// SPDX-License-Identifier: MIT
#include "esceq/dsp/dynamics.h"

#include <algorithm>
#include <cmath>

namespace esceq::dsp {

bool supportsDynamics(FilterType type) noexcept
{
    switch (type)
    {
    case FilterType::Bell:
    case FilterType::LowShelf:
    case FilterType::HighShelf:
    case FilterType::Tilt:
        return true;
    default:
        return false;
    }
}

double dynamicGainDb(double levelDb, double thresholdDb, double rangeDb, double kneeDb) noexcept
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

BandDesign designDetector(const FilterParams& params, double sampleRate) noexcept
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

void EnvelopeFollower::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    attack_ = coefficient(std::max(attackMs_, kMinAttackMs));
    release_ = coefficient(std::max(releaseMs_, kMinReleaseMs));
}

void EnvelopeFollower::setTimes(double attackMs, double releaseMs) noexcept
{
    if (!std::isfinite(attackMs))
        attackMs = 5.0;
    if (!std::isfinite(releaseMs))
        releaseMs = 100.0;
    if (attackMs == attackMs_ && releaseMs == releaseMs_)
        return;

    attackMs_ = attackMs;
    releaseMs_ = releaseMs;
    attack_ = coefficient(std::max(attackMs_, kMinAttackMs));
    release_ = coefficient(std::max(releaseMs_, kMinReleaseMs));
}

double EnvelopeFollower::coefficient(double ms) const noexcept
{
    return std::exp(-1000.0 / (ms * sampleRate_));
}

} // namespace esceq::dsp
