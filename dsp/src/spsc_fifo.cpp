// SPDX-License-Identifier: MIT
#include "esceq/dsp/spsc_fifo.h"

#include <algorithm>

namespace esceq::dsp {

namespace {

std::size_t nextPowerOfTwo(int value)
{
    std::size_t size = 2;
    while (size < static_cast<std::size_t>(std::max(value, 2)))
        size *= 2;
    return size;
}

} // namespace

SpscFifo::SpscFifo(int capacity)
    : buffer_(nextPowerOfTwo(capacity), 0.0f),
      mask_(buffer_.size() - 1)
{
}

int SpscFifo::write(const float* samples, int numSamples) noexcept
{
    const std::size_t write = writePos_.load(std::memory_order_relaxed);
    const std::size_t read = readPos_.load(std::memory_order_acquire);
    const std::size_t space = buffer_.size() - (write - read);
    const std::size_t count = std::min(space, static_cast<std::size_t>(std::max(numSamples, 0)));

    for (std::size_t i = 0; i < count; ++i)
        buffer_[(write + i) & mask_] = samples[i];

    writePos_.store(write + count, std::memory_order_release);
    return static_cast<int>(count);
}

int SpscFifo::writeMix(const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (numChannels <= 0)
        return 0;

    const std::size_t write = writePos_.load(std::memory_order_relaxed);
    const std::size_t read = readPos_.load(std::memory_order_acquire);
    const std::size_t space = buffer_.size() - (write - read);
    const std::size_t count = std::min(space, static_cast<std::size_t>(std::max(numSamples, 0)));
    const float scale = 1.0f / static_cast<float>(numChannels);

    for (std::size_t i = 0; i < count; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            sum += channels[ch][i];
        buffer_[(write + i) & mask_] = sum * scale;
    }

    writePos_.store(write + count, std::memory_order_release);
    return static_cast<int>(count);
}

int SpscFifo::read(float* destination, int maxSamples) noexcept
{
    const std::size_t read = readPos_.load(std::memory_order_relaxed);
    const std::size_t write = writePos_.load(std::memory_order_acquire);
    const std::size_t count = std::min(write - read, static_cast<std::size_t>(std::max(maxSamples, 0)));

    for (std::size_t i = 0; i < count; ++i)
        destination[i] = buffer_[(read + i) & mask_];

    readPos_.store(read + count, std::memory_order_release);
    return static_cast<int>(count);
}

int SpscFifo::available() const noexcept
{
    const std::size_t write = writePos_.load(std::memory_order_acquire);
    const std::size_t read = readPos_.load(std::memory_order_acquire);
    return static_cast<int>(write - read);
}

} // namespace esceq::dsp
