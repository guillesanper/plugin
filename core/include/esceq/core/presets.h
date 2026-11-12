// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

/// Presets: un preset es un estado de esceQ que solo lleva parámetros
/// (el mismo formato de texto que el estado guardado, ver state.h).
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

/// Texto de un preset: como serializeState(), pero solo con los parámetros
/// que no tienen su valor por defecto. Al cargarlo, los que falten toman el
/// valor por defecto: el resultado es el mismo y el archivo es legible.
std::string serializePreset(const ParameterValues& values);

/// Convierte lo que escribe el usuario en un nombre de preset que sirva
/// como nombre de archivo en Windows, macOS y Linux: sin caracteres
/// reservados ni de control, sin espacios ni puntos al principio o al final,
/// sin nombres reservados de Windows (CON, NUL, COM1...) y con una longitud
/// máxima. Devuelve una cadena vacía si no queda nada utilizable.
std::string sanitizePresetName(std::string_view name);

} // namespace esceq::core
