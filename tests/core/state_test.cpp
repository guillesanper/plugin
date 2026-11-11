// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/state.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

using namespace esceq::core;

namespace {

std::string readFixture(const std::string& name)
{
    std::ifstream file(std::string(ESCEQ_FIXTURES_DIR) + "/state/" + name, std::ios::binary);
    REQUIRE(file.is_open());
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

} // namespace

TEST_CASE("Estado: ida y vuelta conserva los valores")
{
    ParameterValues values = defaultValues();
    values[kOutputGain] = -7.3125f;
    values[kBypass] = 1.0f;

    const auto restored = deserializeState(serializeState(values));
    REQUIRE(restored.has_value());
    CHECK(*restored == values);
}

// Los fixtures son estados tal y como los escribió cada versión publicada.
// Nunca se modifican ni se borran: deben cargar siempre.
TEST_CASE("Estado: fixture v1 por defecto")
{
    const auto text = readFixture("v1_default.txt");

    const auto values = deserializeState(text);
    REQUIRE(values.has_value());
    CHECK(*values == defaultValues());

    // Mientras kStateVersion sea 1, las líneas que ya existían se siguen
    // escribiendo igual. (Puede haber líneas nuevas: parámetros añadidos.)
    if (kStateVersion == 1)
    {
        const std::string written = serializeState(defaultValues());
        std::istringstream lines(text);
        for (std::string line; std::getline(lines, line);)
        {
            INFO(line);
            CHECK(written.find(line + '\n') != std::string::npos);
        }
    }
}

TEST_CASE("Estado: fixture v1 con valores propios")
{
    const auto values = deserializeState(readFixture("v1_custom.txt"));
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == -6.5f);
    CHECK((*values)[kBypass] == 1.0f);
}

TEST_CASE("Estado: tolera finales de linea de Windows")
{
    const auto values = deserializeState("esceQ-state 1\r\noutput_gain 3\r\nbypass 1\r\n");
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == 3.0f);
    CHECK((*values)[kBypass] == 1.0f);
}

TEST_CASE("Estado: parametros ausentes toman el valor por defecto")
{
    const auto values = deserializeState("esceQ-state 1\nbypass 1\n");
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == parameters()[kOutputGain].defaultValue);
    CHECK((*values)[kBypass] == 1.0f);
}

TEST_CASE("Estado: un estado de una version futura carga lo que se conoce")
{
    const auto values = deserializeState("esceQ-state 99\nparametro_futuro 12\noutput_gain 2\n");
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == 2.0f);
}

TEST_CASE("Estado: valores fuera de rango o ilegibles no se propagan")
{
    const auto values = deserializeState("esceQ-state 1\noutput_gain 9999\nbypass basura\n");
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == parameters()[kOutputGain].max);
    CHECK((*values)[kBypass] == parameters()[kBypass].defaultValue);
}

TEST_CASE("Estado: se rechaza lo que no es un estado de esceQ")
{
    CHECK_FALSE(deserializeState("").has_value());
    CHECK_FALSE(deserializeState("otra cosa").has_value());
    CHECK_FALSE(deserializeState("esceQ-state").has_value());
    CHECK_FALSE(deserializeState("esceQ-state 0\n").has_value());
    CHECK_FALSE(deserializeState("esceQ-state x\noutput_gain 1\n").has_value());
    CHECK_FALSE(deserializeState(std::string("\0\x01\x02\xff", 4)).has_value());
}

TEST_CASE("Estado: fixture v1 con bandas")
{
    const auto values = deserializeState(readFixture("v1_bands.txt"));
    REQUIRE(values.has_value());
    CHECK((*values)[bandParameter(0, BandParam::Enabled)] == 1.0f);
    CHECK((*values)[bandParameter(0, BandParam::Type)] == 3.0f);
    CHECK((*values)[bandParameter(0, BandParam::Frequency)] == 80.0f);
    CHECK((*values)[bandParameter(0, BandParam::Slope)] == 3.0f);
    CHECK((*values)[bandParameter(4, BandParam::Enabled)] == 1.0f);
    CHECK((*values)[bandParameter(4, BandParam::Gain)] == -4.5f);
    CHECK((*values)[bandParameter(4, BandParam::Q)] == 2.5f);
    CHECK((*values)[bandParameter(23, BandParam::Enabled)] == 0.0f);
}

TEST_CASE("Estado: ida y vuelta con todas las bandas y valores aleatorios")
{
    ParameterValues values = defaultValues();
    std::uint32_t rng = 7u;
    for (std::size_t i = 0; i < kNumParameters; ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        const float normalized = static_cast<float>(rng >> 8) / 16777216.0f;
        values[i] = fromNormalized(parameters()[i], normalized);
    }

    const auto restored = deserializeState(serializeState(values));
    REQUIRE(restored.has_value());
    CHECK(*restored == values);
}

TEST_CASE("Estado: fixture v1 con dinamica")
{
    const auto values = deserializeState(readFixture("v1_dynamics.txt"));
    REQUIRE(values.has_value());
    CHECK((*values)[bandParameter(1, BandParam::Frequency)] == 6500.0f);
    CHECK((*values)[dynamicParameter(1, DynParam::Enabled)] == 1.0f);
    CHECK((*values)[dynamicParameter(1, DynParam::Threshold)] == -30.0f);
    CHECK((*values)[dynamicParameter(1, DynParam::Range)] == -9.0f);
    CHECK((*values)[dynamicParameter(1, DynParam::Attack)] == 0.5f);
    CHECK((*values)[dynamicParameter(1, DynParam::Release)] == 60.0f);
    CHECK((*values)[dynamicParameter(1, DynParam::Knee)] == 3.0f);
    CHECK((*values)[dynamicParameter(1, DynParam::Source)] == 1.0f);
    CHECK((*values)[dynamicParameter(0, DynParam::Enabled)] == 0.0f);
}

TEST_CASE("Estado: un estado anterior a la dinamica la deja desactivada")
{
    // v1_bands.txt se escribió antes de que existieran los parámetros de dinámica.
    const auto values = deserializeState(readFixture("v1_bands.txt"));
    REQUIRE(values.has_value());
    for (int band = 0; band < kNumBands; ++band)
    {
        INFO("banda " << band + 1);
        CHECK((*values)[dynamicParameter(band, DynParam::Enabled)] == 0.0f);
        CHECK((*values)[dynamicParameter(band, DynParam::Threshold)] ==
              parameters()[dynamicParameter(band, DynParam::Threshold)].defaultValue);
    }
}
