// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/macros.h"

#include "esceq/core/band_model.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace esceq::ui {

namespace {

constexpr const char* kDot = " \xC2\xB7 ";

bool validMacro(int macro) noexcept
{
    return macro >= 0 && macro < core::kNumMacros;
}

std::string_view trimmed(std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

int usedTargets(const core::MacroAssignments& macros, int macro) noexcept
{
    const auto& targets = macros.targets(macro);
    return static_cast<int>(std::count_if(targets.begin(), targets.end(), [](const auto& t) { return t.used(); }));
}

} // namespace

std::string macroName(int macro)
{
    return "Macro " + std::to_string(macro + 1);
}

std::string depthText(float depth)
{
    char buffer[32];
    if (std::abs(depth) < 0.05f)
        return "0%";
    if (std::abs(depth - std::round(depth)) < 0.05f)
        std::snprintf(buffer, sizeof buffer, "%+.0f%%", static_cast<double>(depth));
    else
        std::snprintf(buffer, sizeof buffer, "%+.1f%%", static_cast<double>(depth));
    return buffer;
}

std::optional<float> parseDepth(std::string_view text)
{
    text = trimmed(text);
    if (!text.empty() && text.back() == '%')
        text = trimmed(text.substr(0, text.size() - 1));
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);
    if (text.empty() || text.front() == '+')
        return std::nullopt;

    float value = 0.0f;
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const auto [end, error] = std::from_chars(first, last, value);
    if (error != std::errc{} || end != last || !std::isfinite(value))
        return std::nullopt;
    return std::clamp(value, -core::kMaxMacroDepth, core::kMaxMacroDepth);
}

std::vector<MacroLinkEntry> macroLinkEntries(const core::MacroAssignments& macros, std::size_t parameter)
{
    std::vector<MacroLinkEntry> entries;
    if (!core::isMacroTarget(parameter))
        return entries;
    entries.reserve(static_cast<std::size_t>(core::kNumMacros));
    for (int macro = 0; macro < core::kNumMacros; ++macro)
    {
        MacroLinkEntry entry;
        entry.macro = macro;
        entry.depth = macros.depth(macro, parameter);
        entry.full = !entry.depth && usedTargets(macros, macro) >= core::kMaxMacroTargets;
        entry.text = macroName(macro);
        if (entry.depth)
            entry.text += " (" + depthText(*entry.depth) + ")";
        else if (entry.full)
            entry.text += " (full)";
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::string linkedTargetText(const core::MacroAssignments& macros, std::size_t parameter, std::string_view label)
{
    std::string links;
    for (int macro = 0; macro < core::kNumMacros; ++macro)
        if (const auto depth = macros.depth(macro, parameter))
            links += (links.empty() ? "" : ", ") + macroName(macro) + " " + depthText(*depth);
    std::string text(label);
    if (!links.empty())
        text += " (" + links + ")";
    return text;
}

std::optional<core::MacroAssignments> withLink(core::MacroAssignments macros, int macro, std::size_t parameter,
                                               float depth) noexcept
{
    if (!macros.assign(macro, parameter, depth))
        return std::nullopt;
    return macros;
}

core::MacroAssignments withoutLink(core::MacroAssignments macros, int macro, std::size_t parameter) noexcept
{
    macros.remove(macro, parameter);
    return macros;
}

core::MacroAssignments clearMacro(core::MacroAssignments macros, int macro) noexcept
{
    if (validMacro(macro))
        for (int slot = 0; slot < core::kMaxMacroTargets; ++slot)
            macros.clearSlot(macro, slot);
    return macros;
}

std::vector<std::size_t> nodeMacroTargets(const core::ParameterValues& values, int band)
{
    const auto settings = core::bandSettings(values, band);
    std::vector<std::size_t> targets{core::bandParameter(band, core::BandParam::Frequency)};
    if (core::usesGain(settings.filter.type))
        targets.push_back(core::bandParameter(band, core::BandParam::Gain));
    if (core::usesQ(settings.filter.type, settings.filter.slopeDbPerOct))
        targets.push_back(core::bandParameter(band, core::BandParam::Q));
    return targets;
}

bool isLinked(const core::MacroAssignments& macros, std::size_t parameter) noexcept
{
    for (int macro = 0; macro < core::kNumMacros; ++macro)
        if (macros.depth(macro, parameter))
            return true;
    return false;
}

ModulationArc modulationArc(const core::ParameterSpec& spec, float base, float sounding) noexcept
{
    return {valueArc(spec, base).indicator, valueArc(spec, sounding).indicator};
}

bool bandModulated(const core::ParameterValues& base, const core::ParameterValues& sounding, int band) noexcept
{
    for (const auto param : {core::BandParam::Frequency, core::BandParam::Gain, core::BandParam::Q})
    {
        const auto index = core::bandParameter(band, param);
        // Igualdad exacta a propósito: sin modulación, applyMacros deja el valor bit a bit.
        // NOLINTNEXTLINE(clang-diagnostic-float-equal)
        if (base[index] != sounding[index])
            return true;
    }
    return false;
}

bool macroHasTargets(const core::MacroAssignments& macros, int macro) noexcept
{
    return validMacro(macro) && usedTargets(macros, macro) > 0;
}

std::string macroTooltip(const core::MacroAssignments& macros, int macro)
{
    std::string text = macroName(macro);
    if (!macroHasTargets(macros, macro))
        return text + kDot +
               "moves nothing yet: right-click a knob or a band node and choose Link to Macro. Link a host "
               "controller to this macro to move several settings at once";

    std::string targets;
    for (const auto& target : macros.targets(macro))
        if (target.used())
            targets += (targets.empty() ? "" : ", ") + std::string(core::parameters()[target.parameter].name) + " " +
                       depthText(target.depth);
    return text + kDot + "moves " + targets + kDot +
           "the depth is how far each one moves at 100%, in % of its knob; the knobs keep their own value";
}

bool macrosActive(const core::MacroAssignments& macros, const core::ParameterValues& values) noexcept
{
    for (int macro = 0; macro < core::kNumMacros; ++macro)
        if (values[core::macroParameter(macro)] > 0.0f && macroHasTargets(macros, macro))
            return true;
    return false;
}

std::string macrosSummary(const core::MacroAssignments& macros, const core::ParameterValues& values)
{
    int links = 0;
    for (int macro = 0; macro < core::kNumMacros; ++macro)
        if (values[core::macroParameter(macro)] > 0.0f)
            links += usedTargets(macros, macro);
    if (links == 0)
        return {};
    return "Macros: " + std::to_string(links) + (links == 1 ? " link moving" : " links moving");
}

} // namespace esceq::ui
