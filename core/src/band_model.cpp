// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/band_model.h"

#include <cmath>

namespace esceq::core {

static_assert(kNumBands <= dsp::kMaxBands);

// El índice del parámetro "type" se guarda en los proyectos: debe seguir
// correspondiendo al mismo tipo de filtro para siempre.
static_assert(kFilterTypeChoices.size() == 9);
static_assert(static_cast<int>(dsp::FilterType::Bell) == 0);
static_assert(static_cast<int>(dsp::FilterType::LowShelf) == 1);
static_assert(static_cast<int>(dsp::FilterType::HighShelf) == 2);
static_assert(static_cast<int>(dsp::FilterType::LowCut) == 3);
static_assert(static_cast<int>(dsp::FilterType::HighCut) == 4);
static_assert(static_cast<int>(dsp::FilterType::Notch) == 5);
static_assert(static_cast<int>(dsp::FilterType::BandPass) == 6);
static_assert(static_cast<int>(dsp::FilterType::AllPass) == 7);
static_assert(static_cast<int>(dsp::FilterType::Tilt) == 8);

// Lo mismo con el índice del parámetro "chan".
static_assert(kChannelChoices.size() == 5);
static_assert(static_cast<int>(dsp::ChannelMode::Stereo) == 0);
static_assert(static_cast<int>(dsp::ChannelMode::Left) == 1);
static_assert(static_cast<int>(dsp::ChannelMode::Right) == 2);
static_assert(static_cast<int>(dsp::ChannelMode::Mid) == 3);
static_assert(static_cast<int>(dsp::ChannelMode::Side) == 4);

dsp::FilterType filterTypeFromChoice(float choice) noexcept
{
    const auto& spec = parameters()[bandParameter(0, BandParam::Type)];
    return static_cast<dsp::FilterType>(static_cast<int>(sanitize(spec, choice)));
}

dsp::ChannelMode channelModeFromChoice(float choice) noexcept
{
    const auto& spec = parameters()[channelParameter(0)];
    return static_cast<dsp::ChannelMode>(static_cast<int>(sanitize(spec, choice)));
}

int slopeFromChoice(float choice) noexcept
{
    const auto& spec = parameters()[bandParameter(0, BandParam::Slope)];
    return 6 * (static_cast<int>(sanitize(spec, choice)) + 1);
}

bool usesGain(dsp::FilterType type) noexcept
{
    switch (type)
    {
    case dsp::FilterType::Bell:
    case dsp::FilterType::LowShelf:
    case dsp::FilterType::HighShelf:
    case dsp::FilterType::Tilt:
        return true;
    default:
        return false;
    }
}

bool usesDynamics(dsp::FilterType type) noexcept
{
    return dsp::supportsDynamics(type);
}

bool usesQ(dsp::FilterType type, int slopeDbPerOct) noexcept
{
    if (type == dsp::FilterType::Tilt)
        return false;
    if (usesSlope(type))
        return slopeDbPerOct > 6;
    return true;
}

bool usesSlope(dsp::FilterType type) noexcept
{
    return type == dsp::FilterType::LowCut || type == dsp::FilterType::HighCut;
}

dsp::BandSettings bandSettings(const ParameterValues& values, int band) noexcept
{
    const auto& specs = parameters();
    auto value = [&](BandParam p) {
        const std::size_t i = bandParameter(band, p);
        return sanitize(specs[i], values[i]);
    };

    dsp::BandSettings settings;
    settings.enabled = value(BandParam::Enabled) >= 0.5f;
    settings.filter.type = filterTypeFromChoice(value(BandParam::Type));
    settings.filter.frequencyHz = value(BandParam::Frequency);
    settings.filter.gainDb = value(BandParam::Gain);
    settings.filter.q = value(BandParam::Q);
    settings.filter.slopeDbPerOct = slopeFromChoice(value(BandParam::Slope));

    auto dyn = [&](DynParam p) {
        const std::size_t i = dynamicParameter(band, p);
        return sanitize(specs[i], values[i]);
    };
    settings.dynamics.enabled = dyn(DynParam::Enabled) >= 0.5f;
    settings.dynamics.thresholdDb = dyn(DynParam::Threshold);
    settings.dynamics.rangeDb = dyn(DynParam::Range);
    settings.dynamics.attackMs = dyn(DynParam::Attack);
    settings.dynamics.releaseMs = dyn(DynParam::Release);
    settings.dynamics.kneeDb = dyn(DynParam::Knee);
    settings.dynamics.external = dyn(DynParam::Source) >= 0.5f;

    settings.channel = channelModeFromChoice(values[channelParameter(band)]);
    return settings;
}

std::optional<int> firstFreeBand(const ParameterValues& values) noexcept
{
    for (int band = 0; band < kNumBands; ++band)
        if (values[bandParameter(band, BandParam::Enabled)] < 0.5f)
            return band;
    return std::nullopt;
}

} // namespace esceq::core
