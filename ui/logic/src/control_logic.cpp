// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/control_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace esceq::ui {

namespace {

float angleFor(float normalized) noexcept
{
    return -kArcHalfSpan + 2.0f * kArcHalfSpan * std::clamp(normalized, 0.0f, 1.0f);
}

/// Posición 0..1 de la que parte el arco de valor.
float originNormalized(const core::ParameterSpec& spec) noexcept
{
    return isBipolar(spec) ? core::toNormalized(spec, 0.0f) : 0.0f;
}

float stepDiscrete(const core::ParameterSpec& spec, float value, int steps) noexcept
{
    return core::sanitize(spec, core::sanitize(spec, value) + static_cast<float>(steps));
}

float moveNormalized(const core::ParameterSpec& spec, float value, float amount) noexcept
{
    const float normalized = core::toNormalized(spec, value);
    return core::fromNormalized(spec, std::clamp(normalized + amount, 0.0f, 1.0f));
}

} // namespace

float dragNormalized(float normalized, float deltaPixels, bool fine) noexcept
{
    if (!std::isfinite(normalized) || !std::isfinite(deltaPixels))
        return std::isfinite(normalized) ? std::clamp(normalized, 0.0f, 1.0f) : 0.0f;
    const float scale = fine ? kFineFactor : 1.0f;
    return std::clamp(normalized + scale * deltaPixels / kDragPixelsForFullRange, 0.0f, 1.0f);
}

float wheelValue(const core::ParameterSpec& spec, float value, float wheelDelta, bool fine) noexcept
{
    if (!std::isfinite(wheelDelta) || wheelDelta == 0.0f)
        return core::sanitize(spec, value);
    if (spec.kind != core::ParameterKind::Continuous)
        return stepDiscrete(spec, value, wheelDelta > 0.0f ? 1 : -1);
    return moveNormalized(spec, value, wheelDelta * kWheelRangePerUnit * (fine ? kFineFactor : 1.0f));
}

float keyStepValue(const core::ParameterSpec& spec, float value, int steps, bool fine) noexcept
{
    if (spec.kind != core::ParameterKind::Continuous)
        return stepDiscrete(spec, value, steps);
    return moveNormalized(spec, value, static_cast<float>(steps) * kKeyStep * (fine ? kFineFactor : 1.0f));
}

bool isBipolar(const core::ParameterSpec& spec) noexcept
{
    return spec.kind == core::ParameterKind::Continuous && spec.scale == core::ParameterScale::Linear &&
           spec.min < 0.0f && spec.max > 0.0f;
}

ValueArc valueArc(const core::ParameterSpec& spec, float value) noexcept
{
    const float position = angleFor(core::toNormalized(spec, value));
    return {angleFor(originNormalized(spec)), position, position};
}

ValueSpan valueSpan(const core::ParameterSpec& spec, float value) noexcept
{
    const float position = core::toNormalized(spec, value);
    const float origin = originNormalized(spec);
    return {std::min(origin, position), std::max(origin, position), position};
}

std::string editText(const core::ParameterSpec& spec, float value)
{
    return core::formatValue(spec, value);
}

std::optional<float> commitText(const core::ParameterSpec& spec, std::string_view text)
{
    return core::parseValue(spec, text);
}

std::string phaseModeTooltip(int latencySamples, double sampleRate)
{
    if (latencySamples <= 0 || !(sampleRate > 0.0))
        return "Phase mode \xC2\xB7 no latency";
    const double ms = 1000.0 * latencySamples / sampleRate;
    char text[96];
    std::snprintf(text, sizeof text, "Phase mode \xC2\xB7 latency %.1f ms (%d samples)", ms, latencySamples);
    return text;
}

} // namespace esceq::ui
