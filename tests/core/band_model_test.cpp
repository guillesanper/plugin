// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/band_model.h"

#include <catch2/catch_test_macros.hpp>

using namespace esceq;
using namespace esceq::core;

TEST_CASE("Modelo: valores por defecto dan bandas desactivadas")
{
    const auto values = defaultValues();
    for (int band = 0; band < kNumBands; ++band)
    {
        const auto settings = bandSettings(values, band);
        CHECK_FALSE(settings.enabled);
        CHECK(settings.filter.type == dsp::FilterType::Bell);
        CHECK(settings.filter.frequencyHz == 1000.0);
        CHECK(settings.filter.gainDb == 0.0);
        CHECK(settings.filter.slopeDbPerOct == 12);
    }
}

TEST_CASE("Modelo: traduce los parametros de una banda")
{
    auto values = defaultValues();
    values[bandParameter(3, BandParam::Enabled)] = 1.0f;
    values[bandParameter(3, BandParam::Type)] = 4.0f;
    values[bandParameter(3, BandParam::Frequency)] = 8000.0f;
    values[bandParameter(3, BandParam::Gain)] = 2.0f;
    values[bandParameter(3, BandParam::Q)] = 1.5f;
    values[bandParameter(3, BandParam::Slope)] = 7.0f;

    const auto settings = bandSettings(values, 3);
    CHECK(settings.enabled);
    CHECK(settings.filter.type == dsp::FilterType::HighCut);
    CHECK(settings.filter.frequencyHz == 8000.0);
    CHECK(settings.filter.gainDb == 2.0);
    CHECK(settings.filter.q == 1.5);
    CHECK(settings.filter.slopeDbPerOct == 48);
}

TEST_CASE("Modelo: valores fuera de rango se sanean")
{
    auto values = defaultValues();
    values[bandParameter(0, BandParam::Type)] = 42.0f;
    values[bandParameter(0, BandParam::Frequency)] = 1.0e9f;
    values[bandParameter(0, BandParam::Slope)] = -5.0f;

    const auto settings = bandSettings(values, 0);
    CHECK(settings.filter.type == dsp::FilterType::Tilt);
    CHECK(settings.filter.frequencyHz == 20000.0);
    CHECK(settings.filter.slopeDbPerOct == 6);
}

TEST_CASE("Modelo: pendientes y tipos")
{
    CHECK(slopeFromChoice(0.0f) == 6);
    CHECK(slopeFromChoice(15.0f) == 96);
    CHECK(filterTypeFromChoice(5.0f) == dsp::FilterType::Notch);

    CHECK(usesGain(dsp::FilterType::Bell));
    CHECK(usesGain(dsp::FilterType::Tilt));
    CHECK_FALSE(usesGain(dsp::FilterType::LowCut));
    CHECK_FALSE(usesGain(dsp::FilterType::Notch));

    CHECK(usesSlope(dsp::FilterType::HighCut));
    CHECK_FALSE(usesSlope(dsp::FilterType::Bell));

    CHECK(usesQ(dsp::FilterType::Bell, 12));
    CHECK(usesQ(dsp::FilterType::LowCut, 12));
    CHECK_FALSE(usesQ(dsp::FilterType::LowCut, 6));
    CHECK_FALSE(usesQ(dsp::FilterType::Tilt, 12));
}

TEST_CASE("Modelo: primera banda libre")
{
    auto values = defaultValues();
    CHECK(firstFreeBand(values) == 0);

    values[bandParameter(0, BandParam::Enabled)] = 1.0f;
    values[bandParameter(1, BandParam::Enabled)] = 1.0f;
    CHECK(firstFreeBand(values) == 2);

    for (int band = 0; band < kNumBands; ++band)
        values[bandParameter(band, BandParam::Enabled)] = 1.0f;
    CHECK_FALSE(firstFreeBand(values).has_value());
}
