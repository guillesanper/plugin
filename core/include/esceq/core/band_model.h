// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/dsp/equalizer.h"

#include <optional>

/// Traducción entre los parámetros del plugin y los ajustes del motor de
/// audio. El procesador y la interfaz usan estas mismas funciones, así que
/// la curva que se dibuja es siempre la que suena.
namespace esceq::core {

/// Tipo de filtro a partir del valor del parámetro "type".
dsp::FilterType filterTypeFromChoice(float choice) noexcept;

/// Canal de la banda a partir del valor del parámetro "chan".
dsp::ChannelMode channelModeFromChoice(float choice) noexcept;

/// Pendiente en dB/oct a partir del valor del parámetro "slope".
int slopeFromChoice(float choice) noexcept;

/// true si la ganancia de la banda influye en el sonido para ese tipo.
bool usesGain(dsp::FilterType type) noexcept;

/// true si la dinámica influye en el sonido para ese tipo (los que usan ganancia).
bool usesDynamics(dsp::FilterType type) noexcept;

/// true si el Q influye en el sonido para ese tipo y pendiente.
bool usesQ(dsp::FilterType type, int slopeDbPerOct) noexcept;

/// true si la pendiente influye en el sonido para ese tipo.
bool usesSlope(dsp::FilterType type) noexcept;

/// Ajustes de la banda `band` (0..kNumBands-1), dinámica incluida.
dsp::BandSettings bandSettings(const ParameterValues& values, int band) noexcept;

/// Primera banda desactivada, o nullopt si están todas en uso.
std::optional<int> firstFreeBand(const ParameterValues& values) noexcept;

} // namespace esceq::core
