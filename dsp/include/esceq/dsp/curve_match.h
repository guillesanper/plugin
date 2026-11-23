// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/equalizer.h"
#include "esceq/dsp/filter_design.h"
#include "esceq/dsp/spectrum_collision.h"

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

/// Igualar a una referencia: de dos espectros medios (la pista y la
/// referencia) a las campanas y shelves que acercan la pista a la referencia,
/// sobre la curva estática de las bandas que ya hay.
///
/// Objetivo, pesos, inicio voraz y refino de Levenberg-Marquardt, con sus
/// medidas: docs/dsp/igualacion.md. Decisión: docs/decisiones/0023-igualar-a-una-referencia.md.
///
/// Puro: sin estado, sin hilos y sin azar. La misma entrada da bit a bit el
/// mismo resultado con el mismo binario. Asigna memoria y tarda hasta un
/// segundo con 24 bandas: nunca en el hilo de audio. Segura entre hilos.
namespace esceq::dsp {

/// Puntos de la rejilla del ajuste: logarítmica entre la primera y la última
/// frecuencia de los espectros, el doble de densa que la de la sesión (192
/// puntos de 20 Hz a 20 kHz), para que una campana estrecha no caiga entre
/// dos puntos.
inline constexpr int kMatchGridPoints = 384;
/// En graves, la ventana del suavizado abarca al menos estos bins de la FFT
/// que midió los espectros (CurveMatchInput::resolutionHz): con 8192 puntos,
/// 1/3 de octava por debajo de unos 200 Hz tiene menos, y su media es ruido.
inline constexpr double kMatchMinWindowBins = 8.0;
/// El objetivo nunca pasa de ±kMatchMaxTargetDb.
inline constexpr double kMatchMaxTargetDb = 12.0;
/// Límites de las bandas creadas.
inline constexpr double kMatchMaxGainDb = 18.0;
inline constexpr double kMatchMinBellQ = 0.3;
inline constexpr double kMatchMaxBellQ = 16.0;
inline constexpr double kMatchMinShelfQ = 0.5;
inline constexpr double kMatchMaxShelfQ = 1.0;
/// Peso fuera del rango, donde las bandas nuevas no deben crear nada (lo que
/// falta es lo del borde), y anchura de la transición desde el peso 1 del
/// rango, en octavas.
inline constexpr double kMatchOutsideWeight = 0.1;
inline constexpr double kMatchWeightRampOctaves = 1.0 / 6.0;
/// Parada: ningún punto del rango con un error ponderado de kMatchStopErrorDb
/// o más; una banda nueva que quita menos de kMatchMinImprovementDb de error
/// RMS; y, tras el refino, fuera las bandas de menos de kMatchMinBandGainDb.
inline constexpr double kMatchStopErrorDb = 0.5;
inline constexpr double kMatchMinImprovementDb = 0.05;
inline constexpr double kMatchMinBandGainDb = 0.3;
/// Iteraciones de Levenberg-Marquardt tras añadir cada banda y al final.
inline constexpr int kMatchStepIterations = 20;
inline constexpr int kMatchFinalIterations = 100;

/// Ajustes del panel (PLAN.md 5.6).
struct CurveMatchSettings
{
    /// Fracción de la diferencia que se iguala, de 0 a 1.
    double amount = 0.5;
    /// Ancho del suavizado de cada espectro, en octavas (0: sin suavizar,
    /// salvo el mínimo de los graves). El panel ofrece 1/6, 1/3 y 1.
    double smoothingOctaves = 1.0 / 3.0;
    /// Bandas que se pueden crear como mucho (las libres).
    int maxBands = 8;
    /// Rango que se iguala, en Hz.
    double lowHz = 30.0;
    double highHz = 16000.0;
    /// Umbrales de "presente", los de las colisiones (dsp::presence).
    CollisionSettings presence{};

