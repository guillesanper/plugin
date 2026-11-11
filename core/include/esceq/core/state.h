// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"

#include <optional>
#include <string>
#include <string_view>

namespace esceq::core {

/// Versión del formato de estado que escribe este código.
/// Subirla exige añadir una migración y un fixture nuevo en
/// tests/fixtures/state/. Ver docs/decisiones/0003-formato-de-estado.md.
inline constexpr int kStateVersion = 1;

/// Serializa el estado en texto UTF-8:
///
///     esceQ-state 1
///     output_gain 0
///     bypass 0
///
/// Una línea por parámetro (`id valor`), con punto decimal siempre,
/// independientemente de la configuración regional. No es segura en
/// tiempo real (asigna memoria).
std::string serializeState(const ParameterValues& values);

/// Lee un estado escrito por cualquier versión de esceQ.
///
///  - Parámetros ausentes: valor por defecto.
///  - Ids desconocidos (estado de una versión más nueva): se ignoran.
///  - Valores fuera de rango o ilegibles: se ajustan o pasan a por defecto.
///  - Cabecera inválida: devuelve nullopt y el llamador conserva su estado.
///
/// No es segura en tiempo real.
std::optional<ParameterValues> deserializeState(std::string_view text);

} // namespace esceq::core
