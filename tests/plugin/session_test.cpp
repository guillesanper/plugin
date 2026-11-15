// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Espectros compartidos entre instancias: lo que pone el procesador en la
// sesión (audio de salida, nombre y color de la pista) y su ciclo de vida.
#include "test_support.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace esceq;
using namespace esceq::test;

namespace
{

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

std::unique_ptr<PluginProcessor> makeIn(const std::shared_ptr<core::SpectrumSession>& session)
{
    auto p = std::make_unique<PluginProcessor>(session);
    p->setRateAndBufferSizeDetails(kRate, kBlock);
    p->prepareToPlay(kRate, kBlock);
    return p;
}

/// Procesa `blocks` bloques estéreo de un seno de `hz` con amplitud 0,5.
/// `clock` lleva la cuenta de muestras, para que la fase siga entre llamadas.
void play(PluginProcessor& p, double hz, int blocks, long& clock)
{
    juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), kBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        for (int i = 0; i < kBlock; ++i)
        {
            const auto n = static_cast<double>(clock + i);
            const auto s = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * n / kRate));
            for (int ch = 0; ch < p.getMainBusNumInputChannels(); ++ch)
                buffer.setSample(ch, i, s);
        }
        p.processBlock(buffer, midi);
        clock += kBlock;
    }
}

const core::InstanceSpectrum& entryOf(const std::vector<core::InstanceSpectrum>& all, const PluginProcessor& p)
{
    const auto it = std::find_if(all.begin(), all.end(), [&](const auto& s) { return s.id == p.instanceId(); });
    REQUIRE(it != all.end());
    return *it;
}

float levelAt(const core::InstanceSpectrum& s, double hz)
{
    const auto& f = core::sessionFrequencies();
    const auto i = static_cast<std::size_t>(std::lower_bound(f.begin(), f.end(), hz) - f.begin());
    float best = core::kSessionFloorDb;
    for (std::size_t k = i > 2 ? i - 2 : 0; k < std::min(i + 3, f.size()); ++k)
        best = std::max(best, s.levelsDb[k]);
    return best;
}

/// Toca las dos instancias durante `seconds` y actualiza la sesión como lo haría su hilo.
void playBoth(core::SpectrumSession& session, PluginProcessor& a, double hzA, PluginProcessor& b, double hzB,
              double seconds)
{
    const int steps = static_cast<int>(seconds / core::SpectrumSession::kUpdateSeconds);
    const int blocksPerStep = static_cast<int>(kRate * core::SpectrumSession::kUpdateSeconds / kBlock) + 1;
    long clockA = 0;
    long clockB = 0;
    for (int i = 0; i < steps; ++i)
    {
        play(a, hzA, blocksPerStep, clockA);
        play(b, hzB, blocksPerStep, clockB);
        session.update(core::SpectrumSession::kUpdateSeconds);
    }
}

} // namespace

TEST_CASE("Sesion del plugin: las instancias del proceso comparten sesion")
{
    auto a = makeProcessor();
    auto b = makeProcessor();
    CHECK(&a->spectrumSession() == &b->spectrumSession());
    CHECK(a->instanceId() != b->instanceId());

    std::vector<core::InstanceSpectrum> all;
    a->spectrumSession().snapshot(all);
    CHECK(all.size() >= 2);

    const auto size = a->spectrumSession().size();
    b.reset();
    CHECK(a->spectrumSession().size() == size - 1);
}

TEST_CASE("Sesion del plugin: nombre y color de la pista del host")
{
    auto session = core::SpectrumSession::create(core::SpectrumSession::Threading::Manual);
    auto p = makeIn(session);
    std::vector<core::InstanceSpectrum> all;

    juce::AudioProcessor::TrackProperties track;
    track.name = juce::String("  Lead Vox ");
    track.colourARGB = 0xff3366ccu;
    p->updateTrackProperties(track);
    session->snapshot(all);
    CHECK(entryOf(all, *p).displayName() == "Lead Vox");
    CHECK(entryOf(all, *p).trackColour == 0xff3366ccu);

    // El host deja de decirlo: vuelve al número.
    p->updateTrackProperties({});
    session->snapshot(all);
    CHECK(entryOf(all, *p).displayName() == "esceQ 1");
    CHECK_FALSE(entryOf(all, *p).trackColour.has_value());
}

TEST_CASE("Sesion del plugin: se publica la salida de cada instancia")
{
    auto session = core::SpectrumSession::create(core::SpectrumSession::Threading::Manual);
    auto bass = makeIn(session);
    auto keys = makeIn(session);
    std::vector<core::InstanceSpectrum> all;

    SECTION("sin nadie mirando no se publica nada")
    {
        playBoth(*session, *bass, 100.0, *keys, 2000.0, 0.5);
        session->snapshot(all);
        CHECK_FALSE(entryOf(all, *bass).hasSignal);
        CHECK(levelAt(entryOf(all, *bass), 100.0) == core::kSessionFloorDb);
    }

    SECTION("mirando, cada una aparece con su espectro")
    {
        auto viewer = session->watch();
        playBoth(*session, *bass, 100.0, *keys, 2000.0, 2.0);
        session->snapshot(all);
        const auto& b = entryOf(all, *bass);
        const auto& k = entryOf(all, *keys);
        CHECK(b.hasSignal);
        CHECK(k.hasSignal);
        CHECK(levelAt(b, 100.0) > levelAt(b, 2000.0) + 40.0f);
        CHECK(levelAt(k, 2000.0) > levelAt(k, 100.0) + 40.0f);
    }

    SECTION("es la salida: incluye el ecualizador y la ganancia de salida")
    {
        auto viewer = session->watch();
        drag(*keys, core::kOutputGain, {-12.0f});
        playBoth(*session, *bass, 1000.0, *keys, 1000.0, 2.0);
        session->snapshot(all);
        const float difference = levelAt(entryOf(all, *bass), 1000.0) - levelAt(entryOf(all, *keys), 1000.0);
        CHECK(difference == Catch::Approx(12.0f).margin(1.0f));
    }

    SECTION("con bypass se publica la senal sin procesar")
    {
        auto viewer = session->watch();
        drag(*keys, core::kOutputGain, {-12.0f});
        drag(*keys, core::kBypass, {1.0f});
        playBoth(*session, *bass, 1000.0, *keys, 1000.0, 2.0);
        session->snapshot(all);
        const float difference = levelAt(entryOf(all, *bass), 1000.0) - levelAt(entryOf(all, *keys), 1000.0);
        CHECK(difference == Catch::Approx(0.0f).margin(1.0f));
    }
}

TEST_CASE("Sesion del plugin: la seleccion de comparacion vive en el procesador")
{
    auto session = core::SpectrumSession::create(core::SpectrumSession::Threading::Manual);
    auto a = makeIn(session);
    auto b = makeIn(session);

    a->compareSelection().toggle(b->instanceId());
    CHECK(a->compareSelection().contains(b->instanceId()));
    CHECK(b->compareSelection().shown.empty());

    // No forma parte del estado guardado.
    juce::MemoryBlock state;
    a->getStateInformation(state);
    CHECK_FALSE(state.toString().contains("compare"));
}
