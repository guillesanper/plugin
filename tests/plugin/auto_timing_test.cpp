// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Tiempos automáticos de la dinámica vistos desde el host: el interruptor
// de cada banda llega al audio en los cuatro modos de fase (ADR 0016).
#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace esceq;
using namespace esceq::test;

namespace
{

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

/// Ráfagas de 7 kHz de 50 ms, cada 150 ms, sobre un tono grave.
std::vector<float> sibilantVoice()
{
    std::vector<float> out(48000);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const double t = static_cast<double>(i) / kRate;
        const double hiss = (i % 7200) < 2400 ? 0.4 * std::sin(2.0 * std::numbers::pi * 7000.0 * t) : 0.0;
        out[i] = static_cast<float>(0.2 * std::sin(2.0 * std::numbers::pi * 200.0 * t) + hiss);
    }
    return out;
}

/// Procesador con un de-esser en la banda 1: los tiempos fijos, lentos.
std::unique_ptr<PluginProcessor> deEsser(float phaseMode, bool autoTiming, float attackMs)
{
    using core::BandParam;
    using core::DynParam;
    auto p = makeProcessor();
    p->setNonRealtime(true); // en fase lineal, los FIR se diseñan en su bloque
    drag(*p, core::bandParameter(0, BandParam::Enabled), {1.0f});
    drag(*p, core::bandParameter(0, BandParam::Frequency), {7000.0f});
    drag(*p, core::bandParameter(0, BandParam::Q), {1.5f});
    drag(*p, core::dynamicParameter(0, DynParam::Enabled), {1.0f});
    drag(*p, core::dynamicParameter(0, DynParam::Threshold), {-30.0f});
    drag(*p, core::dynamicParameter(0, DynParam::Range), {-12.0f});
    drag(*p, core::dynamicParameter(0, DynParam::Attack), {attackMs});
    drag(*p, core::dynamicParameter(0, DynParam::Release), {1000.0f});
    drag(*p, core::autoTimingParameter(0), {autoTiming ? 1.0f : 0.0f});
    drag(*p, core::kPhaseMode, {phaseMode});
    p->setRateAndBufferSizeDetails(kRate, kBlock);
    p->prepareToPlay(kRate, kBlock);
    return p;
}

std::vector<float> render(PluginProcessor& p, const std::vector<float>& input)
{
    juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), kBlock);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (std::size_t pos = 0; pos < input.size(); pos += kBlock)
    {
        buffer.clear();
        for (int i = 0; i < kBlock && pos + static_cast<std::size_t>(i) < input.size(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, i, input[pos + static_cast<std::size_t>(i)]);
        p.processBlock(buffer, midi);
        for (int i = 0; i < kBlock; ++i)
            out.push_back(buffer.getSample(0, i));
    }
    out.resize(input.size());
    return out;
}

} // namespace

TEST_CASE("Tiempos automaticos: el interruptor de la banda llega al audio en todos los modos de fase")
{
    const auto input = sibilantVoice();
    for (const float mode : {0.0f, 1.0f, 2.0f, 3.0f})
    {
        INFO("modo de fase " << mode);
        auto fixed = deEsser(mode, false, 50.0f);
        auto automatic = deEsser(mode, true, 50.0f);
        auto otherAttack = deEsser(mode, true, 0.1f);
        const auto fixedOut = render(*fixed, input);
        const auto autoOut = render(*automatic, input);

        // Encendidos, cambian el sonido, y el ataque fijo ya no manda.
        CHECK(autoOut != fixedOut);
        CHECK(render(*otherAttack, input) == autoOut);
    }
}
