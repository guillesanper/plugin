// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/dsp/handoff.h"
#include "esceq/dsp/realtime.h"

#include <array>
#include <span>

namespace esceq::dsp
{
class PhaseEqualizer;
}

/// La forma de la máscara de la dinámica espectral, del hilo de audio a la
/// interfaz (PLAN.md 5.7, bloque 6.6): la curva la dibuja como un sombreado
/// bajo cada banda espectral.
///
/// El motor (dsp::SpectralDynamics) da la ganancia de cada bin de cada banda,
/// en dB: 1025 bins a 48 kHz, 4097 a 192 kHz, y solo se puede leer desde el
/// hilo de audio. Aquí se reduce a kSpectralMaskPoints puntos en una rejilla
/// logarítmica fija, de 20 Hz a 20 kHz (la de la curva), y se publica unas
/// kSpectralMaskRateHz veces por segundo de audio en un dsp::TripleBuffer: el
/// hilo de audio no asigna ni espera, y la interfaz lee siempre lo último.
/// La interfaz lo lleva a sus columnas con ui::spectralMaskColumns().
///
/// Solo lee del ecualizador: publicar no cambia el sonido.
namespace esceq::core
{

inline constexpr int kSpectralMaskPoints = 192;
inline constexpr double kSpectralMaskRateHz = 30.0;
inline constexpr double kSpectralMaskMinHz = 20.0;
inline constexpr double kSpectralMaskMaxHz = 20000.0;

/// Frecuencia del punto `point` de la rejilla (0 a kSpectralMaskPoints - 1):
/// kSpectralMaskMinHz en el primero, kSpectralMaskMaxHz en el último, con la
/// misma razón entre vecinos (1/19 de octava, más o menos).
double spectralMaskPointHz(int point) noexcept;

using SpectralMaskPoints = std::array<float, kSpectralMaskPoints>;

/// Lleva la ganancia de cada bin (`binGainsDb`, en dB; el bin k está en
/// k · binHz) a la rejilla. Cada punto se queda con lo que más cambia (el
/// mayor valor absoluto) de los bins que caen en su tramo, de la mitad hasta
/// el punto anterior a la mitad hasta el siguiente en escala logarítmica, para
/// que un tono reducido en un solo bin se vea con su profundidad aunque haya
/// varios bins por punto (en agudos). Si no cae ninguno (en graves, donde los
/// bins están más separados que los puntos), interpola entre los dos bins
/// vecinos. Por encima del último bin, 0. Con `binGainsDb` vacía, todo 0.
void reduceSpectralMask(std::span<const float> binGainsDb, double binHz, SpectralMaskPoints& out) noexcept
    ESCEQ_NONBLOCKING;

/// Lo que se publica: qué bandas tienen máscara ahora y su forma.
struct SpectralMaskFrame
{
    std::array<bool, kNumBands> active{};                ///< La banda actúa en la dinámica espectral.
    std::array<SpectralMaskPoints, kNumBands> gainsDb{}; ///< Su máscara en la rejilla; 0 si no actúa.

    bool anyActive() const noexcept;
};

/// Publicador de la máscara: el hilo de audio escribe, un único lector (la
/// curva del editor abierto) lee.
class SpectralMaskPublisher
{
public:
    /// Fija cada cuántas muestras se publica. No asigna memoria (todo vive
    /// dentro del objeto), pero no es para el hilo de audio.
    void prepare(double sampleRate) noexcept;

    /// Tras procesar un bloque de `numSamples` muestras: si ha pasado el
    /// intervalo, reduce y publica las máscaras de `equalizer`. Mientras no
    /// suena la dinámica espectral no publica nada, salvo una vez al dejar de
    /// sonar (con todas las bandas inactivas, para que la interfaz borre el
    /// sombreado). Hilo de audio.
    void publish(const dsp::PhaseEqualizer& equalizer, int numSamples) noexcept ESCEQ_NONBLOCKING;

    /// La última publicación que el lector aún no ha visto, o nullptr. El
    /// puntero vale hasta la siguiente llamada. Un único lector.
    const SpectralMaskFrame* read() noexcept { return buffer_.read(); }

    /// Muestras entre publicaciones: la frecuencia de muestreo entre kSpectralMaskRateHz.
    int interval() const noexcept { return interval_; }

private:
    dsp::TripleBuffer<SpectralMaskFrame> buffer_;
    int interval_ = 1600;
    int elapsed_ = 0;
    bool publishedActive_ = false;
};

} // namespace esceq::core
