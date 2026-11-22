// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/curve_mapping.h"

#include "esceq/core/band_model.h"
#include "esceq/ui/notes.h"

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

Point nodePosition(const CurveMapping& mapping, const dsp::BandSettings& band, float verticalMargin) noexcept
{
    const double gain = core::usesGain(band.filter.type) ? band.filter.gainDb : 0.0;
    const Rect area = mapping.area();
    // El margen nunca pasa de media altura: en una vista diminuta el nodo va al centro.
    const float margin = std::clamp(verticalMargin, 0.0f, 0.5f * area.height);
    return {std::clamp(mapping.xForFrequency(band.filter.frequencyHz), area.x, area.x + area.width),
            std::clamp(mapping.yForGain(gain), area.y + margin, area.y + area.height - margin)};
}

double draggedGain(const CurveMapping& mapping, double currentDb, float deltaY) noexcept
{
    const double dbPerPixel = 2.0 * mapping.rangeDb() / mapping.area().height;
    const double next = currentDb - static_cast<double>(deltaY) * dbPerPixel;
    const double range = mapping.rangeDb();
    return std::clamp(next, std::min(-range, currentDb), std::max(range, currentDb));
}

int gainOverflow(const CurveMapping& mapping, const dsp::BandSettings& band) noexcept
{
    if (!core::usesGain(band.filter.type))
        return 0;
    // Medio píxel de margen: un nodo justo en el borde no se considera fuera.
    const double margin = mapping.rangeDb() / mapping.area().height;
    if (band.filter.gainDb > mapping.rangeDb() + margin)
        return 1;
    if (band.filter.gainDb < -mapping.rangeDb() - margin)
        return -1;
    return 0;
}

Rect floatingLabelRect(Rect area, Point node, float width, float height, float gap) noexcept
{
    const float w = std::min(width, area.width);
    const float h = std::min(height, area.height);

    float y = node.y - gap - h;
    if (y < area.y)
        y = node.y + gap; // arriba no cabe: debajo del nodo
    y = std::clamp(y, area.y, area.y + area.height - h);

    const float x = std::clamp(node.x - 0.5f * w, area.x, area.x + area.width - w);
    return {x, y, w, h};
}

NodeReadout nodeReadout(const core::ParameterValues& values, int band, std::optional<double> a4Hz, bool detailed)
{
    const auto& specs = core::parameters();
    auto text = [&](std::size_t index) { return core::formatValue(specs[index], values[index]); };

    const auto settings = core::bandSettings(values, band);
    const auto type = settings.filter.type;

    NodeReadout out;
    out.title = "Band " + std::to_string(band + 1) + " \xC2\xB7 " + text(core::bandParameter(band, core::BandParam::Type));
    if (detailed || settings.channel != dsp::ChannelMode::Stereo)
        out.title += " \xC2\xB7 " + text(core::channelParameter(band));

    out.frequency = text(core::bandParameter(band, core::BandParam::Frequency));
    if (a4Hz)
        out.frequency += " \xC2\xB7 " + noteText(settings.filter.frequencyHz, *a4Hz);
    if (core::usesGain(type))
        out.gain = text(core::bandParameter(band, core::BandParam::Gain));
    if (core::usesQ(type, settings.filter.slopeDbPerOct))
        out.q = "Q " + text(core::bandParameter(band, core::BandParam::Q));
    if (core::usesSlope(type))
        out.q = out.q.empty() ? text(core::bandParameter(band, core::BandParam::Slope))
                              : text(core::bandParameter(band, core::BandParam::Slope)) + "  " + out.q;

    if (detailed && core::usesDynamics(type))
    {
        auto dyn = [&](core::DynParam p) { return text(core::dynamicParameter(band, p)); };
        if (!settings.dynamics.enabled)
            out.dynamics.emplace_back("Dynamics off");
        else
        {
            out.dynamics.push_back("Thr " + dyn(core::DynParam::Threshold));
            out.dynamics.push_back("Range " + dyn(core::DynParam::Range));
            if (values[core::autoTimingParameter(band)] >= 0.5f)
                out.dynamics.emplace_back("Auto times");
            else
            {
                out.dynamics.push_back("Att " + dyn(core::DynParam::Attack));
                out.dynamics.push_back("Rel " + dyn(core::DynParam::Release));
            }
            if (values[core::dynamicParameter(band, core::DynParam::Source)] >= 0.5f)
                out.dynamics.emplace_back("Sidechain");
        }
    }
    return out;
}

