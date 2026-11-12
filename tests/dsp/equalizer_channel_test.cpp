// SPDX-License-Identifier: MIT
//
// Canal por banda (Stereo / Left / Right / Mid / Side). Ver docs/dsp/ecualizador.md.
#include "esceq/dsp/equalizer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace esceq::dsp;

namespace {

constexpr double kFs = 48000.0;

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;

    friend bool operator==(const Stereo&, const Stereo&) = default;
};

std::vector<float> noise(int numSamples, std::uint32_t seed)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples));
    std::uint32_t state = seed;
    for (auto& sample : out)
    {
        state = state * 1664525u + 1013904223u;
        sample = 0.5f * (static_cast<float>(state >> 8) / 8388608.0f - 1.0f);
    }
    return out;
}

std::vector<float> sine(int numSamples, double hz, double amplitude)
{
    std::vector<float> out(static_cast<std::size_t>(numSamples));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kFs));
    return out;
}

/// Dos canales sin relación entre sí.
Stereo stereoNoise(int numSamples = 8192)
{
    return {noise(numSamples, 1111u), noise(numSamples, 2222u)};
}

Stereo run(Equalizer& eq, Stereo signal, int blockSize = 512)
{
    const int total = static_cast<int>(signal.left.size());
    for (int pos = 0; pos < total; pos += blockSize)
    {
        float* channels[2] = {signal.left.data() + pos, signal.right.data() + pos};
        eq.process(channels, 2, std::min(blockSize, total - pos));
    }
    return signal;
}

std::vector<float> runMono(Equalizer& eq, std::vector<float> signal)
{
    float* channel = signal.data();
    eq.process(&channel, 1, static_cast<int>(signal.size()));
    return signal;
}

BandSettings bell(ChannelMode channel, double gainDb = 9.0)
{
    BandSettings settings{true, {FilterType::Bell, 1000.0, gainDb, 1.2}};
    settings.channel = channel;
    return settings;
}

Stereo process(const BandSettings& settings, const Stereo& input, int blockSize = 512)
{
    Equalizer eq;
    eq.setBand(0, settings);
    eq.prepare(kFs);
    return run(eq, input, blockSize);
}

float maxDifference(const std::vector<float>& a, const std::vector<float>& b)
{
    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, std::abs(a[i] - b[i]));
    return worst;
}

std::vector<float> mid(const Stereo& s)
{
    std::vector<float> out(s.left.size());
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = 0.5f * (s.left[i] + s.right[i]);
    return out;
}

std::vector<float> side(const Stereo& s)
{
    std::vector<float> out(s.left.size());
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = 0.5f * (s.left[i] - s.right[i]);
    return out;
}

float maxStep(const std::vector<float>& signal)
{
    float worst = 0.0f;
    for (std::size_t i = 1; i < signal.size(); ++i)
        worst = std::max(worst, std::abs(signal[i] - signal[i - 1]));
    return worst;
}

} // namespace

TEST_CASE("Canal: Stereo es el valor por defecto y filtra los dos canales por igual")
{
    CHECK(BandSettings{}.channel == ChannelMode::Stereo);

    const auto input = stereoNoise();
    const auto output = process(bell(ChannelMode::Stereo), input);

    // Cada canal suena como si se procesara solo: los canales son independientes.
    Equalizer mono;
    mono.setBand(0, bell(ChannelMode::Stereo));
    mono.prepare(kFs);
    CHECK(output.left == runMono(mono, input.left));
    mono.reset();
    CHECK(output.right == runMono(mono, input.right));
}

TEST_CASE("Canal: Left procesa solo el izquierdo y el derecho pasa bit a bit")
{
    const auto input = stereoNoise();
    const auto output = process(bell(ChannelMode::Left), input);
    const auto reference = process(bell(ChannelMode::Stereo), input);

    CHECK(output.left == reference.left);
    CHECK(output.right == input.right);
    CHECK(output.left != input.left);
}

TEST_CASE("Canal: Right procesa solo el derecho y el izquierdo pasa bit a bit")
{
    const auto input = stereoNoise();
    const auto output = process(bell(ChannelMode::Right), input);
    const auto reference = process(bell(ChannelMode::Stereo), input);

    CHECK(output.right == reference.right);
    CHECK(output.left == input.left);
    CHECK(output.right != input.right);
}

TEST_CASE("Canal: Mid filtra la suma y deja intacta la diferencia")
{
    const auto input = stereoNoise();
    const auto output = process(bell(ChannelMode::Mid), input);

    // La suma sale como si se hubiera filtrado sola...
    Equalizer mono;
    mono.setBand(0, bell(ChannelMode::Stereo));
    mono.prepare(kFs);
    CHECK(maxDifference(mid(output), runMono(mono, mid(input))) < 2.0e-6f);
    // ...y la diferencia no cambia.
    CHECK(maxDifference(side(output), side(input)) < 2.0e-7f);
    CHECK(maxDifference(mid(output), mid(input)) > 0.05f);
}

