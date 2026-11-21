// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/spectrum_session.h"
#include "esceq/dsp/spectrum_collision.h"
#include "esceq/ui/curve_mapping.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// Comparación con otras instancias de la sesión, sin dependencias gráficas:
/// qué se ofrece en el menú, dónde chocan los espectros y dónde van las
/// etiquetas de las colisiones. ui/src solo lo pinta.
namespace esceq::ui
{

/// Entrada del menú del botón de comparar, debajo de las instancias, que
/// abre el panel Match (igualar a una referencia, PLAN.md 5.6).
inline constexpr std::string_view kMatchMenuText = "Match to reference\xE2\x80\xA6";

/// Una entrada del menú "Compare".
struct CompareMenuItem
{
    core::InstanceId id = 0;
    std::string name;
    bool ticked = false;
    bool hasSignal = false;

    friend bool operator==(const CompareMenuItem&, const CompareMenuItem&) = default;
};

/// Las demás instancias de la sesión (todas menos `own`), por su número.
std::vector<CompareMenuItem> compareMenuItems(std::span<const core::InstanceSpectrum> instances, core::InstanceId own,
                                              const core::CompareSelection& selection);

/// Texto del botón "Compare": vacío sin nada seleccionado, o cuántas
/// instancias se están superponiendo.
std::string compareBadge(const core::CompareSelection& selection);

/// Ajustes de la vista de colisiones.
struct CollisionViewSettings
{
    dsp::CollisionSettings detection{};
    /// Anchura del suavizado en frecuencia: aproximadamente una banda crítica.
    double smoothingOctaves = 1.0 / 3.0;
    /// Severidad a partir de la cual un tramo se marca y se etiqueta.
    float regionThreshold = 0.35f;
    /// Tramos más estrechos que esto no llevan etiqueta (sí se sombrean).
    double minLabelledOctaves = 1.0 / 6.0;
};

/// Un tramo de colisión con una de las otras instancias.
struct CollisionZone
{
    dsp::CollisionRegion region;
    int culprit = -1; ///< Índice en `others` de la instancia que enmascara.
    /// Severidad integrada sobre el tramo (suma de severidad × octavas que
    /// ocupa cada punto): una zona ancha y moderada pesa más que un pico estrecho.
    double weight = 0.0;
};

/// Colisiones de la instancia propia con varias otras.
struct CollisionOverlay
{
    std::vector<float> severity; ///< Máximo de todas, en cada punto (0..1).
    std::vector<int> culprit;    ///< Índice en `others` de la más severa en cada punto, o -1.
    /// Tramos de cada instancia por separado (pueden solaparse entre
    /// instancias), de más a menos importante según `weight`.
    std::vector<CollisionZone> zones;
};

/// `own` y cada elemento de `others` son niveles en dB sobre `frequencies`
/// (todos del mismo tamaño, logarítmica). Se suavizan y se comparan uno a uno
/// con el propio. La severidad por punto es el máximo de todas; los tramos se
/// buscan con cada instancia por separado, para que dos colisiones contiguas
/// con pistas distintas no se fundan en una. Asigna memoria: hilo de la interfaz.
CollisionOverlay computeCollisions(std::span<const double> frequencies, std::span<const float> own,
                                   std::span<const std::span<const float>> others,
                                   const CollisionViewSettings& settings = {});

/// Texto de la etiqueta de una zona: "Bass · 120 Hz".
std::string collisionLabel(const std::string& instanceName, double peakHz);

/// Coloca una etiqueta por zona, en el orden dado (el de importancia),
/// centrada en `centresX`, de anchura `widths`, en una fila a `y` dentro de
/// `area`. Una etiqueta que se solaparía (con `gap` de margen) con otra ya
/// colocada no se coloca: nullopt. Así las más importantes siempre se ven.
std::vector<std::optional<Rect>> placeLabels(std::span<const float> centresX, std::span<const float> widths, Rect area,
                                             float y, float height, float gap);

} // namespace esceq::ui
