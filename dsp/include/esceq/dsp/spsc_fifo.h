// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace esceq::dsp {

/// Cola de muestras sin bloqueo para un productor y un consumidor.
/// Uso típico: el hilo de audio escribe y la interfaz lee para el analizador.
///
/// Garantías:
///  - write() y read() son seguras en tiempo real y no bloquean nunca.
///  - Si la cola está llena, write() descarta lo que no cabe.
///  - Exactamente UN hilo escribe y UN hilo lee. Con más de uno por lado
///    el comportamiento no está definido.
class SpscFifo
{
public:
    /// La capacidad se redondea a la potencia de 2 siguiente. Asigna memoria.
    explicit SpscFifo(int capacity);

    int capacity() const noexcept { return static_cast<int>(buffer_.size()); }

    /// Productor: escribe hasta `numSamples` muestras. Devuelve cuántas cupieron.
    int write(const float* samples, int numSamples) noexcept;

    /// Productor: escribe la media de `numChannels` canales.
    int writeMix(const float* const* channels, int numChannels, int numSamples) noexcept;

    /// Consumidor: lee hasta `maxSamples`. Devuelve cuántas leyó.
    int read(float* destination, int maxSamples) noexcept;

    /// Muestras disponibles para leer (aproximado si el otro hilo está activo).
    int available() const noexcept;

private:
    std::vector<float> buffer_;
    std::size_t mask_;
    std::atomic<std::size_t> writePos_{0};
    std::atomic<std::size_t> readPos_{0};
};

} // namespace esceq::dsp
