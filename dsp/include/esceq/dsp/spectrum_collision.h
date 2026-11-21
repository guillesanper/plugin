// SPDX-License-Identifier: MIT
#pragma once

#include <span>
#include <vector>

/// Detección de colisiones (enmascaramiento) entre dos espectros.
///
/// Todo trabaja sobre espectros ya calculados en dB, en una rejilla de
/// frecuencias creciente (normalmente logarítmica) común a los dos. Las
/// funciones no conocen el origen de los espectros: sirven igual para dos
/// pistas de una sesión que para dos señales de un test.
///
/// Derivación, valores por defecto y sus límites: docs/dsp/colisiones.md.
namespace esceq::dsp
{

/// Umbrales de la detección. Los niveles son los del analizador (con su
/// inclinación), así que "presente" significa "visible en la pantalla".
struct CollisionSettings
{
    /// Por debajo de este nivel una señal no está presente.
    float floorDb = -66.0f;
    /// Una señal no está presente a más de esto por debajo de su propio máximo:
    /// la cola de una pista a -50 dB de su cuerpo no compite con nada.
    float relevanceRangeDb = 30.0f;
    /// Anchura de la transición suave entre "ausente" y "presente".
    float presenceRampDb = 6.0f;
    /// La otra señal no enmascara si está más de esto por debajo de la propia.
    /// Entre ese punto y el mismo nivel, la severidad crece suavemente.
    float maskingWindowDb = 15.0f;

    friend bool operator==(const CollisionSettings&, const CollisionSettings&) = default;
};

/// Promedio en potencia de `levelsDb` dentro de ±`widthOctaves`/2 alrededor
/// de cada frecuencia de `frequencies` (creciente, en Hz). Con anchura 0, o
/// un solo punto, copia. `out` debe tener el mismo tamaño que `levelsDb`.
///
/// Simula la resolución del oído, que no separa componentes dentro de una
/// banda crítica (en torno a 1/3 de octava). Segura en tiempo real (no asigna).
///
/// Con `minWidthHz` > 0, la ventana (simétrica en escala logarítmica) nunca
/// mide menos de eso en Hz: en graves se ensancha para que abarque bastantes
/// bins de la FFT (la igualación, docs/dsp/igualacion.md). smoothWindowOctaves()
/// da su ancho. Con 0, el resultado es el de siempre, bit a bit.
void smoothOctaves(std::span<const double> frequencies, std::span<const float> levelsDb, double widthOctaves,
                   std::span<float> out, double minWidthHz = 0.0) noexcept;

/// Ancho en octavas de la ventana de smoothOctaves() en `frequencyHz` (> 0).
double smoothWindowOctaves(double frequencyHz, double widthOctaves, double minWidthHz = 0.0) noexcept;

/// Cuánto está presente (0..1) una señal de nivel `levelDb` en una pista cuyo
/// máximo es `peakDb`: 1 con margen por encima de los dos umbrales
/// (`floorDb` y `peakDb - relevanceRangeDb`), 0 por debajo de cualquiera, y
/// una transición suave de `presenceRampDb` entre medias.
float presence(float levelDb, float peakDb, const CollisionSettings& settings) noexcept;

/// Cuánto enmascara (0..1) la otra señal a la propia en un punto, según su
/// diferencia de nivel `otherDb - ownDb`: 1 si la otra está igual o más alta,
/// 0 si está `maskingWindowDb` o más por debajo.
float maskingFactor(float otherMinusOwnDb, const CollisionSettings& settings) noexcept;

/// Severidad 0..1 de la colisión en cada punto: presencia de la propia ×
/// presencia de la otra × enmascaramiento. Los dos espectros deben estar ya
/// suavizados (smoothOctaves) y tener el mismo tamaño que `severity`.
/// Segura en tiempo real (no asigna).
void collisionSeverity(std::span<const float> ownDb, std::span<const float> otherDb, std::span<float> severity,
                       const CollisionSettings& settings) noexcept;

/// Tramo de frecuencias contiguo con colisión.
struct CollisionRegion
{
    double lowHz = 0.0;
    double highHz = 0.0;
    double peakHz = 0.0; ///< Donde la severidad es máxima (el centro, si lo es en varios puntos).
    float peakSeverity = 0.0f;
    std::size_t first = 0; ///< Primer punto del tramo en la rejilla.
    std::size_t last = 0;  ///< Último punto (incluido).

    friend bool operator==(const CollisionRegion&, const CollisionRegion&) = default;
};

/// Tramos donde `severity` llega a `threshold`, ordenados de más a menos
/// severos (y, a igual severidad, de grave a agudo). Dos tramos separados por
/// menos de `mergeGapOctaves` se unen en uno; los de anchura menor que
/// `minWidthOctaves` se descartan. Asigna memoria.
std::vector<CollisionRegion> findCollisionRegions(std::span<const double> frequencies, std::span<const float> severity,
                                                  float threshold, double mergeGapOctaves = 1.0 / 6.0,
                                                  double minWidthOctaves = 0.0);

/// Promedio temporal exponencial de un espectro, en potencia.
///
/// Para detectar colisiones interesa lo que suena de forma sostenida, no
/// cada FFT: un golpe de bombo no debe marcar una colisión permanente, ni una
/// nota que acaba de dejar de sonar debe desaparecer de golpe. Promediar en
/// potencia (no en dB) da el nivel medio real.
///
/// Un promediador vacío toma el primer espectro tal cual. No es seguro entre hilos.
class SpectrumAverager
{
public:
    /// `points` niveles, todos en `floorDb`. `timeConstantSeconds` > 0.
    SpectrumAverager(std::size_t points, double timeConstantSeconds, float floorDb);

    /// Mezcla `levelsDb` (mismo tamaño) tras `dtSeconds`. No asigna.
    void update(std::span<const float> levelsDb, double dtSeconds) noexcept;

    /// Vuelve al estado vacío: el siguiente update() se toma tal cual.
    void reset() noexcept;

    const std::vector<float>& levels() const noexcept { return levels_; }
    bool empty() const noexcept { return empty_; }

private:
    std::vector<float> levels_;
    double timeConstant_;
    float floorDb_;
    bool empty_ = true;
};

} // namespace esceq::dsp
