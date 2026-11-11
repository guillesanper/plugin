// SPDX-License-Identifier: MIT
#include "esceq/dsp/fft.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace esceq::dsp {

Fft::Fft(int order)
    : size_(1 << std::clamp(order, 1, 20))
{
    const int bits = std::clamp(order, 1, 20);

    twiddles_.resize(static_cast<std::size_t>(size_ / 2));
    for (int k = 0; k < size_ / 2; ++k)
    {
        const double angle = -2.0 * std::numbers::pi * k / size_;
        twiddles_[static_cast<std::size_t>(k)] = {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
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

void Fft::forward(std::complex<float>* data) const noexcept
{
    for (int i = 0; i < size_; ++i)
    {
        const int j = bitReversed_[static_cast<std::size_t>(i)];
        if (i < j)
            std::swap(data[i], data[j]);
    }

    for (int length = 2; length <= size_; length *= 2)
    {
        const int half = length / 2;
        const int stride = size_ / length;
        for (int start = 0; start < size_; start += length)
        {
            for (int k = 0; k < half; ++k)
            {
                const std::complex<float> twiddle = twiddles_[static_cast<std::size_t>(k * stride)];
                const std::complex<float> even = data[start + k];
                const std::complex<float> odd = data[start + k + half] * twiddle;
                data[start + k] = even + odd;
                data[start + k + half] = even - odd;
            }
        }
    }
}

} // namespace esceq::dsp
