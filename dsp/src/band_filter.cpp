// SPDX-License-Identifier: MIT
#include "esceq/dsp/band_filter.h"

#include <algorithm>
#include <cmath>

namespace esceq::dsp {

void BandFilter::setDesign(const BandDesign& design) noexcept ESCEQ_NONBLOCKING
{
    const int previous = design_.numSections;
    // Solo se copian las secciones en uso: las demás no se leen nunca. Con la
    // dinámica, setDesign() se llama hasta una vez por muestra.
    design_.numSections = design.numSections;
    std::copy_n(design.sections.begin(), design.numSections, design_.sections.begin());

    for (auto& channel : state_)
    {
        for (int i = 0; i < kMaxSections; ++i)
        {
            const bool isNew = i >= previous;
            const bool unused = i >= design_.numSections;
            if (isNew || unused)
                channel[static_cast<std::size_t>(i)] = {};
        }
    }
}

void BandFilter::reset() noexcept ESCEQ_NONBLOCKING
{
    state_ = {};
}

bool BandFilter::isFinite() const noexcept
{
    for (const auto& channel : state_)
        for (int i = 0; i < design_.numSections; ++i)
        {
            const SectionState& state = channel[static_cast<std::size_t>(i)];
            if (!std::isfinite(state.ic1) || !std::isfinite(state.ic2))
                return false;
        }
    return true;
}

namespace {

// Los estados se copian a variables locales durante el bucle: así el
// compilador los deja en registros en vez de leerlos y escribirlos en
// memoria en cada muestra.

/// Una sección de primer orden sobre un canal.
void firstOrder(const SectionCoeffs& c, double& state1, float* samples, int numSamples) noexcept
{
    double ic1 = state1;
    for (int i = 0; i < numSamples; ++i)
    {
        const double x = samples[i];
        const double v = (x - ic1) * c.a1;
        const double lp = v + ic1;
        ic1 = lp + v;
        samples[i] = static_cast<float>(c.m0 * x + c.m2 * lp);
    }
    state1 = ic1;
}

/// Una sección de segundo orden sobre un canal.
void secondOrder(const SectionCoeffs& c, double& state1, double& state2, float* samples, int numSamples) noexcept
{
    double ic1 = state1;
    double ic2 = state2;
    for (int i = 0; i < numSamples; ++i)
    {
        const double x = samples[i];
        const double v3 = x - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        samples[i] = static_cast<float>(c.m0 * x + c.m1 * v1 + c.m2 * v2);
    }
    state1 = ic1;
    state2 = ic2;
}

/// Lo mismo en dos canales a la vez. Cada canal hace exactamente las mismas
/// operaciones que en una pasada por canal, así que el resultado es idéntico
/// bit a bit; pero las dos recurrencias son independientes y el procesador
/// las solapa: cuesta bastante menos que dos pasadas (ver docs/dsp/filtros.md).
void firstOrderStereo(const SectionCoeffs& c, double& stateL, double& stateR, float* left, float* right,
                      int numSamples) noexcept
{
    double l1 = stateL;
    double r1 = stateR;
    for (int i = 0; i < numSamples; ++i)
    {
        const double xl = left[i];
        const double xr = right[i];
        const double vl = (xl - l1) * c.a1;
        const double vr = (xr - r1) * c.a1;
        const double lpl = vl + l1;
        const double lpr = vr + r1;
        l1 = lpl + vl;
        r1 = lpr + vr;
        left[i] = static_cast<float>(c.m0 * xl + c.m2 * lpl);
        right[i] = static_cast<float>(c.m0 * xr + c.m2 * lpr);
    }
    stateL = l1;
    stateR = r1;
}

void secondOrderStereo(const SectionCoeffs& c, double& stateL1, double& stateL2, double& stateR1, double& stateR2,
                       float* left, float* right, int numSamples) noexcept
{
    double l1 = stateL1;
    double l2 = stateL2;
    double r1 = stateR1;
    double r2 = stateR2;
    for (int i = 0; i < numSamples; ++i)
    {
        const double xl = left[i];
        const double xr = right[i];
        const double v3l = xl - l2;
        const double v3r = xr - r2;
        const double v1l = c.a1 * l1 + c.a2 * v3l;
        const double v1r = c.a1 * r1 + c.a2 * v3r;
        const double v2l = l2 + c.a2 * l1 + c.a3 * v3l;
        const double v2r = r2 + c.a2 * r1 + c.a3 * v3r;
        l1 = 2.0 * v1l - l1;
        r1 = 2.0 * v1r - r1;
        l2 = 2.0 * v2l - l2;
        r2 = 2.0 * v2r - r2;
        left[i] = static_cast<float>(c.m0 * xl + c.m1 * v1l + c.m2 * v2l);
        right[i] = static_cast<float>(c.m0 * xr + c.m1 * v1r + c.m2 * v2r);
    }
    stateL1 = l1;
    stateL2 = l2;
    stateR1 = r1;
    stateR2 = r2;
}

} // namespace

void BandFilter::process(float* const* channels, int numChannels, int numSamples) noexcept ESCEQ_NONBLOCKING
{
    const int usedChannels = std::min(numChannels, kMaxChannels);

    for (int s = 0; s < design_.numSections; ++s)
    {
        const auto section = static_cast<std::size_t>(s);
        const SectionCoeffs& c = design_.sections[section];

        if (usedChannels == 2)
        {
            SectionState& left = state_[0][section];
            SectionState& right = state_[1][section];
            if (c.order == 1)
                firstOrderStereo(c, left.ic1, right.ic1, channels[0], channels[1], numSamples);
            else
                secondOrderStereo(c, left.ic1, left.ic2, right.ic1, right.ic2, channels[0], channels[1], numSamples);
            continue;
        }

        for (int ch = 0; ch < usedChannels; ++ch)
        {
            SectionState& state = state_[static_cast<std::size_t>(ch)][section];
            if (c.order == 1)
                firstOrder(c, state.ic1, channels[ch], numSamples);
            else
                secondOrder(c, state.ic1, state.ic2, channels[ch], numSamples);
        }
    }
}

} // namespace esceq::dsp
