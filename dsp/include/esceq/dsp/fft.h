// SPDX-License-Identifier: MIT
#pragma once

#include "esceq/dsp/realtime.h"

#include <complex>
#include <vector>

namespace esceq::dsp {

/// FFT compleja radix-2, en el sitio y sin normalizar:
///   forward: X[k] = sum_n x[n] · e^(-2πi·k·n/N)
///   inverse: x[n] = sum_k X[k] · e^(+2πi·k·n/N)
/// forward() seguida de inverse() multiplica la señal por N.
///
/// En float para el audio y el analizador; en double para diseñar filtros,
/// donde el error de redondeo de una FFT larga se notaría en la respuesta.
///
/// El constructor asigna memoria; forward() e inverse() no, y son seguras en
/// tiempo real. Un mismo objeto se puede usar desde varios hilos a la vez
/// (las transformadas solo leen sus tablas), cada uno con sus datos.
template <typename T>
class BasicFft
{
public:
    using Complex = std::complex<T>;

    /// Tamaño 2^order, con order entre 1 y 20.
    explicit BasicFft(int order);

    int size() const noexcept { return size_; }

    /// Transforma `data` (size() elementos) en el sitio.
    void forward(Complex* data) const noexcept ESCEQ_NONBLOCKING;

    /// Transformada inversa de `data` (size() elementos) en el sitio, sin
    /// dividir por size().
    void inverse(Complex* data) const noexcept ESCEQ_NONBLOCKING;

private:
    void transform(Complex* data, bool inverse) const noexcept ESCEQ_NONBLOCKING;

    int size_;
    std::vector<Complex> twiddles_;
    std::vector<int> bitReversed_;
};

using Fft = BasicFft<float>;
using FftDouble = BasicFft<double>;

extern template class BasicFft<float>;
extern template class BasicFft<double>;

} // namespace esceq::dsp
