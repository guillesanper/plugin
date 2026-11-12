// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/meter_logic.h"

#include <algorithm>
#include <cmath>

namespace esceq::ui {

namespace {

constexpr double kKneeDb = -24.0;      // donde cambia la pendiente de la escala
constexpr float kKneeFraction = 0.3f;  // altura a la que queda ese nivel

} // namespace

float meterFraction(double db) noexcept
{
    if (std::isnan(db) || db <= kMeterBottomDb)
        return 0.0f;
    if (db >= kMeterTopDb)
        return 1.0f;
    if (db < kKneeDb)
        return kKneeFraction * static_cast<float>((db - kMeterBottomDb) / (kKneeDb - kMeterBottomDb));
    return kKneeFraction + (1.0f - kKneeFraction) * static_cast<float>((db - kKneeDb) / (kMeterTopDb - kKneeDb));
}

std::vector<MeterTick> meterTicks()
{
    std::vector<MeterTick> ticks;
    for (const auto& [db, labelled] : {std::pair{6.0, false}, {0.0, true},   {-6.0, true},  {-12.0, true},
                                       {-18.0, false},        {-24.0, true}, {-36.0, true}, {-48.0, true}})
        ticks.push_back({db, meterFraction(db), labelled});
    return ticks;
}

void PeakMeter::update(double peak, double dtSeconds) noexcept
{
    const double dt = std::isfinite(dtSeconds) ? std::max(dtSeconds, 0.0) : 0.0;
    const double magnitude = std::isfinite(peak) ? std::abs(peak) : 0.0;
    const double db = magnitude > 0.0 ? std::max(20.0 * std::log10(magnitude), kSilenceDb) : kSilenceDb;

    if (db > kMeterClipDb)
        clipped_ = true;

    levelDb_ = std::max(db, std::max(levelDb_ - kFallDbPerSecond * dt, kSilenceDb));

    if (db >= holdDb_)
    {
        holdDb_ = db;
        holdAge_ = 0.0;
        return;
    }
    holdAge_ += dt;
    if (holdAge_ > kHoldSeconds)
        holdDb_ = std::max({holdDb_ - kHoldFallDbPerSecond * dt, levelDb_, kSilenceDb});
}

float dynamicMeterFraction(double dynamicGainDb, double rangeDb) noexcept
{
    if (!std::isfinite(dynamicGainDb) || !std::isfinite(rangeDb) || rangeDb == 0.0)
        return 0.0f;
    return static_cast<float>(std::clamp(dynamicGainDb / rangeDb, 0.0, 1.0));
}

} // namespace esceq::ui
