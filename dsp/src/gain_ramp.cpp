// SPDX-License-Identifier: MIT
#include "esceq/dsp/gain_ramp.h"

#include <algorithm>
#include <cmath>

namespace esceq::dsp {

void GainRamp::prepare(double sampleRate, double rampSeconds) noexcept
{
    const double samples = std::round(sampleRate * rampSeconds);
    rampSamples_ = static_cast<int>(std::clamp(samples, 1.0, 1.0e9));
}

void GainRamp::reset(float gain) noexcept
{
    current_ = gain;
    target_ = gain;
    step_ = 0.0f;
    remaining_ = 0;
}

void GainRamp::setTarget(float gain) noexcept
{
    if (gain == target_)
        return;

    target_ = gain;
    remaining_ = rampSamples_;
    step_ = (target_ - current_) / static_cast<float>(remaining_);
}

void GainRamp::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    if (remaining_ == 0 && current_ == 1.0f)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        if (remaining_ > 0)
        {
            current_ += step_;
            if (--remaining_ == 0)
                current_ = target_;
        }

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= current_;
    }
}

} // namespace esceq::dsp
