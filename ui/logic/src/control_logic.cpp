// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/control_logic.h"

#include "esceq/dsp/dynamics.h"
#include "esceq/ui/notes.h"

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

/// "-2.3 dB", "+1.0 dB" o "0.0 dB": lo que redondea a 0 no lleva signo.
std::string signedDb(double db)
{
    const double rounded = std::round(db * 10.0) / 10.0;
    char text[32];
    if (rounded == 0.0 || !std::isfinite(rounded))
        return "0.0 dB";
    std::snprintf(text, sizeof text, "%+.1f dB", rounded);
    return text;
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

std::optional<float> commitText(const core::ParameterSpec& spec, std::string_view text, double a4Hz)
{
    if (spec.unit == "Hz")
        if (const auto midi = parseNote(text))
            return core::sanitize(spec, static_cast<float>(frequencyForNote(*midi, a4Hz)));
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

std::string autoGainLabel(bool enabled, double compensationDb)
{
    return enabled ? "Auto " + signedDb(compensationDb) : "Auto";
}

std::string autoGainTooltip(bool enabled, double compensationDb)
{
    if (!enabled)
        return "Auto gain \xC2\xB7 off: keep the loudness the same as with bypass, to compare at equal loudness";
    return "Auto gain \xC2\xB7 " + signedDb(compensationDb) +
           ": keeps the loudness of the curve the same as with bypass (static gain only, for pink noise)";
}

std::string_view dynamicTimeValueText(bool autoTiming) noexcept
{
    return autoTiming ? "Auto" : "";
}

std::string autoTimingTooltip(double frequencyHz)
{
    // Con el formato de los propios potenciómetros de ataque y relajación.
    const auto& specs = core::parameters();
    const auto& attack = specs[core::dynamicParameter(0, core::DynParam::Attack)];
    const auto& release = specs[core::dynamicParameter(0, core::DynParam::Release)];
    const auto times = dsp::autoEnvelopeTimes(frequencyHz);
    return "Auto timing \xC2\xB7 attack " + core::formatValue(attack, static_cast<float>(times.attackMs)) +
           ", release " + core::formatValue(release, static_cast<float>(times.releaseMs)) + " (" +
           core::formatValue(release, static_cast<float>(times.slowReleaseMs)) + " after long notes)";
}

} // namespace esceq::ui
