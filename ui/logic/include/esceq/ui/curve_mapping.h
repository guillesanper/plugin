// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/dsp/equalizer.h"

#include <optional>
#include <span>
#include <vector>

/// Geometría de la vista de la curva, sin dependencias gráficas.
namespace esceq::ui {

inline constexpr double kMinDisplayHz = 20.0;
inline constexpr double kMaxDisplayHz = 20000.0;
inline constexpr double kAnalyzerTopDb = 0.0;
inline constexpr double kAnalyzerBottomDb = -90.0;

struct Point
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

/// Correspondencia entre frecuencia/ganancia y píxeles.
/// Eje X logarítmico de kMinDisplayHz a kMaxDisplayHz.
/// Eje Y lineal en dB, de +rangeDb (arriba) a -rangeDb (abajo).
class CurveMapping
{
public:
    CurveMapping(Rect area, double rangeDb) noexcept;

    float xForFrequency(double hz) const noexcept;
    double frequencyForX(float x) const noexcept;

    float yForGain(double db) const noexcept;
    double gainForY(float y) const noexcept;

    /// El analizador usa su propia escala: kAnalyzerTopDb arriba, kAnalyzerBottomDb abajo.
    float yForAnalyzerDb(double db) const noexcept;

    Rect area() const noexcept { return area_; }
    double rangeDb() const noexcept { return rangeDb_; }

private:
    Rect area_;
    double rangeDb_;
};

/// Posición del nodo de una banda: su frecuencia y, si el tipo usa ganancia,
/// su ganancia; si no (cortes, notch...), la línea de 0 dB.
Point nodePosition(const CurveMapping& mapping, const dsp::BandSettings& band) noexcept;

/// Índice del nodo visible más cercano a `p` dentro de `radius` píxeles.
/// `visible[i]` indica si el nodo i se dibuja.
std::optional<int> hitTestNodes(std::span<const Point> nodes, std::span<const bool> visible, Point p,
                                float radius) noexcept;

/// `count` frecuencias repartidas logarítmicamente entre los límites de la vista.
std::vector<double> logFrequencies(int count);

/// Frecuencias con línea de rejilla (20, 30, 40... 100, 200...) y si llevan texto.
struct GridLine
{
    double value;
    bool labelled;
};
std::vector<GridLine> frequencyGrid();

/// Ganancias con línea de rejilla para un rango de ±rangeDb.
std::vector<GridLine> gainGrid(double rangeDb);

} // namespace esceq::ui
