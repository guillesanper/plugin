// SPDX-License-Identifier: MIT
#include "esceq/dsp/band_filter.h"

#include <algorithm>

namespace esceq::dsp {

void BandFilter::setDesign(const BandDesign& design) noexcept
{
    const int previous = design_.numSections;
    design_ = design;

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

void BandFilter::reset() noexcept
{
    state_ = {};
}

void BandFilter::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    const int usedChannels = std::min(numChannels, kMaxChannels);

    for (int ch = 0; ch < usedChannels; ++ch)
    {
        float* const samples = channels[ch];

        for (int s = 0; s < design_.numSections; ++s)
        {
            const SectionCoeffs& c = design_.sections[static_cast<std::size_t>(s)];
            SectionState& state = state_[static_cast<std::size_t>(ch)][static_cast<std::size_t>(s)];
            double ic1 = state.ic1;
            double ic2 = state.ic2;

            if (c.order == 1)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    const double x = samples[i];
                    const double v = (x - ic1) * c.a1;
                    const double lp = v + ic1;
                    ic1 = lp + v;
                    samples[i] = static_cast<float>(c.m0 * x + c.m2 * lp);
                }
            }
            else
            {
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
            }

            state.ic1 = ic1;
            state.ic2 = ic2;
        }
    }
}

} // namespace esceq::dsp
