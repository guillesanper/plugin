// SPDX-License-Identifier: MIT
#include "esceq/dsp/spectrum_collision.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace esceq::dsp
{

namespace
{

double dbToPower(double db) noexcept
{
    return std::pow(10.0, 0.1 * db);
}

double powerToDb(double power) noexcept
{
    return 10.0 * std::log10(std::max(power, 1.0e-30));
}

/// Interpolación suave (smoothstep) de 0 a 1, con la entrada ya en 0..1.
float smooth01(float t) noexcept
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void smoothOctaves(std::span<const double> frequencies, std::span<const float> levelsDb, double widthOctaves,
                   std::span<float> out) noexcept
{
    const std::size_t n = std::min({frequencies.size(), levelsDb.size(), out.size()});
    if (n == 0)
        return;
    if (widthOctaves <= 0.0 || n == 1)
    {
        std::copy_n(levelsDb.begin(), n, out.begin());
        return;
    }

    // Ventana deslizante con dos punteros: las frecuencias son crecientes, así
    // que los límites de la ventana solo avanzan. La suma se lleva en doble
    // precisión y en potencia.
    const double halfRatio = std::exp2(0.5 * widthOctaves);
    std::size_t lo = 0;
    std::size_t hi = 0; // primer punto fuera de la ventana por arriba
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double low = frequencies[i] / halfRatio;
        const double high = frequencies[i] * halfRatio;
        while (hi < n && frequencies[hi] <= high)
            sum += dbToPower(levelsDb[hi++]);
        while (lo < i && frequencies[lo] < low)
            sum -= dbToPower(levelsDb[lo++]);
        const auto count = static_cast<double>(hi - lo);
        out[i] = static_cast<float>(powerToDb(std::max(sum, 0.0) / count));
    }
}

float presence(float levelDb, float peakDb, const CollisionSettings& settings) noexcept
{
    const float threshold = std::max(settings.floorDb, peakDb - settings.relevanceRangeDb);
    const float ramp = std::max(settings.presenceRampDb, 1.0e-3f);
    return smooth01((levelDb - threshold) / ramp);
}

float maskingFactor(float otherMinusOwnDb, const CollisionSettings& settings) noexcept
{
    const float window = std::max(settings.maskingWindowDb, 1.0e-3f);
    return smooth01((otherMinusOwnDb + window) / window);
}

void collisionSeverity(std::span<const float> ownDb, std::span<const float> otherDb, std::span<float> severity,
                       const CollisionSettings& settings) noexcept
{
    assert(ownDb.size() == otherDb.size() && ownDb.size() == severity.size());
    const std::size_t n = std::min({ownDb.size(), otherDb.size(), severity.size()});
    if (n == 0)
        return;

    const float ownPeak = *std::max_element(ownDb.begin(), ownDb.begin() + static_cast<std::ptrdiff_t>(n));
    const float otherPeak = *std::max_element(otherDb.begin(), otherDb.begin() + static_cast<std::ptrdiff_t>(n));
    for (std::size_t i = 0; i < n; ++i)
    {
        const float own = ownDb[i];
        const float other = otherDb[i];
        severity[i] = presence(own, ownPeak, settings) * presence(other, otherPeak, settings) *
                      maskingFactor(other - own, settings);
    }
}

std::vector<CollisionRegion> findCollisionRegions(std::span<const double> frequencies, std::span<const float> severity,
                                                  float threshold, double mergeGapOctaves, double minWidthOctaves)
{
    const std::size_t n = std::min(frequencies.size(), severity.size());
    std::vector<CollisionRegion> regions;

    for (std::size_t i = 0; i < n;)
    {
        if (severity[i] < threshold)
        {
            ++i;
            continue;
        }
        std::size_t last = i;
        while (last + 1 < n && severity[last + 1] >= threshold)
            ++last;

        // Si el hueco con el tramo anterior es pequeño, se une a él.
        if (!regions.empty() && std::log2(frequencies[i] / frequencies[regions.back().last]) < mergeGapOctaves)
            regions.back().last = last;
        else
            regions.push_back({.first = i, .last = last});
        i = last + 1;
    }

    std::vector<CollisionRegion> result;
    result.reserve(regions.size());
    for (auto region : regions)
    {
        region.lowHz = frequencies[region.first];
        region.highHz = frequencies[region.last];
        if (std::log2(region.highHz / region.lowHz) < minWidthOctaves)
            continue;
        // El pico es el centro de los puntos con la severidad máxima: en una
        // meseta (severidad 1 en toda una zona) queda en medio, no en un borde.
        std::size_t firstPeak = region.first;
        std::size_t lastPeak = region.first;
        for (std::size_t k = region.first; k <= region.last; ++k)
        {
            if (severity[k] > region.peakSeverity)
            {
                region.peakSeverity = severity[k];
                firstPeak = lastPeak = k;
            }
            else if (severity[k] == region.peakSeverity)
                lastPeak = k;
        }
        region.peakHz = frequencies[firstPeak + (lastPeak - firstPeak) / 2];
        result.push_back(region);
    }

    std::stable_sort(result.begin(), result.end(), [](const CollisionRegion& a, const CollisionRegion& b)
                     { return a.peakSeverity > b.peakSeverity; });
    return result;
}

SpectrumAverager::SpectrumAverager(std::size_t points, double timeConstantSeconds, float floorDb)
    : levels_(points, floorDb), timeConstant_(std::max(timeConstantSeconds, 1.0e-6)), floorDb_(floorDb)
{
}

void SpectrumAverager::update(std::span<const float> levelsDb, double dtSeconds) noexcept
{
    const std::size_t n = std::min(levels_.size(), levelsDb.size());
    if (empty_)
    {
        for (std::size_t i = 0; i < n; ++i)
            levels_[i] = std::max(levelsDb[i], floorDb_);
        empty_ = false;
        return;
    }

    const double keep = std::exp(-std::max(dtSeconds, 0.0) / timeConstant_);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double power = keep * dbToPower(levels_[i]) + (1.0 - keep) * dbToPower(levelsDb[i]);
        levels_[i] = std::max(static_cast<float>(powerToDb(power)), floorDb_);
    }
}

void SpectrumAverager::reset() noexcept
{
    std::fill(levels_.begin(), levels_.end(), floorDb_);
    empty_ = true;
}

} // namespace esceq::dsp
