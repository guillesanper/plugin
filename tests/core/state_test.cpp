// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/state.h"

#include <catch2/catch_test_macros.hpp>

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

    // Mientras kStateVersion sea 1, el formato escrito tampoco cambia.
    if (kStateVersion == 1)
        CHECK(serializeState(defaultValues()) == text);
}

TEST_CASE("Estado: fixture v1 con valores propios")
{
    const auto values = deserializeState(readFixture("v1_custom.txt"));
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == -6.5f);
    CHECK((*values)[kBypass] == 1.0f);
}

TEST_CASE("Estado: tolera finales de línea de Windows")
{
    const auto values = deserializeState("esceQ-state 1\r\noutput_gain 3\r\nbypass 1\r\n");
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == 3.0f);
    CHECK((*values)[kBypass] == 1.0f);
}

TEST_CASE("Estado: parámetros ausentes toman el valor por defecto")
{
    const auto values = deserializeState("esceQ-state 1\nbypass 1\n");
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == kParameters[kOutputGain].defaultValue);
    CHECK((*values)[kBypass] == 1.0f);
}

TEST_CASE("Estado: un estado de una versión futura carga lo que se conoce")
{
    const auto values = deserializeState("esceQ-state 99\nparametro_futuro 12\noutput_gain 2\n");
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == 2.0f);
}

TEST_CASE("Estado: valores fuera de rango o ilegibles no se propagan")
{
    const auto values = deserializeState("esceQ-state 1\noutput_gain 9999\nbypass basura\n");
    REQUIRE(values.has_value());
    CHECK((*values)[kOutputGain] == kParameters[kOutputGain].max);
    CHECK((*values)[kBypass] == kParameters[kBypass].defaultValue);
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
