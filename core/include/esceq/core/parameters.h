// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace esceq::core {

enum class ParameterKind
{
    Continuous, ///< Valor real en [min, max].
    Toggle,     ///< 0 o 1.
    Choice,     ///< Índice entero en [0, choices.size() - 1].
};

enum class ParameterScale
{
    Linear,
    Logarithmic, ///< Cada octava (o cada duplicación) ocupa lo mismo en el control.
};

struct ParameterSpec
{
    std::string_view id{};   ///< CONTRATO: no se renombra ni se reutiliza jamás.
    std::string_view name{}; ///< Texto visible; se puede cambiar libremente.
    std::string_view unit{};
    ParameterKind kind = ParameterKind::Continuous;
    float min = 0.0f;
    float max = 1.0f;
    float defaultValue = 0.0f;
    ParameterScale scale = ParameterScale::Linear; ///< CONTRATO: define la automatización.
    std::span<const std::string_view> choices{};    ///< Solo Choice. El ORDEN es contrato; los textos no.
};

/// Número de bandas declaradas. Todas existen siempre como parámetros
/// (los hosts exigen una lista fija); las desactivadas no consumen CPU.
inline constexpr int kNumBands = 24;

/// Parámetros de cada banda, en este orden.
enum class BandParam : std::size_t
{
    Enabled,
    Type,
    Frequency,
    Gain,
    Q,
    Slope,
    Count
};

inline constexpr std::size_t kParamsPerBand = static_cast<std::size_t>(BandParam::Count);

/// Posición de cada parámetro en parameters().
enum ParameterIndex : std::size_t
{
    kOutputGain,
    kBypass,
    kFirstBandParameter,
    kNumParameters = kFirstBandParameter + static_cast<std::size_t>(kNumBands) * kParamsPerBand,
};

/// Índice en parameters() del parámetro `param` de la banda `band` (0..kNumBands-1).
constexpr std::size_t bandParameter(int band, BandParam param) noexcept
{
    return kFirstBandParameter + static_cast<std::size_t>(band) * kParamsPerBand + static_cast<std::size_t>(param);
}

/// Tipos de filtro. El índice coincide con dsp::FilterType (lo comprueba band_model.cpp).
inline constexpr std::array<std::string_view, 9> kFilterTypeChoices{
    "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "All Pass", "Tilt",
};

/// Pendientes de los cortes: el índice i equivale a 6·(i+1) dB/oct.
inline constexpr std::array<std::string_view, 16> kSlopeChoices{
    "6 dB/oct",  "12 dB/oct", "18 dB/oct", "24 dB/oct", "30 dB/oct", "36 dB/oct", "42 dB/oct", "48 dB/oct",
    "54 dB/oct", "60 dB/oct", "66 dB/oct", "72 dB/oct", "78 dB/oct", "84 dB/oct", "90 dB/oct", "96 dB/oct",
};

/// Única fuente de verdad de los parámetros del plugin. El procesador, la
/// interfaz y el estado guardado se construyen a partir de esta tabla.
///
/// Reglas (ver docs/decisiones/0004-parametros-congelados.md):
///  - Los parámetros nuevos se añaden AL FINAL.
///  - Un `id` publicado no se cambia ni se borra; tampoco su tipo, rango,
///    escala ni número de opciones.
///  - tests/fixtures/parameters/v1.txt fija el contrato y el test falla si se rompe.
///
/// La tabla se construye la primera vez que se llama (asigna memoria): no
/// llamar por primera vez desde el hilo de audio.
const std::array<ParameterSpec, kNumParameters>& parameters() noexcept;

/// Valores de todos los parámetros, en el orden de parameters().
using ParameterValues = std::array<float, kNumParameters>;

/// Valores por defecto de la tabla.
ParameterValues defaultValues() noexcept;

/// Índice del parámetro con ese id, o nullopt si no existe.
std::optional<std::size_t> findParameter(std::string_view id) noexcept;

/// Ajusta `value` al rango del parámetro; Toggle y Choice quedan en enteros.
/// Un valor no finito devuelve el valor por defecto.
float sanitize(const ParameterSpec& spec, float value) noexcept;

/// Valor del parámetro <-> posición 0..1 del host (automatización).
/// CONTRATO: cambiar esta correspondencia altera la automatización guardada.
float toNormalized(const ParameterSpec& spec, float value) noexcept;
float fromNormalized(const ParameterSpec& spec, float normalized) noexcept;

/// Texto para mostrar, con unidad ("1.20 kHz", "+3.0 dB", "Low Cut").
std::string formatValue(const ParameterSpec& spec, float value);

/// Interpreta un texto escrito por el usuario ("1.2k", "350 Hz", "-3", "bell").
/// Devuelve el valor saneado, o nullopt si no se entiende.
std::optional<float> parseValue(const ParameterSpec& spec, std::string_view text);

} // namespace esceq::core
