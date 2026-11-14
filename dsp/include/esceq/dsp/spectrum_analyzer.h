// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/fft.h"

#include <complex>
#include <span>
#include <vector>

namespace esceq::dsp {

/// Analizador de espectro para dibujar, pensado para el hilo de la interfaz.
///
/// Guarda las últimas fftSize() muestras recibidas y, a petición, calcula
/// su espectro con ventana de Hann. Calibrado para que un seno de amplitud
/// 1 se lea a 0 dB en su frecuencia.
///
/// No es segura en tiempo real (el constructor asigna memoria y compute()
/// hace una FFT entera) ni entre hilos.
class SpectrumAnalyzer
{
public:
    explicit SpectrumAnalyzer(int fftOrder = 12);

    int fftSize() const noexcept { return fft_.size(); }

    void setSampleRate(double sampleRate) noexcept;

    /// Inclinación en dB por octava, con 0 dB de corrección en 1 kHz.
    /// Un valor de 4,5 hace que la música suene "plana" en la pantalla.
    void setTiltDbPerOctave(double tilt) noexcept { tilt_ = tilt; }

    /// Añade muestras al historial. Las no finitas (NaN, infinito) cuentan como silencio.
    void push(const float* samples, int numSamples) noexcept;

    /// Vacía el historial (silencio).
    void clear() noexcept;

    /// Nivel en dB del historial en cada frecuencia de `frequencies`.
    /// Cada punto toma el máximo de los bins dentro de ±1/24 de octava, o
    /// interpola entre los dos bins vecinos si no hay ninguno. Los niveles
    /// nunca bajan de kFloorDb. `levelsDb` debe tener el mismo tamaño.
    void compute(std::span<const double> frequencies, std::span<float> levelsDb);

    static constexpr float kFloorDb = -150.0f;

private:
    Fft fft_;
    std::vector<float> history_;
    std::vector<float> window_;
    std::vector<std::complex<float>> buffer_;
    std::vector<float> binDb_;
    std::size_t writePos_ = 0;
    double sampleRate_ = 48000.0;
    double tilt_ = 0.0;
};

} // namespace esceq::dsp
