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

dsp::FilterType filterTypeFromChoice(float choice) noexcept
{
    const auto& spec = parameters()[bandParameter(0, BandParam::Type)];
    return static_cast<dsp::FilterType>(static_cast<int>(sanitize(spec, choice)));
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
