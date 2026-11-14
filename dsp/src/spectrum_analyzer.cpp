// SPDX-License-Identifier: MIT
#include "esceq/dsp/spectrum_analyzer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace esceq::dsp {

SpectrumAnalyzer::SpectrumAnalyzer(int fftOrder)
    : fft_(fftOrder)
{
    const auto n = static_cast<std::size_t>(fft_.size());
    history_.assign(n, 0.0f);
    buffer_.resize(n);
    binDb_.assign(n / 2 + 1, kFloorDb);

    window_.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        window_[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n)));
}

void SpectrumAnalyzer::setSampleRate(double sampleRate) noexcept
{
    if (sampleRate > 0.0)
        sampleRate_ = sampleRate;
}

void SpectrumAnalyzer::push(const float* samples, int numSamples) noexcept
{
    const std::size_t n = history_.size();
    for (int i = 0; i < numSamples; ++i)
    {
        // Una muestra no finita estropearía el espectro (y el suavizado de la
        // interfaz) para siempre: se trata como silencio.
        history_[writePos_] = std::isfinite(samples[i]) ? samples[i] : 0.0f;
        writePos_ = (writePos_ + 1) % n;
    }
}

void SpectrumAnalyzer::clear() noexcept
{
    std::fill(history_.begin(), history_.end(), 0.0f);
}

void SpectrumAnalyzer::compute(std::span<const double> frequencies, std::span<float> levelsDb)
{
    const std::size_t n = history_.size();
    for (std::size_t i = 0; i < n; ++i)
        buffer_[i] = {history_[(writePos_ + i) % n] * window_[i], 0.0f};

    fft_.forward(buffer_.data());

    // Hann: un seno de amplitud A da |X| = A·N/4 en su bin.
    const double scale = 4.0 / static_cast<double>(n);
    for (std::size_t k = 0; k < binDb_.size(); ++k)
    {
        const double magnitude = std::abs(buffer_[k]) * scale;
        binDb_[k] = static_cast<float>(20.0 * std::log10(std::max(magnitude, 1.0e-12)));
    }

    const double binHz = sampleRate_ / static_cast<double>(n);
    const double lastBin = static_cast<double>(binDb_.size() - 1);
    const double halfBand = std::pow(2.0, 1.0 / 24.0);

    const std::size_t count = std::min(frequencies.size(), levelsDb.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        const double f = std::max(frequencies[i], 1.0);
        const double lo = std::clamp(std::ceil(f / halfBand / binHz), 1.0, lastBin);
        const double hi = std::clamp(std::floor(f * halfBand / binHz), 1.0, lastBin);

        double level = 0.0;
        if (lo <= hi)
        {
            level = binDb_[static_cast<std::size_t>(lo)];
            for (auto k = static_cast<std::size_t>(lo) + 1; k <= static_cast<std::size_t>(hi); ++k)
                level = std::max(level, static_cast<double>(binDb_[k]));
        }
        else
        {
            const double position = std::clamp(f / binHz, 1.0, lastBin);
            const auto below = static_cast<std::size_t>(std::floor(position));
            const auto above = std::min(below + 1, binDb_.size() - 1);
            const double t = position - static_cast<double>(below);
            level = binDb_[below] + t * (binDb_[above] - binDb_[below]);
        }

        level += tilt_ * std::log2(f / 1000.0);
        levelsDb[i] = std::max(static_cast<float>(level), kFloorDb);
    }
}

} // namespace esceq::dsp
