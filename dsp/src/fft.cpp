// SPDX-License-Identifier: MIT
#include "esceq/dsp/fft.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace esceq::dsp {

template <typename T>
BasicFft<T>::BasicFft(int order)
    : size_(1 << std::clamp(order, 1, 20))
{
    const int bits = std::clamp(order, 1, 20);

    // Los factores se calculan en double también para float: así el error de
    // la tabla es el redondeo final y no se acumula.
    twiddles_.resize(static_cast<std::size_t>(size_ / 2));
    for (int k = 0; k < size_ / 2; ++k)
    {
        const double angle = -2.0 * std::numbers::pi * k / size_;
        twiddles_[static_cast<std::size_t>(k)] = {static_cast<T>(std::cos(angle)), static_cast<T>(std::sin(angle))};
    }

    bitReversed_.resize(static_cast<std::size_t>(size_));
    for (int i = 0; i < size_; ++i)
    {
        int reversed = 0;
        for (int b = 0; b < bits; ++b)
            reversed |= ((i >> b) & 1) << (bits - 1 - b);
        bitReversed_[static_cast<std::size_t>(i)] = reversed;
    }
}

template <typename T>
void BasicFft<T>::forward(Complex* data) const noexcept ESCEQ_NONBLOCKING
{
    transform(data, false);
}

template <typename T>
void BasicFft<T>::inverse(Complex* data) const noexcept ESCEQ_NONBLOCKING
{
    transform(data, true);
}

template <typename T>
void BasicFft<T>::transform(Complex* data, bool inverse) const noexcept ESCEQ_NONBLOCKING
{
    for (int i = 0; i < size_; ++i)
    {
        const int j = bitReversed_[static_cast<std::size_t>(i)];
        if (i < j)
            std::swap(data[i], data[j]);
    }

    // La inversa usa los factores conjugados. El producto se escribe con
    // aritmética real: el operador de std::complex comprueba los NaN en cada
    // multiplicación y es varias veces más lento.
    const T sign = inverse ? T(-1) : T(1);
    for (int length = 2; length <= size_; length *= 2)
    {
        const int half = length / 2;
        const int stride = size_ / length;
        for (int start = 0; start < size_; start += length)
        {
            for (int k = 0; k < half; ++k)
            {
                const Complex twiddle = twiddles_[static_cast<std::size_t>(k * stride)];
                const T wRe = twiddle.real();
                const T wIm = sign * twiddle.imag();
                const Complex even = data[start + k];
                const Complex odd = data[start + k + half];
                const Complex rotated{odd.real() * wRe - odd.imag() * wIm, odd.real() * wIm + odd.imag() * wRe};
                data[start + k] = {even.real() + rotated.real(), even.imag() + rotated.imag()};
                data[start + k + half] = {even.real() - rotated.real(), even.imag() - rotated.imag()};
            }
        }
    }
}

template class BasicFft<float>;
template class BasicFft<double>;

} // namespace esceq::dsp
