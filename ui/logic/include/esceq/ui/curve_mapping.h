// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/dsp/equalizer.h"

#include <optional>
#include <span>
#include <string>
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
///
/// El nodo nunca queda fuera de la vista: si la ganancia supera el rango que
/// se muestra (por ejemplo, -30 dB con la vista en ±18 dB, puesta desde el
/// panel o por automatización), se dibuja pegado al borde, donde se puede
/// seguir seleccionando y arrastrando. Con `verticalMargin`, se queda a esa
/// distancia del borde superior o inferior: así el nodo se ve entero y queda
/// sitio para la flecha que indica que la ganancia sigue más allá.
Point nodePosition(const CurveMapping& mapping, const dsp::BandSettings& band, float verticalMargin = 0.0f) noexcept;

/// Nueva ganancia al arrastrar verticalmente `deltaY` píxeles un nodo con
/// ganancia `currentDb`. El arrastre no puede sacar el nodo de la vista
/// (±rangeDb); si ya estaba fuera, solo puede acercarlo, sin saltos.
double draggedGain(const CurveMapping& mapping, double currentDb, float deltaY) noexcept;

/// Hacia dónde se sale de la vista la ganancia de una banda: +1 si queda por
/// encima del rango visible, -1 si queda por debajo y 0 si se ve. La vista
/// lo señala con una flecha en el borde, junto al nodo.
int gainOverflow(const CurveMapping& mapping, const dsp::BandSettings& band) noexcept;

/// Rectángulo de la etiqueta flotante de un nodo: centrada sobre él, a `gap`
/// píxeles; debajo si arriba no cabe; y siempre dentro del área de la curva.
Rect floatingLabelRect(Rect area, Point node, float width, float height, float gap) noexcept;

/// Textos de la etiqueta flotante de una banda. Los campos que no influyen
/// con el tipo de filtro actual quedan vacíos.
struct NodeReadout
{
    std::string title;     ///< "Band 3 · Bell" o "Band 3 · Bell · Mid"; con detalle, siempre con el canal.
    std::string frequency; ///< "1.20 kHz"; con la escala de notas, "1.20 kHz · D6 +37 ct".
    std::string gain;      ///< "+3.0 dB"; vacío en los tipos sin ganancia.
    std::string q;         ///< "Q 1.50", o la pendiente en los cortes de 6 dB/oct.
    /// Solo con detalle y en los tipos con dinámica: "Dynamics off", o sus
    /// ajustes ("Thr -20.0 dB", "Range -6.0 dB", "Att 5.00 ms", "Rel 80.0 ms";
    /// "Auto times" en lugar de los tiempos si son automáticos, y "Sidechain"
    /// si el detector escucha la entrada externa).
    std::vector<std::string> dynamics;
};

/// Con `a4Hz` (la escala de notas visible, con esa afinación), la frecuencia
/// lleva detrás la nota más cercana y su desviación (esceq/ui/notes.h).
/// Con `detailed` (la vista ampliada, sin el panel de la banda), el título
/// dice siempre el canal y se rellena `dynamics`.
NodeReadout nodeReadout(const core::ParameterValues& values, int band, std::optional<double> a4Hz = std::nullopt,
                        bool detailed = false);

/// Ancho, en píxeles, de la columna de la escala de ganancia, a la derecha
/// de la curva (donde van las etiquetas "+6", "0", "-6"...).
inline constexpr float kGainScaleWidth = 40.0f;

/// true si `p` cae en la escala de ganancia de la curva `area`: un clic ahí
/// borra los picos mantenidos del analizador.
bool onGainScale(Rect area, Point p) noexcept;

/// Ajustes de la banda tal y como suenan ahora, para dibujar su curva: los
/// que recibe el procesador (core::bandSettings con el algoritmo de filtro de
/// la instancia) y, si la banda es dinámica, con la ganancia dinámica
/// `dynamicGainDb` sumada a la estática.
dsp::BandSettings soundingBandSettings(const core::ParameterValues& values, int band, dsp::FilterAlgorithm algorithm,
                                       double dynamicGainDb) noexcept;

/// Índice del nodo visible más cercano a `p` dentro de `radius` píxeles.
/// `visible[i]` indica si el nodo i se dibuja.
std::optional<int> hitTestNodes(std::span<const Point> nodes, std::span<const bool> visible, Point p,
                                float radius) noexcept;

/// `count` frecuencias repartidas logarítmicamente entre los límites de la vista.
/// Banda que se elige con las flechas izquierda (`step` = -1) y derecha (+1)
/// en la curva: la siguiente activa en orden de número, dando la vuelta.
/// Sin banda elegida (`current` < 0), la derecha va a la primera activa y la
/// izquierda a la última. nullopt si no hay ninguna activa.
std::optional<int> stepSelectedBand(const core::ParameterValues& values, int current, int step) noexcept;

/// Q tras mover la rueda sobre un nodo: cada paso de rueda (`delta` = ±1 en
/// las unidades de JUCE) la multiplica o divide por 2^1,5. Sin ajustar al rango.
double wheelQ(double q, double delta) noexcept;

/// Un parámetro y el valor que debe tomar.
struct ParameterChange
{
    std::size_t index = 0;
    float value = 0.0f;

    friend bool operator==(const ParameterChange&, const ParameterChange&) = default;
};

/// Cambios que crean una banda nueva con doble clic en la curva: una campana
/// en `frequencyHz` y `gainDb`, y todo lo demás de la banda (Q, pendiente,
/// dinámica, canal) en su valor por defecto, aunque la banda guardara otros
/// ajustes de cuando estuvo activa. Solo incluye los parámetros que cambian
/// respecto a `values`, y la activación va la última: la banda empieza a
/// sonar ya con sus valores. Con `q`, la campana lleva ese Q en lugar del
/// de por defecto (ajustado a su rango).
std::vector<ParameterChange> newBandChanges(const core::ParameterValues& values, int band, double frequencyHz,
                                            double gainDb, std::optional<double> q = std::nullopt);

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
