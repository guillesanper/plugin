// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/session_overlay.h"

#include "esceq/core/parameters.h"

#include <algorithm>
#include <cmath>

namespace esceq::ui
{

std::vector<CompareMenuItem> compareMenuItems(std::span<const core::InstanceSpectrum> instances, core::InstanceId own,
                                              const core::CompareSelection& selection)
{
    std::vector<const core::InstanceSpectrum*> others;
    for (const auto& instance : instances)
        if (instance.id != own)
            others.push_back(&instance);
    std::stable_sort(others.begin(), others.end(), [](auto* a, auto* b) { return a->number < b->number; });

    std::vector<CompareMenuItem> items;
    items.reserve(others.size());
    for (const auto* instance : others)
        items.push_back({.id = instance->id,
                         .name = instance->displayName(),
                         .ticked = selection.contains(instance->id),
                         .hasSignal = instance->hasSignal});
    return items;
}

std::string compareBadge(const core::CompareSelection& selection)
{
    return selection.shown.empty() ? std::string{} : std::to_string(selection.shown.size());
}

CollisionOverlay computeCollisions(std::span<const double> frequencies, std::span<const float> own,
                                   std::span<const std::span<const float>> others,
                                   const CollisionViewSettings& settings)
{
    const std::size_t n = frequencies.size();
    CollisionOverlay overlay;
    overlay.severity.assign(n, 0.0f);
    overlay.culprit.assign(n, -1);
    if (n == 0 || own.size() != n || others.empty())
        return overlay;

    std::vector<float> ownSmooth(n);
    std::vector<float> otherSmooth(n);
    std::vector<float> severity(n);
    dsp::smoothOctaves(frequencies, own, settings.smoothingOctaves, ownSmooth);

    for (std::size_t k = 0; k < others.size(); ++k)
    {
        if (others[k].size() != n)
            continue;
        dsp::smoothOctaves(frequencies, others[k], settings.smoothingOctaves, otherSmooth);
        dsp::collisionSeverity(ownSmooth, otherSmooth, severity, settings.detection);
        for (std::size_t i = 0; i < n; ++i)
            if (severity[i] > overlay.severity[i])
            {
                overlay.severity[i] = severity[i];
                overlay.culprit[i] = static_cast<int>(k);
            }

        for (const auto& region : dsp::findCollisionRegions(frequencies, severity, settings.regionThreshold))
        {
            double weight = 0.0;
            for (std::size_t i = region.first; i <= region.last; ++i)
            {
                // Octavas que "ocupa" el punto: media distancia a cada vecino.
                const double lo = i > 0 ? std::log2(frequencies[i] / frequencies[i - 1]) : 0.0;
                const double hi = i + 1 < n ? std::log2(frequencies[i + 1] / frequencies[i]) : 0.0;
                weight += static_cast<double>(severity[i]) * 0.5 * (lo + hi);
            }
            overlay.zones.push_back({region, static_cast<int>(k), weight});
        }
    }

    std::stable_sort(overlay.zones.begin(), overlay.zones.end(),
                     [](const CollisionZone& a, const CollisionZone& b) { return a.weight > b.weight; });
    return overlay;
}

std::string collisionLabel(const std::string& instanceName, double peakHz)
{
    static const auto& spec = core::parameters()[core::bandParameter(0, core::BandParam::Frequency)];
    return instanceName + " \xC2\xB7 " + core::formatValue(spec, static_cast<float>(peakHz));
}

std::vector<std::optional<Rect>> placeLabels(std::span<const float> centresX, std::span<const float> widths, Rect area,
                                             float y, float height, float gap)
{
    const std::size_t count = std::min(centresX.size(), widths.size());
    std::vector<std::optional<Rect>> placed(count);
    std::vector<Rect> taken;

    for (std::size_t i = 0; i < count; ++i)
    {
        const float width = widths[i];
        if (width <= 0.0f || width > area.width)
            continue;
        const float x = std::clamp(centresX[i] - 0.5f * width, area.x, area.x + area.width - width);
        const Rect r{x, y, width, height};
        const bool overlaps = std::any_of(taken.begin(), taken.end(), [&](const Rect& t)
                                          { return r.x < t.x + t.width + gap && t.x < r.x + r.width + gap; });
        if (overlaps)
            continue;
        taken.push_back(r);
        placed[i] = r;
    }
    return placed;
}

} // namespace esceq::ui
