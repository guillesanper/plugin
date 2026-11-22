// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/spectral_mask.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace esceq::ui
{

float spectralMaskAt(const core::SpectralMaskPoints& points, double hz) noexcept
{
    constexpr int kLast = core::kSpectralMaskPoints - 1;
    if (!(hz > core::kSpectralMaskMinHz))
        return points.front();
    // Posición en la rejilla: 0 en el primer punto, kLast en el último.
    const double position =
        kLast * std::log(hz / core::kSpectralMaskMinHz) / std::log(core::kSpectralMaskMaxHz / core::kSpectralMaskMinHz);
    if (position >= kLast)
        return points.back();
    // En un punto de la rejilla (con el error de redondeo del logaritmo), su valor.
    const double nearest = std::round(position);
    if (std::abs(position - nearest) < 1e-9)
        return points[static_cast<std::size_t>(nearest)];
    const int below = std::clamp(static_cast<int>(position), 0, kLast - 1);
    const auto fraction = static_cast<float>(position - below);
    const float a = points[static_cast<std::size_t>(below)];
    const float b = points[static_cast<std::size_t>(below + 1)];
    return a + fraction * (b - a);
}

void spectralMaskColumns(const core::SpectralMaskPoints& points, std::span<const double> columnHz,
                         std::span<float> out) noexcept
{
    const std::size_t count = std::min(columnHz.size(), out.size());
    for (std::size_t i = 0; i < count; ++i)
        out[i] = spectralMaskAt(points, columnHz[i]);
}

} // namespace esceq::ui
