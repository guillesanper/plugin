// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/meter_logic.h"

#include "esceq/core/state.h"
#include "esceq/dsp/spectrum_analyzer.h"

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

SpectrumBallistics::SpectrumBallistics(std::size_t points, float floorDb)
    : levels_(points, floorDb),
      floorDb_(floorDb)
{
}

bool SpectrumBallistics::update(const std::vector<float>& target, double dtSeconds) noexcept
{
    const auto rise = static_cast<float>(1.0 - std::exp(-dtSeconds / kRiseSeconds));
    const auto fall = static_cast<float>(1.0 - std::exp(-dtSeconds / fallSeconds_));
    bool moved = false;
    const std::size_t n = std::min(levels_.size(), target.size());
    for (std::size_t i = 0; i < n; ++i)
    {
        float& level = levels_[i];
        const float before = level;
        if (level <= floorDb_ + 1.0f)
            level = target[i]; // primer dato: sin transición desde el silencio
        else
            level += (target[i] - level) * (target[i] > level ? rise : fall);
        moved = moved || std::abs(level - before) > kRepaintDb;
    }
    return moved;
}

void SpectrumBallistics::setFallSeconds(double seconds) noexcept
{
    if (std::isfinite(seconds) && seconds > 0.0)
        fallSeconds_ = seconds;
}

SpectrumPeakHold::SpectrumPeakHold(std::size_t points, float floorDb)
    : levels_(points, floorDb),
      floorDb_(floorDb)
{
}

bool SpectrumPeakHold::update(const std::vector<float>& levels) noexcept
{
    bool rose = false;
    const std::size_t n = std::min(levels_.size(), levels.size());
    for (std::size_t i = 0; i < n; ++i)
    {
        if (levels[i] <= levels_[i])
            continue;
        rose = rose || levels[i] - levels_[i] > SpectrumBallistics::kRepaintDb;
        levels_[i] = levels[i];
    }
    return rose;
}

void SpectrumPeakHold::clear() noexcept
{
    std::fill(levels_.begin(), levels_.end(), floorDb_);
}

bool SpectrumPeakHold::hasPeaks() const noexcept
{
    return std::any_of(levels_.begin(), levels_.end(), [this](float level) { return level > floorDb_; });
}

SpectrumAverage::SpectrumAverage(std::size_t points)
    : power_(points, 0.0),
      levels_(points, dsp::SpectrumAnalyzer::kFloorDb)
{
}

void SpectrumAverage::update(const std::vector<float>& levelsDb, double dtSeconds) noexcept
{
    if (levelsDb.size() != levels_.size())
        return;
    const double dt = std::isfinite(dtSeconds) ? std::max(dtSeconds, 0.0) : 0.0;
    const double alpha = 1.0 - std::exp(-dt / kSeconds);
    for (std::size_t i = 0; i < levels_.size(); ++i)
    {
        const double db = std::isfinite(levelsDb[i]) ? levelsDb[i] : dsp::SpectrumAnalyzer::kFloorDb;
        const double power = std::pow(10.0, 0.1 * db);
        power_[i] = empty_ ? power : power_[i] + alpha * (power - power_[i]);
        levels_[i] = std::max(static_cast<float>(10.0 * std::log10(power_[i])), dsp::SpectrumAnalyzer::kFloorDb);
    }
    empty_ = false;
}

void SpectrumAverage::clear() noexcept
{
    std::fill(levels_.begin(), levels_.end(), dsp::SpectrumAnalyzer::kFloorDb);
    empty_ = true;
}

int analyzerFftOrder(int fftSize) noexcept
{
    switch (fftSize)
    {
    case 8192:
        return 13;
    case 16384:
        return 14;
    default:
        return 12;
    }
}

double analyzerFallSeconds(int fallMs) noexcept
{
    for (const int valid : core::kAnalyzerFallTimesMs)
        if (fallMs == valid)
            return static_cast<double>(fallMs) / 1000.0;
    return SpectrumBallistics::kFallSeconds;
}

} // namespace esceq::ui
