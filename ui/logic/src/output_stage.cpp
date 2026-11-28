// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/output_stage.h"

#include "esceq/ui/macros.h"

#include "esceq/core/band_model.h"
#include "esceq/dsp/phase_equalizer.h"

#include <cmath>
#include <cstdio>

namespace esceq::ui {

namespace {

constexpr const char* kDot = " \xC2\xB7 ";

std::string formatted(std::size_t index, float value)
{
    return core::formatValue(core::parameters()[index], value);
}

} // namespace

bool saturationAmountActive(dsp::SaturationMode mode) noexcept
{
    return mode != dsp::SaturationMode::Off;
}

double saturationLatencyMs(dsp::PhaseMode phase, double sampleRate) noexcept
{
    if (!(sampleRate > 0.0))
        return 0.0;
    return 1000.0 * dsp::PhaseEqualizer::saturationLatency(phase, sampleRate) / sampleRate;
}

std::string saturationTooltip(dsp::PhaseMode phase, double sampleRate)
{
    std::string text = "Saturation";
    const double latency = saturationLatencyMs(phase, sampleRate);
    if (latency > 0.0)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%sadds %.2f ms latency", kDot, latency);
        text += buffer;
    }
    text += std::string(kDot) +
            "after the EQ, before the output gain: Soft adds odd harmonics, Warm also even ones. The drive's level "
            "change is compensated";
    if (latency > 0.0)
        text += ". In linear phase, switching it on or off changes the latency and leaves a short gap, also when "
                "automated";
    return text;
}

std::string saturationDriveTooltip(dsp::SaturationMode mode)
{
    std::string text = "Saturation drive \xC2\xB7 how hard the signal hits the curve; the level stays about the same";
    if (!saturationAmountActive(mode))
        text += std::string(kDot) + "saturation is off: choose Soft or Warm";
    return text;
}

std::string saturationMixTooltip(dsp::SaturationMode mode)
{
    std::string text = "Saturation mix \xC2\xB7 saturated signal against the clean one";
    if (!saturationAmountActive(mode))
        text += std::string(kDot) + "saturation is off: choose Soft or Warm";
    return text;
}

bool outputStageActive(const core::ParameterValues& values, const core::MacroAssignments& macros) noexcept
{
    const auto& specs = core::parameters();
    return saturationAmountActive(core::saturationModeFromChoice(values[core::kSaturationMode])) ||
           std::abs(core::sanitize(specs[core::kOutputGain], values[core::kOutputGain])) >= 0.05f ||
           values[core::kAutoGain] >= 0.5f || macrosActive(macros, values);
}

std::string outputStageTooltip(const core::ParameterValues& values, bool open, const core::MacroAssignments& macros)
{
    if (open)
        return "Hide the output stage (saturation, output gain, Auto gain and macros)";

    std::string text = "Show the output stage: saturation, output gain, Auto gain and macros";
    const auto& specs = core::parameters();
    const auto mode = core::saturationModeFromChoice(values[core::kSaturationMode]);
    if (saturationAmountActive(mode))
        text += std::string(kDot) + "Saturation " +
                std::string(core::kSaturationModeChoices[static_cast<std::size_t>(mode)]) + ", drive " +
                formatted(core::kSaturationDrive, values[core::kSaturationDrive]) + ", mix " +
                formatted(core::kSaturationMix, values[core::kSaturationMix]);
    const float gain = core::sanitize(specs[core::kOutputGain], values[core::kOutputGain]);
    if (std::abs(gain) >= 0.05f)
        text += std::string(kDot) + "Output " + formatted(core::kOutputGain, gain);
    if (values[core::kAutoGain] >= 0.5f)
        text += std::string(kDot) + "Auto gain on";
    if (const auto summary = macrosSummary(macros, values); !summary.empty())
        text += std::string(kDot) + summary;
    return text;
}

} // namespace esceq::ui
