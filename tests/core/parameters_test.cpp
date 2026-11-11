// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/parameters.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <set>
#include <string_view>

using namespace esceq::core;

namespace {

struct Frozen
{
    std::string_view id;
    ParameterKind kind;
    float min;
    float max;
};

// CONTRATO PÚBLICO. Esta lista solo puede CRECER por el final.
// Si este test falla es que un cambio rompería proyectos guardados:
// no edites una fila existente; lee docs/decisiones/0004-parametros-congelados.md.
constexpr Frozen kFrozen[] = {
    {"output_gain", ParameterKind::Continuous, -24.0f, 24.0f},
    {"bypass", ParameterKind::Toggle, 0.0f, 1.0f},
};

} // namespace

TEST_CASE("Parametros: los ya publicados no cambian de id, tipo, rango ni posicion")
{
    REQUIRE(kParameters.size() >= std::size(kFrozen));

    for (std::size_t i = 0; i < std::size(kFrozen); ++i)
    {
        INFO("parámetro " << kFrozen[i].id);
        CHECK(kParameters[i].id == kFrozen[i].id);
        CHECK(kParameters[i].kind == kFrozen[i].kind);
        CHECK(kParameters[i].min == kFrozen[i].min);
        CHECK(kParameters[i].max == kFrozen[i].max);
    }
}

TEST_CASE("Parametros: cada parametro nuevo debe anadirse al contrato congelado")
{
    CHECK(kParameters.size() == std::size(kFrozen));
}

TEST_CASE("Parametros: la tabla es coherente")
{
    std::set<std::string_view> ids;
    for (const auto& spec : kParameters)
    {
        INFO("parámetro " << spec.id);
        CHECK_FALSE(spec.id.empty());
        CHECK(spec.id.find_first_of(" \t\r\n") == std::string_view::npos); // el formato de estado separa por espacios
        CHECK(ids.insert(spec.id).second);
        CHECK(spec.min < spec.max);
        CHECK(spec.defaultValue >= spec.min);
        CHECK(spec.defaultValue <= spec.max);
    }
}

TEST_CASE("Parametros: findParameter")
{
    CHECK(findParameter("output_gain") == kOutputGain);
    CHECK(findParameter("bypass") == kBypass);
    CHECK_FALSE(findParameter("no_existe").has_value());
}

TEST_CASE("Parametros: sanitize")
{
    const auto& gain = kParameters[kOutputGain];
    CHECK(sanitize(gain, 3.0f) == 3.0f);
    CHECK(sanitize(gain, 100.0f) == 24.0f);
    CHECK(sanitize(gain, -100.0f) == -24.0f);
    CHECK(sanitize(gain, std::numeric_limits<float>::quiet_NaN()) == gain.defaultValue);
    CHECK(sanitize(gain, std::numeric_limits<float>::infinity()) == gain.defaultValue);

    const auto& bypass = kParameters[kBypass];
    CHECK(sanitize(bypass, 0.7f) == 1.0f);
    CHECK(sanitize(bypass, 0.2f) == 0.0f);
    CHECK(sanitize(bypass, 5.0f) == 1.0f);
}
