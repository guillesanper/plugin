// SPDX-License-Identifier: MIT
#pragma once

#include <complex>
#include <vector>

namespace esceq::dsp {

/// FFT compleja radix-2, en el sitio y sin normalizar:
/// X[k] = sum_n x[n] · e^(-2πi·k·n/N).
///
/// El constructor asigna memoria; forward() no.
class Fft
{
public:
    /// Tamaño 2^order, con order entre 1 y 20.
    explicit Fft(int order);

    int size() const noexcept { return size_; }

    /// Transforma `data` (size() elementos) en el sitio. Segura en tiempo real.
    void forward(std::complex<float>* data) const noexcept;

private:
    int size_;
    std::vector<std::complex<float>> twiddles_;
    std::vector<int> bitReversed_;
};

} // namespace esceq::dsp
