// SPDX-License-Identifier: AGPL-3.0-or-later
//
// La máscara de la dinámica espectral, de la rejilla publicada (192 puntos)
// a las columnas de la curva (PLAN.md 5.7).
#include "esceq/ui/curve_mapping.h"
#include "esceq/ui/spectral_mask.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace esceq;
using namespace esceq::ui;

TEST_CASE("Mascara espectral: la rejilla es la de la curva")
{
    // Si la curva cambiara de rango, la rejilla publicada dejaría de cubrirla.
    STATIC_REQUIRE(core::kSpectralMaskMinHz == kMinDisplayHz);
    STATIC_REQUIRE(core::kSpectralMaskMaxHz == kMaxDisplayHz);
    CHECK_THAT(core::spectralMaskPointHz(0), WithinAbs(20.0, 1e-9));
    CHECK_THAT(core::spectralMaskPointHz(core::kSpectralMaskPoints - 1), WithinAbs(20000.0, 1e-6));
}

TEST_CASE("Mascara espectral: en los puntos de la rejilla, su valor; entre ellos, interpolado")
{
    core::SpectralMaskPoints points{};
    for (int i = 0; i < core::kSpectralMaskPoints; ++i)
        points[static_cast<std::size_t>(i)] = -0.05f * static_cast<float>(i);

    for (int i = 0; i < core::kSpectralMaskPoints; ++i)
        CHECK(spectralMaskAt(points, core::spectralMaskPointHz(i)) == points[static_cast<std::size_t>(i)]);

    // A mitad de camino (en escala logarítmica) entre dos puntos, la media.
    const double between = std::sqrt(core::spectralMaskPointHz(100) * core::spectralMaskPointHz(101));
    CHECK_THAT(spectralMaskAt(points, between), WithinAbs(-0.05 * 100.5, 1e-4));

    // Fuera de la rejilla, el extremo.
    CHECK(spectralMaskAt(points, 10.0) == points.front());
    CHECK(spectralMaskAt(points, 0.0) == points.front());
    CHECK(spectralMaskAt(points, 22000.0) == points.back());
}

TEST_CASE("Mascara espectral: una reduccion estrecha llega a las columnas de la curva con su profundidad")
{
    // Un tono a 2 kHz: el detector mide en 1/6 de octava, así que su
    // reducción ocupa unos 3 puntos de la rejilla (1/19 de octava cada uno).
    // La columna más cercana la ve entera, en cualquier ancho de ventana, y
    // lejos de ella no hay nada.
    core::SpectralMaskPoints points{};
    constexpr int kTone = 125; // ~2,0 kHz
    for (int i = kTone - 1; i <= kTone + 1; ++i)
        points[static_cast<std::size_t>(i)] = -6.0f;
    const double toneHz = core::spectralMaskPointHz(kTone);
    for (const float width : {540.0f, 1180.0f, 2400.0f})
    {
        INFO(width);
        const CurveMapping m({0.0f, 0.0f, width, 300.0f}, 18.0);
        const int columns = static_cast<int>(width / 2.0f);
        std::vector<double> hz(static_cast<std::size_t>(columns));
        for (int i = 0; i < columns; ++i)
            hz[static_cast<std::size_t>(i)] =
                m.frequencyForX(width * static_cast<float>(i) / static_cast<float>(columns - 1));
        std::vector<float> out(hz.size(), 1.0f);
        spectralMaskColumns(points, hz, out);

        float deepest = 0.0f;
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            deepest = std::min(deepest, out[i]);
            if (std::abs(std::log2(hz[i] / toneHz)) > 0.2)
                CHECK(out[i] == 0.0f);
        }
        CHECK_THAT(deepest, WithinAbs(-6.0, 1e-4));
    }
}