TEST_CASE("Canal: Side filtra la diferencia y deja intacta la suma")
{
    const auto input = stereoNoise();
    const auto output = process(bell(ChannelMode::Side), input);

    Equalizer mono;
    mono.setBand(0, bell(ChannelMode::Stereo));
    mono.prepare(kFs);
    CHECK(maxDifference(side(output), runMono(mono, side(input))) < 2.0e-6f);
    CHECK(maxDifference(mid(output), mid(input)) < 2.0e-7f);
    CHECK(maxDifference(side(output), side(input)) > 0.05f);
}

TEST_CASE("Canal: una senal sin componente lateral pasa bit a bit por una banda Side, y al reves")
{
    const auto base = noise(8192, 77u);
    std::vector<float> inverted(base.size());
    std::transform(base.begin(), base.end(), inverted.begin(), [](float x) { return -x; });

    const Stereo centred{base, base};      // solo suma
    const Stereo wide{base, inverted};     // solo diferencia

    CHECK(process(bell(ChannelMode::Side), centred) == centred);
    CHECK(process(bell(ChannelMode::Mid), wide) == wide);
    // Y la contraria si actua.
    CHECK(process(bell(ChannelMode::Mid), centred) != centred);
    CHECK(process(bell(ChannelMode::Side), wide) != wide);
}

TEST_CASE("Canal: nulo, Mid mas Side con los mismos ajustes equivale a Stereo")
{
    const auto input = stereoNoise();

    Equalizer eq;
    eq.setBand(0, bell(ChannelMode::Mid));
    eq.setBand(1, bell(ChannelMode::Side));
    eq.prepare(kFs);
    const auto output = run(eq, input);
    const auto reference = process(bell(ChannelMode::Stereo), input);

    CHECK(maxDifference(output.left, reference.left) < 5.0e-6f);
    CHECK(maxDifference(output.right, reference.right) < 5.0e-6f);
}

TEST_CASE("Canal: nulo, Left mas Right con los mismos ajustes es identico a Stereo")
{
    const auto input = stereoNoise();

    Equalizer eq;
    eq.setBand(0, bell(ChannelMode::Left));
    eq.setBand(1, bell(ChannelMode::Right));
    eq.prepare(kFs);
    CHECK(run(eq, input) == process(bell(ChannelMode::Stereo), input));
}

TEST_CASE("Canal: una banda que no hace nada pasa bit a bit en todos los modos")
{
    const auto input = stereoNoise();
    for (auto mode : {ChannelMode::Stereo, ChannelMode::Left, ChannelMode::Right, ChannelMode::Mid, ChannelMode::Side})
        CHECK(process(bell(mode, 0.0), input) == input);
}

TEST_CASE("Canal: con audio mono, Right y Side no actuan y el resto procesa el canal")
{
    const auto input = noise(4096, 5u);

    auto mono = [&](ChannelMode mode) {
        Equalizer eq;
        eq.setBand(0, bell(mode));
        eq.prepare(kFs);
        return runMono(eq, input);
    };

    const auto reference = mono(ChannelMode::Stereo);
    CHECK(reference != input);
    CHECK(mono(ChannelMode::Left) == reference);
    CHECK(mono(ChannelMode::Mid) == reference);
    CHECK(mono(ChannelMode::Right) == input);
    CHECK(mono(ChannelMode::Side) == input);
}

TEST_CASE("Canal: no depende del tamano de bloque")
{
    const auto input = stereoNoise(6000);
    for (auto mode : {ChannelMode::Left, ChannelMode::Right, ChannelMode::Mid, ChannelMode::Side})
    {
        auto settings = bell(mode);
        settings.dynamics.enabled = true;
        settings.dynamics.thresholdDb = -30.0;
        settings.dynamics.rangeDb = -12.0;
        const auto reference = process(settings, input, 6000);
        for (int blockSize : {1, 7, 32, 100})
            CHECK(process(settings, input, blockSize) == reference);
    }
}

