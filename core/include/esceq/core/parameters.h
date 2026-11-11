// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace esceq::core {

enum class ParameterKind
{
    Continuous, ///< Valor real en [min, max].
    Toggle,     ///< 0 o 1.
};

struct ParameterSpec
{
    std::string_view id;   ///< CONTRATO: no se renombra ni se reutiliza jamás.
    std::string_view name; ///< Texto visible; se puede cambiar libremente.
    std::string_view unit;
    ParameterKind kind;
    float min;
    float max;
    float defaultValue;
};

/// Posición de cada parámetro en kParameters.
enum ParameterIndex : std::size_t
{
    kOutputGain,
    kBypass,
    kNumParameters
};

/// Única fuente de verdad de los parámetros del plugin. El procesador, la
/// interfaz y el estado guardado se construyen a partir de esta tabla.
///
/// Reglas (ver docs/decisiones/0004-parametros-congelados.md):
///  - Los parámetros nuevos se añaden AL FINAL.
///  - Un `id` publicado no se cambia ni se borra; tampoco su rango.
///  - tests/core/parameters_test.cpp fija el contrato y falla si se rompe.
inline constexpr std::array<ParameterSpec, kNumParameters> kParameters{{
    {"output_gain", "Output Gain", "dB", ParameterKind::Continuous, -24.0f, 24.0f, 0.0f},
    {"bypass", "Bypass", "", ParameterKind::Toggle, 0.0f, 1.0f, 0.0f},
}};

static_assert(kParameters[kOutputGain].id == "output_gain");
static_assert(kParameters[kBypass].id == "bypass");

/// Valores de todos los parámetros, en el orden de kParameters.
using ParameterValues = std::array<float, kNumParameters>;

/// Valores por defecto de la tabla.
ParameterValues defaultValues() noexcept;

/// Índice del parámetro con ese id, o nullopt si no existe.
std::optional<std::size_t> findParameter(std::string_view id) noexcept;

/// Ajusta `value` al rango del parámetro; los Toggle quedan en 0 o 1.
/// Un valor no finito devuelve el valor por defecto.
float sanitize(const ParameterSpec& spec, float value) noexcept;

} // namespace esceq::core
