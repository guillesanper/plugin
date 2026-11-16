// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

/// Utilidades para medir sonoridad en los tests: ruido rosa y sonoridad
/// integrada de ITU-R BS.1770 a 48 kHz. Son independientes de
/// dsp/auto_gain.h a propósito: es lo que comprueba que la compensación
/// calculada deja la sonoridad medida igual.
namespace esceq::test {

/// Ruido rosa de amplitud moderada (filtro de Paul Kellet, "refined":
/// ±0,05 dB de una pendiente de −3 dB/octava por encima de 10 Hz), a partir
/// de ruido blanco uniforme con la semilla `seed`.
inline std::vector<float> pinkNoise(std::size_t numSamples, std::uint32_t seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> white(-1.0, 1.0);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    std::vector<float> out(numSamples);
    for (auto& sample : out)
    {
        const double w = white(rng);
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        sample = static_cast<float>(0.05 * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362));
        b6 = w * 0.115926;
    }
    return out;
}

/// Sonoridad integrada, en LUFS, de los canales de `channels` (todos con
/// peso 1, como L y R), a 48 kHz, según ITU-R BS.1770-5: ponderación K,
/// bloques de 400 ms con un 75 % de solapamiento, umbral absoluto de
/// −70 LUFS y relativo de −10 LU.
inline double integratedLoudness(const std::vector<std::vector<float>>& channels)
{
    struct Biquad
    {
        double b0, b1, b2, a1, a2;
        double z1 = 0, z2 = 0;
        double operator()(double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    const std::size_t length = channels.front().size();
    constexpr std::size_t kBlock = 19200; // 400 ms
    constexpr std::size_t kHop = 4800;    // 75 % de solapamiento
    std::vector<double> blockPower(length >= kBlock ? (length - kBlock) / kHop + 1 : 0, 0.0);

    for (const auto& channel : channels)
    {
        Biquad shelf{1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585};
        Biquad highPass{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
        std::vector<double> squared(length);
        for (std::size_t i = 0; i < length; ++i)
        {
            const double y = highPass(shelf(static_cast<double>(channel[i])));
            squared[i] = y * y;
        }
        for (std::size_t j = 0; j < blockPower.size(); ++j)
        {
            const auto begin = squared.begin() + static_cast<std::ptrdiff_t>(j * kHop);
            blockPower[j] +=
                std::accumulate(begin, begin + static_cast<std::ptrdiff_t>(kBlock), 0.0) / static_cast<double>(kBlock);
        }
    }

    auto loudness = [](double power) { return -0.691 + 10.0 * std::log10(power); };
    auto gatedMean = [&](double threshold)
    {
        double sum = 0.0;
        int count = 0;
        for (const double p : blockPower)
            if (loudness(p) > threshold)
            {
                sum += p;
                ++count;
            }
        return count > 0 ? sum / count : 0.0;
    };
    const double relative = loudness(gatedMean(-70.0)) - 10.0;
    return loudness(gatedMean(std::max(relative, -70.0)));
}

} // namespace esceq::test