TEST_CASE("Canal: la deteccion dinamica sigue al canal de la banda")
{
    // Tono fuerte en un solo canal, muy por encima del umbral.
    constexpr int kLength = 24000;
    const auto tone = sine(kLength, 1000.0, 0.5);
    const std::vector<float> silence(static_cast<std::size_t>(kLength), 0.0f);

    auto dynamicGain = [&](ChannelMode mode, const Stereo& input) {
        auto settings = bell(mode, 0.0);
        settings.dynamics.enabled = true;
        settings.dynamics.thresholdDb = -30.0;
        settings.dynamics.rangeDb = -12.0;
        settings.dynamics.kneeDb = 0.0;
        Equalizer eq;
        eq.setBand(0, settings);
        eq.prepare(kFs);
        run(eq, input);
        return eq.dynamicGainDb(0);
    };

    const Stereo onlyLeft{tone, silence};
    const Stereo onlyRight{silence, tone};
    std::vector<float> inverted(tone.size());
    std::transform(tone.begin(), tone.end(), inverted.begin(), [](float x) { return -x; });
    const Stereo centred{tone, tone};
    const Stereo wide{tone, inverted};

    // El canal de la banda manda...
    CHECK(dynamicGain(ChannelMode::Left, onlyLeft) < -11.0);
    CHECK(dynamicGain(ChannelMode::Right, onlyRight) < -11.0);
    CHECK(dynamicGain(ChannelMode::Mid, centred) < -11.0);
    CHECK(dynamicGain(ChannelMode::Side, wide) < -11.0);

    // ...y lo que ocurre en el otro no la mueve.
    CHECK(dynamicGain(ChannelMode::Left, onlyRight) == 0.0);
    CHECK(dynamicGain(ChannelMode::Right, onlyLeft) == 0.0);
    CHECK(dynamicGain(ChannelMode::Mid, wide) == 0.0);
    CHECK(dynamicGain(ChannelMode::Side, centred) == 0.0);

    // En Stereo la deteccion es enlazada: basta un canal.
    CHECK(dynamicGain(ChannelMode::Stereo, onlyLeft) < -11.0);
    CHECK(dynamicGain(ChannelMode::Stereo, onlyRight) < -11.0);
}

TEST_CASE("Canal: el sidechain externo tambien se escucha por el canal de la banda")
{
    constexpr int kLength = 24000;
    const auto tone = sine(kLength, 1000.0, 0.5);
    const std::vector<float> silence(static_cast<std::size_t>(kLength), 0.0f);

    auto dynamicGain = [&](ChannelMode mode, const std::vector<float>& scLeft, const std::vector<float>& scRight,
                           int scChannels) {
        auto settings = bell(mode, 0.0);
        settings.dynamics.enabled = true;
        settings.dynamics.external = true;
        settings.dynamics.thresholdDb = -30.0;
        settings.dynamics.rangeDb = -12.0;
        Equalizer eq;
        eq.setBand(0, settings);
        eq.prepare(kFs);
        Stereo audio{silence, silence};
        float* channels[2] = {audio.left.data(), audio.right.data()};
        const float* sidechain[2] = {scLeft.data(), scRight.data()};
        eq.process(channels, 2, kLength, sidechain, scChannels);
        return eq.dynamicGainDb(0);
    };

    CHECK(dynamicGain(ChannelMode::Right, silence, tone, 2) < -11.0);
    CHECK(dynamicGain(ChannelMode::Right, tone, silence, 2) == 0.0);
    // Un sidechain mono se escucha tal cual, sea cual sea el canal de la banda.
    CHECK(dynamicGain(ChannelMode::Right, tone, silence, 1) < -11.0);
    CHECK(dynamicGain(ChannelMode::Side, tone, silence, 1) < -11.0);
}

TEST_CASE("Canal: cambiar de canal hace un fundido sin saltos")
{
    // Corte de graves sobre un seno grave: al cambiar de canal, la senal de
    // un lado aparece y la del otro desaparece. Sin fundido, el salto es del
    // tamano de la senal.
    // 67 Hz: los cambios (cada 4800 muestras) no caen en cruces por cero.
    constexpr int kLength = 4800;
    const auto tone = sine(kLength * 6, 67.0, 0.9);
    Stereo signal{tone, tone};

    Equalizer eq;
    BandSettings settings{true, {FilterType::LowCut, 2000.0, 0.0, kButterworthQ, 24}};
    eq.setBand(0, settings);
    eq.prepare(kFs);

    const ChannelMode sequence[] = {ChannelMode::Left, ChannelMode::Side, ChannelMode::Right,
                                    ChannelMode::Mid,  ChannelMode::Stereo};
    int position = 0;
    auto advance = [&] {
        float* channels[2] = {signal.left.data() + position, signal.right.data() + position};
        eq.process(channels, 2, kLength);
        position += kLength;
    };

    advance();
    for (auto mode : sequence)
    {
        settings.channel = mode;
        eq.setBand(0, settings);
        advance();
    }

    // El seno limpio da pasos de 0,008. Con un fundido de 10 ms (480 muestras)
    // el paso extra es como mucho 0,9/480 = 0,002. Sin fundido, saltos de 0,5 o mas.
    CHECK(maxStep(signal.left) < 0.012f);
    CHECK(maxStep(signal.right) < 0.012f);
    CHECK(eq.isSettled());
}