    friend bool operator==(const CurveMatchSettings&, const CurveMatchSettings&) = default;
};

/// Lo que se iguala.
struct CurveMatchInput
{
    /// Rejilla de los dos espectros: creciente y positiva (la de la sesión).
    std::span<const double> frequencies;
    /// Espectros medios en dB, del mismo tamaño que la rejilla, medidos igual
    /// (los sesgos del analizador, como su inclinación, se anulan al restar).
    std::span<const float> trackDb;
    std::span<const float> referenceDb;
    /// Separación entre bins de la FFT que los midió (frecuencia de muestreo
    /// del análisis / puntos de la FFT: 5,86 Hz con 8192 puntos a 48 kHz), o
    /// 0 si no se sabe: sin ancho mínimo en graves.
    double resolutionHz = 0.0;
    /// Bandas que ya hay: se ajusta lo que falta sobre su curva estática,
    /// con la regla de canales de autoGainDb() (staticCurveDb()), vista al
    /// ancho del suavizado.
    std::span<const BandSettings> currentBands;
    int numChannels = 2;
    /// Las curvas de las bandas nuevas se calculan con este algoritmo y esta
    /// frecuencia de muestreo: lo que se ve es lo que suena.
    double sampleRate = 48000.0;
    FilterAlgorithm algorithm = FilterAlgorithm::Classic;
};

/// Resultado del ajuste. Las curvas están en la rejilla del ajuste.
struct CurveMatchResult
{
    /// Bandas propuestas (campanas y shelves), de grave a agudo, con el
    /// algoritmo de la entrada y la pendiente por defecto. Vacío si no hace
    /// falta ninguna, si no hay contenido común o si se ha cancelado.
    std::vector<FilterParams> bands;
    /// Nivel libre c del ajuste: las bandas se ajustan para que S(C + Σ Hk)
    /// se acerque a T + c, con c cerca de 0 (está penalizado). El nivel no se iguala.
    double offsetDb = 0.0;
    /// Error RMS ponderado, con el nivel libre, de lo que falta antes de las
    /// bandas propuestas y de lo que queda con ellas, en dB.
    double initialRmsDb = 0.0;
    double finalRmsDb = 0.0;

    std::vector<double> frequencies; ///< Rejilla del ajuste (kMatchGridPoints).
    std::vector<double> weights;     ///< Peso de cada punto: 0, kMatchOutsideWeight o 1, con transiciones.
    /// Curva objetivo T: centrada en la mediana ponderada, 0 donde no cuenta
    /// dentro del rango y, fuera, la del borde.
    std::vector<double> targetDb;
    std::vector<double> currentDb;   ///< Curva estática de las bandas actuales C, sin suavizar.
    std::vector<double> proposedDb;  ///< C más las bandas propuestas, sin suavizar.

    /// Curvas de una banda evaluadas en toda la rejilla: el coste, que no depende de la máquina.
    std::int64_t evaluations = 0;
    /// Iteraciones de Levenberg-Marquardt, aceptadas o no.
    int iterations = 0;
    bool cancelled = false;
};

/// Q máximo de una campana propuesta en `frequencyHz`: el de una campana
/// cuyo ancho (qForBandwidth) es el de la ventana del suavizado ahí
/// (smoothWindowOctaves, con el mínimo de los graves), entre kMatchMinBellQ y
/// kMatchMaxBellQ. Sin suavizado, kMatchMaxBellQ.
double matchMaxBellQ(double frequencyHz, double smoothingOctaves, double minWidthHz = 0.0) noexcept;

/// Ajusta las bandas que acercan la pista a la referencia (PLAN.md 5.6):
///
///     D = suavizado(R) − suavizado(P),  T = clamp(cantidad·(D − mediana_w D), ±12 dB),  N = T − S(C)
///
/// y minimiza Σ w·(N + c − [S(C + Σ Hk) − S(C)])² (fuera del rango, N es lo
/// del borde) con un nivel c casi libre. S es el promedio en potencia en la
/// ventana del suavizado: la curva actual y las bandas nuevas se comparan al
/// mismo ancho que el objetivo, así que una banda estrecha que ya hay cuenta
/// por lo que quita a ese ancho, y no se rellena. El Q de las campanas no pasa
/// de matchMaxBellQ(). Si `cancel` no es nulo y pasa a true, vuelve tras la
/// iteración en curso con `cancelled` y sin bandas.
CurveMatchResult matchCurve(const CurveMatchInput& input, const CurveMatchSettings& settings,
                            const std::atomic<bool>* cancel = nullptr);

} // namespace esceq::dsp
