// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/realtime.h"

namespace esceq::dsp {

/// Aplica una ganancia a un bloque multicanal, interpolando linealmente
/// hacia el valor objetivo para evitar chasquidos.
///
/// Garantías:
///  - process() es segura en tiempo real: no asigna memoria ni bloquea.
///  - El resultado no depende de cómo se trocee el audio en bloques.
///  - Con ganancia 1 y sin rampa en curso, process() no toca las muestras
///    (paso de audio bit a bit).
///  - No es segura entre hilos: todas las llamadas deben venir del mismo hilo.
class GainRamp
{
public:
    /// Fija la duración de la rampa. Llamar antes de procesar y cada vez
    /// que cambie la frecuencia de muestreo. No interrumpe el valor actual.
    void prepare(double sampleRate, double rampSeconds) noexcept;

    /// Salta a `gain` sin rampa.
    void reset(float gain) noexcept ESCEQ_NONBLOCKING;

    /// Nueva ganancia objetivo (lineal). La rampa empieza desde el valor actual.
    void setTarget(float gain) noexcept ESCEQ_NONBLOCKING;

    /// Procesa en el sitio `numSamples` muestras de `numChannels` canales.
    void process(float* const* channels, int numChannels, int numSamples) noexcept ESCEQ_NONBLOCKING;

    float current() const noexcept { return current_; }
    bool isRamping() const noexcept { return remaining_ > 0; }

private:
    float current_ = 1.0f;
    float target_ = 1.0f;
    float step_ = 0.0f;
    int remaining_ = 0;
    int rampSamples_ = 1;
};

} // namespace esceq::dsp
