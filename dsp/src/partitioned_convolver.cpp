// SPDX-License-Identifier: MIT
#include "esceq/dsp/partitioned_convolver.h"

#include <algorithm>
#include <bit>
#include <cassert>

namespace esceq::dsp
{

namespace
{

int orderOf(int size) noexcept
{
    return std::countr_zero(static_cast<unsigned>(size));
}

bool validBlockSize(int blockSize) noexcept
{
    return blockSize >= 2 && blockSize <= (1 << 19) && std::has_single_bit(static_cast<unsigned>(blockSize));
}

} // namespace

FirKernel::FirKernel(int blockSize, std::span<const double> ll, std::span<const double> lr, std::span<const double> rl,
                     std::span<const double> rr)
    : blockSize_(blockSize)
{
    assert(validBlockSize(blockSize));
    assert(lr.empty() || lr.size() == ll.size());
    assert(rl.empty() || rl.size() == ll.size());
    assert(rr.empty() || rr.size() == ll.size());
    if (!validBlockSize(blockSize))
        return;

    const auto length = ll.size();
    const auto block = static_cast<std::size_t>(blockSize);
    partitions_ = static_cast<int>((length + block - 1) / block);
    auto at = [](std::span<const double> taps, std::size_t i, double fallback)
    { return taps.empty() ? fallback : (i < taps.size() ? taps[i] : 0.0); };

    // Con z = L + i·R, la salida yL + i·yR es  p ∗ z + q ∗ conj(z)  con
    //   p = ((ll + rr) + i·(rl − lr)) / 2
    //   q = ((ll − rr) + i·(rl + lr)) / 2
    // (desarrollo en docs/dsp/fase-lineal.md). q = 0 si los canales se tratan igual.
    std::vector<std::complex<double>> p(length), q(length);
    bool cross = false;
    for (std::size_t i = 0; i < length; ++i)
    {
        const double a = ll[i];
        const double b = at(lr, i, 0.0);
        const double c = at(rl, i, 0.0);
        const double d = at(rr, i, a);
        p[i] = {0.5 * (a + d), 0.5 * (c - b)};
        q[i] = {0.5 * (a - d), 0.5 * (c + b)};
        cross = cross || q[i] != std::complex<double>{};
    }

    // Cada partición: sus coeficientes seguidos de ceros, en una FFT de
    // 2·blockSize. La normalización de la FFT inversa (1/N) va incluida.
    const int fftSize = 2 * blockSize;
    const FftDouble fft(orderOf(fftSize));
    const double scale = 1.0 / static_cast<double>(fftSize);
    std::vector<std::complex<double>> buffer(static_cast<std::size_t>(fftSize));
    auto transform = [&](const std::vector<std::complex<double>>& taps, std::vector<std::complex<float>>& out)
    {
        out.assign(static_cast<std::size_t>(partitions_ * fftSize), {});
        for (int part = 0; part < partitions_; ++part)
        {
            std::fill(buffer.begin(), buffer.end(), std::complex<double>{});
            const std::size_t first = static_cast<std::size_t>(part) * block;
            for (std::size_t i = 0; i < block && first + i < length; ++i)
                buffer[i] = taps[first + i];
            fft.forward(buffer.data());
            auto* destination = out.data() + static_cast<std::size_t>(part * fftSize);
            for (int k = 0; k < fftSize; ++k)
                destination[k] = std::complex<float>(buffer[static_cast<std::size_t>(k)] * scale);
        }
    };

    transform(p, direct_);
    if (cross)
        transform(q, mirrored_);
}

const std::complex<float>* FirKernel::direct(int p) const noexcept
{
    return direct_.data() + static_cast<std::size_t>(p) * static_cast<std::size_t>(2 * blockSize_);
}

const std::complex<float>* FirKernel::mirrored(int p) const noexcept
{
    if (mirrored_.empty())
        return nullptr;
    return mirrored_.data() + static_cast<std::size_t>(p) * static_cast<std::size_t>(2 * blockSize_);
}

PartitionedConvolver::PartitionedConvolver(int blockSize, int partitions)
    : blockSize_(validBlockSize(blockSize) ? blockSize : 2), fftSize_(2 * blockSize_),
      partitions_(std::max(1, partitions)), fft_(orderOf(fftSize_)), input_(static_cast<std::size_t>(fftSize_)),
      spectra_(static_cast<std::size_t>(partitions_ * fftSize_)), work_(static_cast<std::size_t>(fftSize_)),
      fadeWork_(static_cast<std::size_t>(fftSize_)), output_(static_cast<std::size_t>(blockSize_))
{
    assert(validBlockSize(blockSize));
}

void PartitionedConvolver::reset() noexcept ESCEQ_NONBLOCKING
{
    position_ = 0;
    newest_ = 0;
    std::fill(input_.begin(), input_.end(), std::complex<float>{});
    std::fill(spectra_.begin(), spectra_.end(), std::complex<float>{});
    std::fill(output_.begin(), output_.end(), std::complex<float>{});
}

bool PartitionedConvolver::fits(const FirKernel& kernel) const noexcept
{
    return kernel.blockSize() == blockSize_ && kernel.partitions() <= partitions_;
}

void PartitionedConvolver::process(const float* left, const float* right, float* outLeft, float* outRight, int length,
                                   const FirKernel& kernel, const FirKernel* fadeFrom) noexcept ESCEQ_NONBLOCKING
{
    length = std::clamp(length, 0, samplesUntilBlock());
    std::complex<float>* const in = input_.data() + blockSize_ + position_;
    const std::complex<float>* const out = output_.data() + position_;
    for (int i = 0; i < length; ++i)
    {
        // Se lee la entrada antes de escribir la salida: pueden ser la misma memoria.
        in[i] = {left[i], right[i]};
        outLeft[i] = out[i].real();
        if (outRight != nullptr)
            outRight[i] = out[i].imag();
    }

    position_ += length;
    if (position_ == blockSize_)
    {
        computeBlock(kernel, fadeFrom);
        position_ = 0;
    }
}

void PartitionedConvolver::computeBlock(const FirKernel& kernel, const FirKernel* fadeFrom) noexcept
{
    // FFT de los dos últimos bloques de entrada (solapamiento y descarte).
    newest_ = (newest_ + 1) % partitions_;
    std::complex<float>* const spectrum = spectra_.data() + static_cast<std::size_t>(newest_ * fftSize_);
    std::copy(input_.begin(), input_.end(), spectrum);
    fft_.forward(spectrum);
    std::copy(input_.begin() + blockSize_, input_.end(), input_.begin());

    if (!fits(kernel) || (fadeFrom != nullptr && !fits(*fadeFrom)))
    {
        std::fill(output_.begin(), output_.end(), std::complex<float>{});
        return;
    }

    std::fill(work_.begin(), work_.end(), std::complex<float>{});
    accumulate(kernel, work_.data());
    fft_.inverse(work_.data());
    // La primera mitad tiene el solapamiento circular: se descarta.
    std::copy(work_.begin() + blockSize_, work_.end(), output_.begin());

    if (fadeFrom == nullptr)
        return;

    std::fill(fadeWork_.begin(), fadeWork_.end(), std::complex<float>{});
    accumulate(*fadeFrom, fadeWork_.data());
    fft_.inverse(fadeWork_.data());
    const float step = 1.0f / static_cast<float>(blockSize_);
    for (int i = 0; i < blockSize_; ++i)
    {
        const std::complex<float> before = fadeWork_[static_cast<std::size_t>(blockSize_ + i)];
        const std::complex<float> after = output_[static_cast<std::size_t>(i)];
        const float gain = static_cast<float>(i + 1) * step;
        output_[static_cast<std::size_t>(i)] = {before.real() + gain * (after.real() - before.real()),
                                                before.imag() + gain * (after.imag() - before.imag())};
    }
}

void PartitionedConvolver::accumulate(const FirKernel& kernel, std::complex<float>* accumulator) const noexcept
{
    // Aritmética real explícita: el producto de std::complex comprueba los
    // NaN en cada operación y no se vectoriza.
    auto* acc = reinterpret_cast<float*>(accumulator);
    const int mask = fftSize_ - 1;
    for (int p = 0; p < kernel.partitions(); ++p)
    {
        const int slot = (newest_ - p + partitions_) % partitions_;
        const auto* x = reinterpret_cast<const float*>(spectra_.data() + static_cast<std::size_t>(slot * fftSize_));
        const auto* h = reinterpret_cast<const float*>(kernel.direct(p));
        for (int k = 0; k < fftSize_; ++k)
        {
            const float xr = x[2 * k], xi = x[2 * k + 1];
            const float hr = h[2 * k], hi = h[2 * k + 1];
            acc[2 * k] += hr * xr - hi * xi;
            acc[2 * k + 1] += hr * xi + hi * xr;
        }

        const auto* m = reinterpret_cast<const float*>(kernel.mirrored(p));
        if (m == nullptr)
            continue;
        // Espectro de conj(z): conj(Z[N − k]).
        for (int k = 0; k < fftSize_; ++k)
        {
            const int j = (fftSize_ - k) & mask;
            const float xr = x[2 * j], xi = -x[2 * j + 1];
            const float hr = m[2 * k], hi = m[2 * k + 1];
            acc[2 * k] += hr * xr - hi * xi;
            acc[2 * k + 1] += hr * xi + hi * xr;
        }
    }
}

} // namespace esceq::dsp
