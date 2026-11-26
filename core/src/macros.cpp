// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/macros.h"

#include <algorithm>
#include <cmath>

namespace esceq::core {

namespace {

bool validMacro(int macro) noexcept
{
    return macro >= 0 && macro < kNumMacros;
}

float clampDepth(float depth) noexcept
{
    return std::clamp(depth, -kMaxMacroDepth, kMaxMacroDepth);
}

} // namespace

bool isMacroTarget(std::size_t index) noexcept
{
    if (index == kOutputGain || index == kSaturationDrive || index == kSaturationMix)
        return true;
    if (index >= kFirstBandParameter && index < kFirstDynamicParameter)
    {
        const auto param = static_cast<BandParam>((index - kFirstBandParameter) % kParamsPerBand);
        return param == BandParam::Frequency || param == BandParam::Gain || param == BandParam::Q;
    }
    if (index >= kFirstDynamicParameter && index < kFirstChannelParameter)
    {
        const auto param = static_cast<DynParam>((index - kFirstDynamicParameter) % kDynParamsPerBand);
        return param == DynParam::Threshold || param == DynParam::Range || param == DynParam::Attack ||
               param == DynParam::Release || param == DynParam::Knee;
    }
    return false;
}

const MacroAssignments::Targets& MacroAssignments::targets(int macro) const noexcept
{
    return macros_[static_cast<std::size_t>(std::clamp(macro, 0, kNumMacros - 1))];
}

std::optional<float> MacroAssignments::depth(int macro, std::size_t parameter) const noexcept
{
    if (!validMacro(macro))
        return std::nullopt;
    for (const auto& target : macros_[static_cast<std::size_t>(macro)])
        if (target.used() && target.parameter == parameter)
            return target.depth;
    return std::nullopt;
}

bool MacroAssignments::assign(int macro, std::size_t parameter, float depth) noexcept
{
    if (!validMacro(macro) || !isMacroTarget(parameter) || !std::isfinite(depth))
        return false;
    auto& targets = macros_[static_cast<std::size_t>(macro)];
    for (auto& target : targets)
        if (target.parameter == parameter)
        {
            target.depth = clampDepth(depth);
            return true;
        }
    for (auto& target : targets)
        if (!target.used())
        {
            target = {parameter, clampDepth(depth)};
            return true;
        }
    return false;
}

bool MacroAssignments::remove(int macro, std::size_t parameter) noexcept
{
    if (!validMacro(macro))
        return false;
    for (auto& target : macros_[static_cast<std::size_t>(macro)])
        if (target.used() && target.parameter == parameter)
        {
            target = {};
            return true;
        }
    return false;
}

bool MacroAssignments::setSlot(int macro, int slot, std::size_t parameter, float depth) noexcept
{
    if (!validMacro(macro) || slot < 0 || slot >= kMaxMacroTargets || !isMacroTarget(parameter) ||
        !std::isfinite(depth))
        return false;
    auto& targets = macros_[static_cast<std::size_t>(macro)];
    for (int other = 0; other < kMaxMacroTargets; ++other)
        if (other != slot && targets[static_cast<std::size_t>(other)].parameter == parameter)
            return false;
    targets[static_cast<std::size_t>(slot)] = {parameter, clampDepth(depth)};
    return true;
}

void MacroAssignments::clearSlot(int macro, int slot) noexcept
{
    if (validMacro(macro) && slot >= 0 && slot < kMaxMacroTargets)
        macros_[static_cast<std::size_t>(macro)][static_cast<std::size_t>(slot)] = {};
}

bool MacroAssignments::empty() const noexcept
{
    for (const auto& targets : macros_)
        for (const auto& target : targets)
            if (target.used())
                return false;
    return true;
}

void MacroAssignments::clear() noexcept
{
    macros_ = {};
}

void applyMacros(const MacroAssignments& assignments, ParameterValues& values) noexcept ESCEQ_NONBLOCKING
{
    // Cada destino distinto, con su modulación sumada. Como mucho, uno por hueco.
    struct Offset
    {
        std::size_t parameter;
        double amount;
    };
    std::array<Offset, static_cast<std::size_t>(kNumMacros * kMaxMacroTargets)> offsets{};
    std::size_t count = 0;

    for (int macro = 0; macro < kNumMacros; ++macro)
    {
        const float raw = values[macroParameter(macro)];
        // El host ya da valores saneados; aun así, un NaN no modula.
        const double amount = raw > 0.0f ? std::min(static_cast<double>(raw), 100.0) / 100.0 : 0.0;
        for (const auto& target : assignments.targets(macro))
        {
            if (!target.used())
                continue;
            const double add = amount * static_cast<double>(target.depth) / 100.0;
            std::size_t i = 0;
            while (i < count && offsets[i].parameter != target.parameter)
                ++i;
            if (i == count)
                offsets[count++] = {target.parameter, 0.0};
            offsets[i].amount += add;
        }
    }

    for (std::size_t i = 0; i < count; ++i)
    {
        const auto [parameter, amount] = offsets[i];
        if (!(amount < 0.0 || amount > 0.0))
            continue; // sin modulación: el valor de siempre, bit a bit
        const auto& spec = parameters()[parameter];
        const double position = static_cast<double>(toNormalized(spec, values[parameter])) + amount;
        values[parameter] = fromNormalized(spec, static_cast<float>(std::clamp(position, 0.0, 1.0)));
    }
}

} // namespace esceq::core
