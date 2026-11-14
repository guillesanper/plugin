// SPDX-License-Identifier: AGPL-3.0-or-later
//
// processBlock tal y como lo llama el host: buses, sidechain, mono y bypass.
#include "test_support.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace esceq;
using namespace esceq::test;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

/// Banda 1: campana dinámica a 1 kHz que escucha el sidechain externo.
void setupExternalBand(PluginProcessor& p)
{
    using namespace core;
    drag(p, bandParameter(0, BandParam::Frequency), {1000.0f});
    drag(p, bandParameter(0, BandParam::Q), {2.0f});
    drag(p, dynamicParameter(0, DynParam::Enabled), {1.0f});
    drag(p, dynamicParameter(0, DynParam::Threshold), {-30.0f});
    drag(p, dynamicParameter(0, DynParam::Range), {-12.0f});
    drag(p, dynamicParameter(0, DynParam::Knee), {0.0f});
    drag(p, dynamicParameter(0, DynParam::Source), {1.0f});
    drag(p, bandParameter(0, BandParam::Enabled), {1.0f});
}

bool setLayout(PluginProcessor& p, const juce::AudioChannelSet& main, const juce::AudioChannelSet& sidechain)
{
    auto layout = p.getBusesLayout();
    layout.inputBuses.getReference(0) = main;
    layout.outputBuses.getReference(0) = main;
    layout.inputBuses.getReference(1) = sidechain;
    return p.setBusesLayout(layout);
}

/// Procesa `blocks` bloques de un seno de 1 kHz: `mainLevel` en el bus
/// principal y `sideLevel` en el sidechain (si está conectado).
juce::AudioBuffer<float> run(PluginProcessor& p, int blocks, float mainLevel, float sideLevel)
{
    p.setRateAndBufferSizeDetails(kRate, kBlock);
    p.prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), kBlock);
    juce::MidiBuffer midi;
    const int mainChannels = p.getMainBusNumInputChannels();
    long n = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i, ++n)
        {
            const auto s = static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(n) / kRate));
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, (ch < mainChannels ? mainLevel : sideLevel) * s);
        }
        p.processBlock(buffer, midi);
    }
    return buffer;
}

} // namespace

TEST_CASE("processBlock: el sidechain externo controla la banda dinamica")
{
    auto p = makeProcessor();
    REQUIRE(setLayout(*p, juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo()));
    setupExternalBand(*p);

    run(*p, 200, 0.0005f, 0.5f); // principal casi en silencio, sidechain fuerte
    CHECK(p->sidechainConnected());
    CHECK(p->dynamicGainDb(0) == Catch::Approx(-12.0f).margin(0.01));
}

TEST_CASE("processBlock: con el sidechain en silencio la banda no actua aunque el principal suene")
{
    auto p = makeProcessor();
    REQUIRE(setLayout(*p, juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo()));
    setupExternalBand(*p);

    run(*p, 200, 0.5f, 0.0f);
    CHECK(p->dynamicGainDb(0) == 0.0f);
}

TEST_CASE("processBlock: sidechain mono")
{
    auto p = makeProcessor();
    REQUIRE(setLayout(*p, juce::AudioChannelSet::stereo(), juce::AudioChannelSet::mono()));
    setupExternalBand(*p);

    run(*p, 200, 0.0005f, 0.5f);
    CHECK(p->sidechainConnected());
    CHECK(p->dynamicGainDb(0) == Catch::Approx(-12.0f).margin(0.01));
}

TEST_CASE("processBlock: sin sidechain conectado la deteccion externa no actua")
{
    auto p = makeProcessor();
    REQUIRE(setLayout(*p, juce::AudioChannelSet::stereo(), juce::AudioChannelSet::disabled()));
    setupExternalBand(*p);

    run(*p, 200, 0.5f, 0.0f);
    CHECK_FALSE(p->sidechainConnected());
    CHECK(p->dynamicGainDb(0) == 0.0f);
}

TEST_CASE("processBlock: mono procesa el unico canal")
{
    auto p = makeProcessor();
    REQUIRE(setLayout(*p, juce::AudioChannelSet::mono(), juce::AudioChannelSet::disabled()));
    drag(*p, core::bandParameter(0, core::BandParam::Frequency), {1000.0f});
    drag(*p, core::bandParameter(0, core::BandParam::Gain), {-12.0f});
    drag(*p, core::bandParameter(0, core::BandParam::Enabled), {1.0f});

    const auto out = run(*p, 40, 0.5f, 0.0f);
    float peak = 0.0f;
    for (int i = 0; i < kBlock; ++i)
        peak = std::max(peak, std::abs(out.getSample(0, i)));
    CHECK(peak == Catch::Approx(0.5f * std::pow(10.0f, -12.0f / 20.0f)).margin(0.005));
    CHECK(p->outputChannelCount() == 1);
}

TEST_CASE("processBlock: con bypass la salida es la entrada")
{
    auto p = makeProcessor();
    drag(*p, core::bandParameter(0, core::BandParam::Gain), {9.0f});
    drag(*p, core::bandParameter(0, core::BandParam::Enabled), {1.0f});
    drag(*p, core::kOutputGain, {-6.0f});
    drag(*p, core::kBypass, {1.0f});

    p->setRateAndBufferSizeDetails(kRate, kBlock);
    p->prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> buffer(p->getTotalNumInputChannels(), kBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < 20; ++b)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample(ch, i, ch < 2 ? 0.25f * std::sin(0.05f * static_cast<float>(b * kBlock + i)) : 0.0f);
        juce::AudioBuffer<float> input;
        input.makeCopyOf(buffer);
        p->processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                REQUIRE(buffer.getSample(ch, i) == input.getSample(ch, i));
    }
}

TEST_CASE("processBlock: un parametro que cambia con el audio en marcha se aplica")
{
    auto p = makeProcessor();
    drag(*p, core::bandParameter(0, core::BandParam::Frequency), {1000.0f});
    drag(*p, core::bandParameter(0, core::BandParam::Enabled), {1.0f});
    auto peakOf = [](const juce::AudioBuffer<float>& b) {
        float peak = 0.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
            peak = std::max(peak, std::abs(b.getSample(0, i)));
        return peak;
    };

    CHECK(peakOf(run(*p, 40, 0.5f, 0.0f)) == Catch::Approx(0.5f).margin(0.005));

    // run() vuelve a llamar a prepareToPlay; aquí se procesa sin hacerlo.
    drag(*p, core::bandParameter(0, core::BandParam::Gain), {-12.0f});
    juce::AudioBuffer<float> buffer(p->getTotalNumInputChannels(), kBlock);
    juce::MidiBuffer midi;
    long n = 0;
    for (int b = 0; b < 40; ++b)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample(ch, i, ch < 2 ? 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 *
                                                                                    static_cast<double>(n + i) / kRate))
                                                : 0.0f);
        n += kBlock;
        p->processBlock(buffer, midi);
    }
    CHECK(peakOf(buffer) == Catch::Approx(0.5f * std::pow(10.0f, -12.0f / 20.0f)).margin(0.005));
}
