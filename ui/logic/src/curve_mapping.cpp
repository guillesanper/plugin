// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/curve_mapping.h"

#include "esceq/core/band_model.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace esceq::ui {

namespace {

const double kLogSpan = std::log(kMaxDisplayHz / kMinDisplayHz);

} // namespace

CurveMapping::CurveMapping(Rect area, double rangeDb) noexcept
    : area_(area),
      rangeDb_(rangeDb > 0.0 ? rangeDb : 1.0)
{
    area_.width = std::max(area_.width, 1.0f);
    area_.height = std::max(area_.height, 1.0f);
}

float CurveMapping::xForFrequency(double hz) const noexcept
{
    const double t = std::log(std::max(hz, 1.0e-3) / kMinDisplayHz) / kLogSpan;
    return area_.x + static_cast<float>(t) * area_.width;
}

double CurveMapping::frequencyForX(float x) const noexcept
{
    const double t = static_cast<double>(x - area_.x) / area_.width;
    return kMinDisplayHz * std::exp(t * kLogSpan);
}

float CurveMapping::yForGain(double db) const noexcept
{
    const double t = (rangeDb_ - db) / (2.0 * rangeDb_);
    return area_.y + static_cast<float>(t) * area_.height;
}

double CurveMapping::gainForY(float y) const noexcept
{
    const double t = static_cast<double>(y - area_.y) / area_.height;
    return rangeDb_ - t * 2.0 * rangeDb_;
}

float CurveMapping::yForAnalyzerDb(double db) const noexcept
{
    const double t = (kAnalyzerTopDb - db) / (kAnalyzerTopDb - kAnalyzerBottomDb);
    return area_.y + static_cast<float>(t) * area_.height;
}

Point nodePosition(const CurveMapping& mapping, const dsp::BandSettings& band) noexcept
{
    const double gain = core::usesGain(band.filter.type) ? band.filter.gainDb : 0.0;
    const Rect area = mapping.area();
    return {std::clamp(mapping.xForFrequency(band.filter.frequencyHz), area.x, area.x + area.width),
            std::clamp(mapping.yForGain(gain), area.y, area.y + area.height)};
}

double draggedGain(const CurveMapping& mapping, double currentDb, float deltaY) noexcept
{
    const double dbPerPixel = 2.0 * mapping.rangeDb() / mapping.area().height;
    const double next = currentDb - static_cast<double>(deltaY) * dbPerPixel;
    const double range = mapping.rangeDb();
    return std::clamp(next, std::min(-range, currentDb), std::max(range, currentDb));
}

std::optional<int> hitTestNodes(std::span<const Point> nodes, std::span<const bool> visible, Point p,
                                float radius) noexcept
{
    std::optional<int> best;
    float bestDistance = std::numeric_limits<float>::max();
    const std::size_t count = std::min(nodes.size(), visible.size());

    // Se recorre de atrás adelante: con dos nodos a la misma distancia gana
    // el que se dibuja encima (el de índice mayor).
    for (std::size_t i = count; i-- > 0;)
    {
        if (!visible[i])
            continue;
        const float distance = std::hypot(nodes[i].x - p.x, nodes[i].y - p.y);
        if (distance <= radius && distance < bestDistance)
        {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::vector<double> logFrequencies(int count)
{
    std::vector<double> out;
    if (count <= 0)
        return out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const double t = count == 1 ? 0.0 : static_cast<double>(i) / (count - 1);
        out.push_back(kMinDisplayHz * std::exp(t * kLogSpan));
    }
    return out;
}

std::vector<GridLine> frequencyGrid()
{
    std::vector<GridLine> lines;
    for (double decade = 10.0; decade <= 10000.0; decade *= 10.0)
        for (int m = 1; m <= 9; ++m)
        {
            const double f = decade * m;
            if (f < kMinDisplayHz || f > kMaxDisplayHz)
                continue;
            lines.push_back({f, m == 1 || m == 2 || m == 5});
        }
    lines.push_back({kMaxDisplayHz, true});
    return lines;
}

std::vector<GridLine> gainGrid(double rangeDb)
{
    double step = 6.0;
    if (rangeDb <= 6.0)
        step = 1.0;
    else if (rangeDb <= 12.0)
        step = 3.0;
    else if (rangeDb > 24.0)
        step = 10.0;

    std::vector<GridLine> lines;
    for (double db = -std::floor(rangeDb / step) * step; db <= rangeDb + 1.0e-9; db += step)
        lines.push_back({db, true});
    return lines;
}

} // namespace esceq::ui
