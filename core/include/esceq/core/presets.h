// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/macros.h"
#include "esceq/core/parameters.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

/// Presets: un preset es un estado de esceQ que solo lleva parámetros y, desde
/// la versión 4 del estado, las asignaciones de las macros (el mismo formato
/// de texto que el estado guardado, ver state.h).
namespace esceq::core {

/// Extensión de los archivos de preset de usuario.
inline constexpr std::string_view kPresetExtension = ".esceq";

struct FactoryPreset
{
    std::string_view name;
    std::string_view state; ///< Texto en el formato de estado.
};

/// Presets que vienen con el plugin, en el orden en que se muestran.
/// CONTRATO: una vez publicado, un preset de fábrica no cambia de nombre ni
/// de sonido (hay proyectos que lo llevan cargado); los nuevos se añaden al final.
std::span<const FactoryPreset> factoryPresets() noexcept;

/// Parámetros de un preset de fábrica por su nombre, o nullopt si no existe.
std::optional<ParameterValues> loadFactoryPreset(std::string_view name);

/// Lo que carga un preset: sus parámetros y sus asignaciones de las macros.
/// Cargarlo sustituye las asignaciones por las suyas, también si no trae
/// ninguna (los de fábrica y los anteriores a la versión 4): las asignaciones
/// de antes moverían bandas que el preset usa para otra cosa (decidido con el
/// usuario en el bloque 7.2, ADR 0028).
struct PresetContents
{
    ParameterValues parameters = defaultValues();
    MacroAssignments macros{};

    friend bool operator==(const PresetContents&, const PresetContents&) = default;
};

/// Lee el texto de un preset (de fábrica o de usuario): como
/// deserializePluginState(), sin el algoritmo ni las preferencias de la interfaz.
std::optional<PresetContents> deserializePreset(std::string_view text);

/// Parámetros que no forman parte de un preset: el modo de fase y la
/// compensación automática de ganancia. Son decisiones de la sesión, no del
/// sonido de un preset: elegir un preset no debe cambiar la latencia de la
/// pista ni encender o apagar la compensación (los presets anteriores la
/// apagarían). Ver docs/decisiones/0012-modos-de-fase.md y
/// docs/decisiones/0015-compensacion-automatica-de-ganancia.md.
inline constexpr std::array<std::size_t, 2> kParametersOutsidePresets{kPhaseMode, kAutoGain};

/// Lo que queda al cargar `preset` sobre `current`: los valores del preset,
/// salvo los kParametersOutsidePresets, que conservan los de `current`.
ParameterValues applyPreset(const ParameterValues& preset, const ParameterValues& current) noexcept;

/// Texto de un preset: como serializeState(), pero solo con los parámetros
/// que no tienen su valor por defecto y sin los kParametersOutsidePresets,
/// seguidos de las asignaciones de las macros (líneas `macro.N.M`, si hay).
/// Al cargarlo, los que falten toman el valor por defecto: el resultado es el
/// mismo y el archivo es legible.
std::string serializePreset(const ParameterValues& values, const MacroAssignments& macros = {});

/// Convierte lo que escribe el usuario en un nombre de preset que sirva
/// como nombre de archivo en Windows, macOS y Linux: sin caracteres
/// reservados ni de control, sin espacios ni puntos al principio o al final,
/// sin nombres reservados de Windows (CON, NUL, COM1...) y con una longitud
/// máxima. Devuelve una cadena vacía si no queda nada utilizable.
std::string sanitizePresetName(std::string_view name);

} // namespace esceq::core
