// SPDX-License-Identifier: MIT
#include "esceq/dsp/decibels.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using namespace esceq::dsp;

TEST_CASE("dbToGain: valores conocidos")
{
    CHECK(dbToGain(0.0f) == 1.0f);
    CHECK_THAT(dbToGain(6.0f), WithinAbs(1.99526, 1e-4));
    CHECK_THAT(dbToGain(-20.0f), WithinAbs(0.1, 1e-6));
    CHECK(dbToGain(kSilenceDb) == 0.0f);
    CHECK(dbToGain(-200.0f) == 0.0f);
}

TEST_CASE("gainToDb: valores conocidos y límites")
{
    CHECK_THAT(gainToDb(1.0f), WithinAbs(0.0, 1e-6));
    CHECK_THAT(gainToDb(0.5f), WithinAbs(-6.0206, 1e-3));
    CHECK(gainToDb(0.0f) == kSilenceDb);
    CHECK(gainToDb(-1.0f) == kSilenceDb);
    CHECK(gainToDb(1e-30f) == kSilenceDb);
}

TEST_CASE("dbToGain y gainToDb son inversas")
{
    for (float db = -60.0f; db <= 24.0f; db += 3.5f)
        CHECK_THAT(gainToDb(dbToGain(db)), WithinAbs(db, 1e-3));
}
