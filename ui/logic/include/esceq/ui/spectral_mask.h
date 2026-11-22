// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/spectral_mask.h"

#include <span>

/// La forma de la máscara de la dinámica espectral en la curva (PLAN.md 5.7):
/// de la rejilla fija en la que la publica el hilo de audio
/// (core::SpectralMaskPublisher, 192 puntos de 20 Hz a 20 kHz) a las columnas
/// de la curva, que dependen del ancho de la ventana.
namespace esceq::ui
{

/// Máscara a `hz`: interpolación lineal, en escala logarítmica de frecuencia,
/// entre los dos puntos vecinos de la rejilla; fuera de ella, el punto del
/// extremo. En las frecuencias de la rejilla, su valor exacto.
float spectralMaskAt(const core::SpectralMaskPoints& points, double hz) noexcept;

/// spectralMaskAt() en cada frecuencia de `columnHz`; `out` debe tener el mismo tamaño.
void spectralMaskColumns(const core::SpectralMaskPoints& points, std::span<const double> columnHz,
                         std::span<float> out) noexcept;

} // namespace esceq::ui
