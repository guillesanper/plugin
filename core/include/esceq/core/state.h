// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"

#include <optional>
#include <string>
#include <string_view>

namespace esceq::core {

/// Versión del formato de estado que escribe este código.
/// Subirla exige añadir una migración y un fixture nuevo en
/// tests/fixtures/state/. Ver docs/decisiones/0003-formato-de-estado.md y
/// docs/decisiones/0009-preferencias-deshacer-y-presets.md.
///
///  - 1: solo parámetros.
///  - 2: además, preferencias de la interfaz (líneas `ui.*`).
inline constexpr int kStateVersion = 2;

/// Preferencias de la interfaz que se guardan con el proyecto. No son
/// parámetros: el host no las automatiza ni las ve.
struct UiSettings
{
    /// Modo del analizador. CONTRATO: 0 = apagado, 1 = post, 2 = pre + post.
    int analyzerMode = 1;
    /// Rango de la curva, en dB (± este valor): 6, 12, 18 o 30.
    int displayRangeDb = 18;
    /// Tamaño de la ventana en píxeles lógicos; 0 = el tamaño por defecto.
    int windowWidth = 0;
    int windowHeight = 0;
    /// Nombre del preset cargado, o vacío. Solo informativo.
    std::string presetName{};

    friend bool operator==(const UiSettings&, const UiSettings&) = default;
};

inline constexpr int kNumAnalyzerModes = 3;
inline constexpr int kMaxPresetNameLength = 64;

/// Lleva las preferencias a valores válidos: lo que no se entienda toma su
/// valor por defecto. Los límites del tamaño de ventana son amplios a
/// propósito: la interfaz los ajusta a los suyos.
UiSettings sanitize(UiSettings settings);

/// Todo lo que se guarda con el proyecto.
struct PluginState
{
    ParameterValues parameters = defaultValues();
    UiSettings ui{};

    friend bool operator==(const PluginState&, const PluginState&) = default;
};

/// Serializa el estado completo en texto UTF-8:
///
///     esceQ-state 2
///     output_gain 0
///     bypass 0
///     ...
///     ui.analyzer 1
///     ui.range 18
///     ui.width 1180
///     ui.height 700
///     ui.preset Vocal
///
/// Una línea por dato (`clave valor`), con punto decimal siempre,
/// independientemente de la configuración regional. Las claves `ui.*` no
/// son parámetros. No es segura en tiempo real (asigna memoria).
std::string serializePluginState(const PluginState& state);

/// Lee un estado escrito por cualquier versión de esceQ.
///
///  - Datos ausentes: valor por defecto.
///  - Claves desconocidas (estado de una versión más nueva): se ignoran.
///  - Valores fuera de rango o ilegibles: se ajustan o pasan a por defecto.
///  - Cabecera inválida: devuelve nullopt y el llamador conserva su estado.
///  - Versiones anteriores: se migran paso a paso hasta la actual.
///
/// No es segura en tiempo real.
std::optional<PluginState> deserializePluginState(std::string_view text);

/// Solo los parámetros, sin preferencias de la interfaz: es lo que guarda un
/// preset. Se lee con cualquiera de las dos funciones de lectura.
std::string serializeState(const ParameterValues& values);

/// Como deserializePluginState(), pero devuelve solo los parámetros.
std::optional<ParameterValues> deserializeState(std::string_view text);

} // namespace esceq::core
