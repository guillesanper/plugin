// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/spectral_mask.h"

#include "esceq/dsp/phase_equalizer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace esceq::core
{

namespace
{

/// Razón entre las frecuencias de dos puntos vecinos de la rejilla.
double pointRatio() noexcept
{
    return std::pow(kSpectralMaskMaxHz / kSpectralMaskMinHz, 1.0 / (kSpectralMaskPoints - 1));
}

} // namespace

double spectralMaskPointHz(int point) noexcept
{
    return kSpectralMaskMinHz * std::pow(pointRatio(), point);
}

void reduceSpectralMask(std::span<const float> binGainsDb, double binHz, SpectralMaskPoints& out) noexcept
{
    out.fill(0.0f);
    const auto numBins = static_cast<int>(binGainsDb.size());
    if (numBins == 0 || !(binHz > 0.0))
        return;

    const double ratio = pointRatio();
    const double halfStep = std::sqrt(ratio);
    // Frecuencia del punto y borde inferior de su tramo; se avanzan
    // multiplicando (el error acumulado en 192 puntos es del orden de 1e-14).
    double hz = kSpectralMaskMinHz;
    double low = kSpectralMaskMinHz / halfStep;
    for (std::size_t point = 0; point < out.size(); ++point, hz *= ratio, low *= ratio)
    {
        const double high = low * ratio;
        // Bins en [low, high).
        const int first = static_cast<int>(std::ceil(low / binHz));
        const int last = std::min(numBins, static_cast<int>(std::ceil(high / binHz))) - 1;
        if (first <= last)
        {
            float extreme = 0.0f;
            for (int k = first; k <= last; ++k)
            {
                const float db = binGainsDb[static_cast<std::size_t>(k)];
                if (std::abs(db) > std::abs(extreme))
                    extreme = db;
            }
            out[point] = extreme;
            continue;
        }

        // Ningún bin en el tramo: entre los dos vecinos.
        const double position = hz / binHz;
        const int below = static_cast<int>(position);
        if (below + 1 >= numBins)
            continue; // por encima del último bin
        const auto fraction = static_cast<float>(position - below);
        const float a = binGainsDb[static_cast<std::size_t>(below)];
        const float b = binGainsDb[static_cast<std::size_t>(below + 1)];
        out[point] = a + fraction * (b - a);
    }
}

bool SpectralMaskFrame::anyActive() const noexcept
{
    return std::any_of(active.begin(), active.end(), [](bool a) { return a; });
}

void SpectralMaskPublisher::prepare(double sampleRate) noexcept
{
    interval_ = std::max(1, static_cast<int>(std::lround(sampleRate / kSpectralMaskRateHz)));
    elapsed_ = 0;
}

void SpectralMaskPublisher::publish(const dsp::PhaseEqualizer& equalizer, int numSamples) noexcept
{
    elapsed_ += std::max(0, numSamples);
    if (elapsed_ < interval_)
        return;
    elapsed_ %= interval_;

    const bool sounding = equalizer.spectralActive();
    if (!sounding && !publishedActive_)
        return; // nada que decir: ya se publicó que no hay máscaras

    auto& frame = buffer_.writeSlot();
    const auto& engine = equalizer.spectralDynamics();
    const double binHz = engine.binFrequencyHz(1);
    bool any = false;
    for (int band = 0; band < kNumBands; ++band)
    {
        const auto index = static_cast<std::size_t>(band);
        const auto gains = sounding ? engine.binGainsDb(band) : std::span<const float>{};
        frame.active[index] = !gains.empty();
        any = any || !gains.empty();
        reduceSpectralMask(gains, binHz, frame.gainsDb[index]);
    }
    if (!any && !publishedActive_)
        return;
    buffer_.publish();
    publishedActive_ = any;
}

} // namespace esceq::core
