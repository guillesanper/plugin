// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/fft.h"
#include "esceq/dsp/realtime.h"

#include <complex>
#include <span>
#include <vector>

/// Convolución estéreo con FIR largos, por bloques y en el dominio de la
/// frecuencia (convolución particionada uniforme con solapamiento y descarte).
///
/// Cada canal de salida es la suma de los dos de entrada, cada uno con su FIR:
///
///     salidaL = ll ∗ L + lr ∗ R
///     salidaR = rl ∗ L + rr ∗ R
///
/// Los dos canales viajan juntos en una sola FFT compleja (L + i·R). Si
/// ll = rr y lr = rl = 0 (lo normal), cuesta una FFT directa, una inversa y un
/// producto por partición y por bloque; con canales distintos, un producto más.
///
/// Referencias: W. G. Gardner, "Efficient convolution without input-output
/// delay", JAES 43(3), 1995; F. Wefers, "Partitioned convolution algorithms
/// for real-time auralization", tesis, RWTH Aachen, 2015. Ver docs/dsp/fase-lineal.md.
namespace esceq::dsp
{

/// Un FIR estéreo preparado para PartitionedConvolver: la FFT de cada
/// partición de blockSize coeficientes. Se construye fuera del hilo de audio
/// (asigna memoria y hace una FFT por partición) y después solo se lee.
class FirKernel
{
public:
    /// `ll`, `lr`, `rl` y `rr` tienen la misma longitud, múltiplo de
    /// `blockSize` (potencia de 2, entre 2 y 2^19). `lr` y `rl` vacíos
    /// equivalen a ceros; `rr` vacío equivale a `ll`.
    FirKernel(int blockSize, std::span<const double> ll, std::span<const double> lr = {},
              std::span<const double> rl = {}, std::span<const double> rr = {});

    int blockSize() const noexcept { return blockSize_; }
    int partitions() const noexcept { return partitions_; }
    /// true si los canales no se tratan igual (ll ≠ rr) o se mezclan (lr, rl ≠ 0).
    bool hasCrossTerms() const noexcept { return !mirrored_.empty(); }

    /// Espectro (2·blockSize valores) de la partición `p` para la señal L + i·R.
    const std::complex<float>* direct(int p) const noexcept;
    /// Espectro de la partición `p` para la señal conjugada, L − i·R
    /// (nullptr si !hasCrossTerms()).
    const std::complex<float>* mirrored(int p) const noexcept;

private:
    int blockSize_;
    int partitions_ = 0;
    std::vector<std::complex<float>> direct_;
    std::vector<std::complex<float>> mirrored_;
};

/// Convolución por bloques de blockSize muestras con FirKernel de hasta
/// `partitions` particiones.
///
/// Latencia: blockSize muestras. Un FirKernel de longitud n con su centro
/// en n/2 (fase lineal) suma n/2 más.
///
/// Garantías:
///  - process() y reset() son seguras en tiempo real.
///  - El resultado no depende de cómo se trocee el audio en llamadas.
///  - No es segura entre hilos.
class PartitionedConvolver
{
public:
    /// Asigna memoria. `blockSize`: potencia de 2 entre 2 y 2^19.
    PartitionedConvolver(int blockSize, int partitions);

    int blockSize() const noexcept { return blockSize_; }
    int partitions() const noexcept { return partitions_; }

    /// Silencio previo: borra la entrada pasada y la salida pendiente.
    void reset() noexcept ESCEQ_NONBLOCKING;

    /// Muestras que faltan para completar el bloque en curso (1..blockSize).
    int samplesUntilBlock() const noexcept { return blockSize_ - position_; }

    /// Procesa `length` muestras, como mucho samplesUntilBlock().
    ///
    /// `left` y `right` son la entrada (para mono, `right` = `left` y
    /// `outRight` = nullptr). La salida puede ser la misma memoria que la entrada.
    ///
    /// Si con estas muestras se completa un bloque, se calcula la salida del
    /// bloque siguiente con `kernel`. Con `fadeFrom` no nulo, esa salida pasa
    /// gradualmente, a lo largo del bloque, de la de `fadeFrom` a la de
    /// `kernel` (para cambiar de FIR sin chasquidos). Los núcleos deben tener
    /// el mismo blockSize y como mucho partitions() particiones; si no, ese
    /// bloque sale en silencio.
    void process(const float* left, const float* right, float* outLeft, float* outRight, int length,
                 const FirKernel& kernel, const FirKernel* fadeFrom = nullptr) noexcept ESCEQ_NONBLOCKING;

private:
    bool fits(const FirKernel& kernel) const noexcept;
    void computeBlock(const FirKernel& kernel, const FirKernel* fadeFrom) noexcept;
    /// Suma en `accumulator` el producto de las particiones de `kernel` por la entrada pasada.
    void accumulate(const FirKernel& kernel, std::complex<float>* accumulator) const noexcept;

    int blockSize_;
    int fftSize_;
    int partitions_;
    Fft fft_;

    int position_ = 0;                          ///< Muestras del bloque en curso.
    int newest_ = 0;                            ///< Ranura de spectra_ con el bloque más reciente.
    std::vector<std::complex<float>> input_;    ///< 2·blockSize: bloque anterior y bloque en curso, L + i·R.
    std::vector<std::complex<float>> spectra_;  ///< partitions × fftSize: FFT de los últimos bloques.
    std::vector<std::complex<float>> work_;     ///< fftSize
    std::vector<std::complex<float>> fadeWork_; ///< fftSize
    std::vector<std::complex<float>> output_;   ///< blockSize: salida del bloque en curso, L + i·R.
};

} // namespace esceq::dsp