bool onGainScale(Rect area, Point p) noexcept
{
    const float right = area.x + area.width;
    return p.x >= right - kGainScaleWidth && p.x <= right && p.y >= area.y && p.y <= area.y + area.height;
}

dsp::BandSettings soundingBandSettings(const core::ParameterValues& values, int band, dsp::FilterAlgorithm algorithm,
                                       double dynamicGainDb) noexcept
{
    auto settings = core::bandSettings(values, band, algorithm);
    if (settings.dynamics.enabled && core::usesDynamics(settings.filter.type))
        settings.filter.gainDb += dynamicGainDb;
    return settings;
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

std::vector<ParameterChange> newBandChanges(const core::ParameterValues& values, int band, double frequencyHz,
                                            double gainDb, std::optional<double> q, dsp::FilterType type)
{
    const auto& specs = core::parameters();
    std::vector<ParameterChange> changes;
    auto set = [&](std::size_t index, float value) {
        value = core::sanitize(specs[index], value);
        if (values[index] != value)
            changes.push_back({index, value});
    };
    auto setDefault = [&](std::size_t index) { set(index, specs[index].defaultValue); };

    const auto frequency = core::bandParameter(band, core::BandParam::Frequency);
    const auto gain = core::bandParameter(band, core::BandParam::Gain);
    // El índice del tipo es el de dsp::FilterType (static_assert en core/src/band_model.cpp).
    set(core::bandParameter(band, core::BandParam::Type), static_cast<float>(static_cast<int>(type)));
    if (q)
        set(core::bandParameter(band, core::BandParam::Q), static_cast<float>(*q));
    else
        setDefault(core::bandParameter(band, core::BandParam::Q));
    setDefault(core::bandParameter(band, core::BandParam::Slope));
    set(frequency, static_cast<float>(frequencyHz));
    set(gain, static_cast<float>(gainDb));

    for (const auto param : {core::DynParam::Enabled, core::DynParam::Threshold, core::DynParam::Range,
                             core::DynParam::Attack, core::DynParam::Release, core::DynParam::Knee,
                             core::DynParam::Source})
        setDefault(core::dynamicParameter(band, param));
    setDefault(core::channelParameter(band));
    setDefault(core::autoTimingParameter(band));
    setDefault(core::spectralParameter(band));

    // La última: la banda suena ya con sus valores.
    set(core::bandParameter(band, core::BandParam::Enabled), 1.0f);
    return changes;
}

std::optional<int> stepSelectedBand(const core::ParameterValues& values, int current, int step) noexcept
{
    const int direction = step >= 0 ? 1 : -1;
    // Sin banda elegida se empieza desde fuera de la lista: -1 hacia la
    // derecha, kNumBands hacia la izquierda.
    int band = current >= 0 && current < core::kNumBands ? current : (direction > 0 ? -1 : core::kNumBands);
    for (int i = 0; i < core::kNumBands; ++i)
    {
        band = (band + direction + core::kNumBands) % core::kNumBands;
        if (values[core::bandParameter(band, core::BandParam::Enabled)] >= 0.5f)
            return band;
    }
    return std::nullopt;
}

double wheelQ(double q, double delta) noexcept
{
    return q * std::pow(2.0, delta * 1.5);
}

} // namespace esceq::ui
