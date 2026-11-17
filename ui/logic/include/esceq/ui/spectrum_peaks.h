// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/ui/curve_mapping.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

/// Bandas desde el espectro (PLAN.md 5.3, ADR 0019), sin dependencias
/// gráficas: picos del analizador, su Q y los cambios de parámetros que crean
/// una banda en uno de ellos. Algoritmo y medidas en docs/dsp/picos.md.
///
/// Funciones puras: se pueden llamar desde cualquier hilo, y las que
/// devuelven un vector asignan memoria (la interfaz las llama en su hilo, al
/// mover el ratón o al cambiar el espectro, nunca en el de audio).
namespace esceq::ui {

/// Límites del Q estimado de un pico (los del plan: de 0,5 a 30).
inline constexpr double kPeakMinQ = 0.5;
inline constexpr double kPeakMaxQ = 30.0;

/// Distancia máxima, en octavas, entre el ratón y el pico que se marca.
inline constexpr double kPeakPickOctaves = 1.0 / 6.0;

/// Al soltar con Alt, el umbral de la dinámica queda esto por debajo del
/// nivel del pico, en dB.
inline constexpr double kPeakThresholdBelowDb = 3.0;

/// Criterios de la detección. Los valores por defecto son los del plan.
struct PeakOptions
{
    /// Prominencia mínima, en dB: cuánto debe superar el pico al mínimo de su entorno.
    double prominenceDb = 6.0;
    /// Entorno en el que se busca ese mínimo: ± estas octavas alrededor del pico.
    double neighbourhoodOctaves = 1.0 / 3.0;
    /// Nivel mínimo del pico, en dB, ya sin la inclinación.
    double minimumDb = -60.0;
    /// Inclinación que llevan los niveles, en dB/oct con 0 dB en 1 kHz (la
    /// del analizador, dsp::SpectrumAnalyzer::tiltDb). Se quita antes de
    /// buscar: los picos se miden en el espectro real.
    double tiltDbPerOctave = 0.0;
};

/// Un pico del espectro.
struct SpectrumPeak
{
    double frequencyHz = 0.0; ///< Refinada por interpolación parabólica, en Hz.
    double levelDb = 0.0;     ///< Nivel en el máximo, sin la inclinación, en dB.
    double q = kPeakMinQ;     ///< Del ancho a -3 dB, de kPeakMinQ a kPeakMaxQ.
};

/// Picos de un espectro, de graves a agudos. `levelsDb[i]` es el nivel en
/// `frequenciesHz[i]` (frecuencias crecientes y positivas, mismo tamaño; si
/// no, no hay picos). Un pico es un máximo local (o una meseta de niveles
/// iguales) que no toca los extremos, supera options.minimumDb y está al
/// menos options.prominenceDb por encima del mínimo de su entorno. Los
/// niveles no finitos no son nunca un pico ni su entorno.
std::vector<SpectrumPeak> findPeaks(std::span<const float> levelsDb, std::span<const double> frequenciesHz,
                                    const PeakOptions& options = {});

/// El pico más cercano a `frequencyHz` (en octavas) a no más de
/// `maxOctaves`, o nullopt.
std::optional<SpectrumPeak> nearestPeak(std::span<const SpectrumPeak> peaks, double frequencyHz,
                                        double maxOctaves = kPeakPickOctaves) noexcept;

/// Texto del marcador de un pico: su frecuencia, su nota con la afinación
/// `a4Hz` y su nivel: "1.20 kHz · D6 +37 ct · -18.4 dB".
std::string peakMarkerText(const SpectrumPeak& peak, double a4Hz);

/// Q de una campana con un ancho de `octaves` entre sus puntos a -3 dB
/// (simétricos en escala logarítmica): f0 / (f2 - f1). Sin ajustar al rango;
/// infinito si el ancho no es positivo.
double qForBandwidth(double octaves) noexcept;

/// Cambios que crean la banda `band` en un pico: una campana en su
/// frecuencia, con su Q y `gainDb`, y lo demás por defecto (newBandChanges).
std::vector<ParameterChange> peakBandChanges(const core::ParameterValues& values, int band, const SpectrumPeak& peak,
                                             double gainDb);

/// Cambios que, al soltar con Alt, convierten la banda recién creada en
/// dinámica para controlar la resonancia: umbral kPeakThresholdBelowDb por
/// debajo del nivel del pico, la ganancia arrastrada pasa a ser el rango (si
/// es menos de 0,05 dB, lo que se lee "0.0 dB", el rango por defecto) y la
/// ganancia estática vuelve a 0 dB. Solo incluye los que cambian respecto a
/// `values`, y la activación de la dinámica va la última.
std::vector<ParameterChange> dynamicPeakChanges(const core::ParameterValues& values, int band, const SpectrumPeak& peak);

} // namespace esceq::ui
