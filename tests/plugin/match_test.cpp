// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Igualar a una referencia de extremo a extremo (PLAN.md 5.6): dos
// procesadores en una sesión de espectros con hilo manual, el aprendizaje de
// la interfaz (ui::MatchController) con lo que leen sus colas, el ajuste en
// el hilo del procesador (en manual) y Apply. Y que, sin aprender del
// sidechain, processBlock no cambia.
#include "test_support.h"

#include "esceq/core/band_model.h"
#include "esceq/dsp/auto_gain.h"
#include "esceq/ui/match.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace esceq;
using namespace esceq::test;

namespace
{

constexpr double kRate = 48000.0;
constexpr int kBlock = 480;
/// Un paso de la simulación: un paso de la sesión y un refresco de la interfaz.
constexpr double kStep = core::SpectrumSession::kUpdateSeconds;
constexpr int kBlocksPerStep = static_cast<int>(kRate * kStep) / kBlock; // 5

std::unique_ptr<PluginProcessor> makeIn(const std::shared_ptr<core::SpectrumSession>& session, bool sidechain)
{
    auto p = std::make_unique<PluginProcessor>(session, core::MatchWorker::Threading::Manual);
    auto layout = p->getBusesLayout();
    layout.inputBuses.getReference(1) =
        sidechain ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
    REQUIRE(p->setBusesLayout(layout));
    p->setRateAndBufferSizeDetails(kRate, kBlock);
    p->prepareToPlay(kRate, kBlock);
    return p;
}

/// Ruido rosa (el filtro de Paul Kellet sobre ruido blanco), siempre el mismo.
struct PinkNoise
{
    std::uint32_t seed = 2024u;
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

    float next()
    {
        seed = seed * 1664525u + 1013904223u;
        const double white = static_cast<double>(seed >> 8) / 8388608.0 - 1.0;
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        b3 = 0.86650 * b3 + white * 0.3104856;
        b4 = 0.55000 * b4 + white * 0.5329522;
        b5 = -0.7616 * b5 - white * 0.0168980;
        const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
        b6 = white * 0.115926;
        return static_cast<float>(0.05 * pink);
    }
};

/// La curva conocida: tres campanas y un shelf.
void setKnownCurve(PluginProcessor& p)
{
    auto values = p.currentValues();
    struct Band
    {
        dsp::FilterType type;
        float hz, gain, q;
    };
    const Band bands[] = {
        {dsp::FilterType::LowShelf, 120.0f, 4.0f, 0.707f},
        {dsp::FilterType::Bell, 500.0f, -3.0f, 1.0f},
        {dsp::FilterType::Bell, 2500.0f, 4.0f, 1.2f},
        {dsp::FilterType::Bell, 7000.0f, -3.5f, 1.0f},
    };
    for (int k = 0; k < 4; ++k)
    {
        const auto& b = bands[k];
        values[core::bandParameter(k, core::BandParam::Type)] = static_cast<float>(static_cast<int>(b.type));
        values[core::bandParameter(k, core::BandParam::Frequency)] = b.hz;
        values[core::bandParameter(k, core::BandParam::Gain)] = b.gain;
        values[core::bandParameter(k, core::BandParam::Q)] = b.q;
        values[core::bandParameter(k, core::BandParam::Enabled)] = 1.0f;
    }
    p.applyValues(values);
}

/// Curva estática de un procesador en `frequencies`, con la regla de la compensación de ganancia.
std::vector<double> staticCurve(PluginProcessor& p, const std::vector<double>& frequencies)
{
    const auto bands = core::allBandSettings(p.currentValues(), p.filterAlgorithm());
    std::vector<double> curve(frequencies.size());
    dsp::staticCurveDb(bands, kRate, 2, frequencies, curve);
    return curve;
}

ui::MatchContext contextOf(PluginProcessor& p, const std::vector<core::InstanceSpectrum>& instances,
                           const core::ParameterValues& values)
{
    ui::MatchContext c;
    c.instances = instances;
    c.self = p.instanceId();
    c.sidechainConnected = p.sidechainConnected();
    c.sampleRate = p.currentSampleRate();
    c.numChannels = p.outputChannelCount();
    c.algorithm = p.filterAlgorithm();
    c.values = &values;
    return c;
}

/// Lo que hace la interfaz en cada refresco: vaciar las colas en el controlador.
void drainQueues(PluginProcessor& p, ui::MatchController& controller)
{
    std::vector<float> chunk(4096);
    for (int n; (n = p.preAnalyzerFifo().read(chunk.data(), 4096)) > 0;)
        controller.pushTrack(chunk.data(), n);
    for (int n; (n = p.matchSidechain().read(chunk.data(), 4096)) > 0;)
        controller.pushSidechain(chunk.data(), n);
    // La cola "post" también la vacía la interfaz.
    while (p.postAnalyzerFifo().read(chunk.data(), 4096) > 0)
    {
    }
}

bool sameSamples(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp(a.getReadPointer(ch), b.getReadPointer(ch),
                        static_cast<std::size_t>(a.getNumSamples()) * sizeof(float)) != 0)
            return false;
    return true;
}

} // namespace

TEST_CASE("Igualacion: de extremo a extremo, la curva aplicada queda a 1 dB de la conocida")
{
    const bool fromSidechain = GENERATE(false, true);
    INFO((fromSidechain ? "referencia: el sidechain" : "referencia: otra instancia"));

    auto session = core::SpectrumSession::create(core::SpectrumSession::Threading::Manual);
    auto track = makeIn(session, fromSidechain);
    auto reference = makeIn(session, false);
    setKnownCurve(*reference);
    const auto& frequencies = core::sessionFrequencies();
    const auto known = staticCurve(*reference, frequencies);

    ui::MatchController controller(track->matchState(), track->matchWorker(), track->spectrumSession(),
                                   track->matchSidechain());
    std::vector<core::InstanceSpectrum> instances;
    session->snapshot(instances);
    auto values = track->currentValues();
    controller.selectReference(fromSidechain ? core::MatchReference::sidechain()
                                             : core::MatchReference::ofInstance(reference->instanceId()));
    auto settings = track->matchState().settings;
    settings.amount = 1.0; // al 100 %, con 1/3 de octava (por defecto)
    REQUIRE(settings.smoothing == core::MatchSmoothing::ThirdOctave);
    controller.setSettings(settings);

    PinkNoise noise;
    juce::AudioBuffer<float> trackBuffer(track->getTotalNumInputChannels(), kBlock);
    juce::AudioBuffer<float> referenceBuffer(reference->getTotalNumInputChannels(), kBlock);
    juce::MidiBuffer midi;
    auto step = [&]
    {
        for (int b = 0; b < kBlocksPerStep; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float s = noise.next();
                for (int ch = 0; ch < 2; ++ch)
                {
                    trackBuffer.setSample(ch, i, s);
                    referenceBuffer.setSample(ch, i, s);
                }
            }
            // La referencia suena el mismo ruido por la curva conocida, y su
            // salida va a la sesión o, como sidechain, a la pista.
            reference->processBlock(referenceBuffer, midi);
            if (fromSidechain)
                for (int ch = 0; ch < 2; ++ch)
                    trackBuffer.copyFrom(2 + ch, 0, referenceBuffer, ch, 0, kBlock);
            track->processBlock(trackBuffer, midi);
        }
        session->update(kStep);
        session->snapshot(instances);
        values = track->currentValues();
        drainQueues(*track, controller);
        controller.tick(kStep, contextOf(*track, instances, values));
    };

    // Unos pasos sin aprender (el audio ya suena) y luego Learn.
    for (int i = 0; i < 4; ++i)
        step();
    REQUIRE(controller.learn(contextOf(*track, instances, values)));
    CHECK(track->matchSidechain().enabled() == fromSidechain);

    // 10 s simulados (200 pasos) más los que tarda en contar el primero.
    int steps = 0;
    while (!track->matchState().learner.ready())
    {
        REQUIRE(steps < 205);
        step();
        ++steps;
    }
    CHECK(steps >= 200);
    controller.stop();
    CHECK_FALSE(track->matchSidechain().enabled());
    step();
    CHECK(track->matchWorker().runPending() >= 1);

    const auto context = contextOf(*track, instances, values);
    const auto status = controller.status(context);
    REQUIRE(status.canApply);
    const auto apply = controller.applyChanges(context);
    REQUIRE(apply.has_value());
    const auto proposal = controller.proposal();
    REQUIRE(proposal != nullptr);
    CHECK(proposal->result.bands.size() <= 8);
    track->applyAction(ui::withChanges(values, apply->changes));

    // Lo aplicado: la curva de la pista frente a la conocida, de 50 Hz a 12
    // kHz, salvo un nivel constante (el nivel no se iguala: el objetivo se
    // centra en la mediana de la diferencia).
    const auto applied = staticCurve(*track, frequencies);
    std::vector<double> difference;
    for (std::size_t i = 0; i < frequencies.size(); ++i)
        if (frequencies[i] >= 50.0 && frequencies[i] <= 12000.0)
            difference.push_back(known[i] - applied[i]);
    auto sorted = difference;
    std::sort(sorted.begin(), sorted.end());
    const double level = sorted[sorted.size() / 2];
    double worst = 0.0;
    for (const double d : difference)
        worst = std::max(worst, std::abs(d - level));
    INFO("bandas: " << proposal->result.bands.size() << ", nivel: " << level << " dB, peor: " << worst << " dB");
    CHECK(std::abs(level) < 1.0);
    CHECK(worst <= 1.0);
}

TEST_CASE("Igualacion: sin aprender del sidechain, processBlock es bit a bit el mismo")
{
    auto session = core::SpectrumSession::create(core::SpectrumSession::Threading::Manual);
    auto plain = makeIn(session, true);
    auto tapped = makeIn(session, true);
    for (auto* p : {plain.get(), tapped.get()})
        setKnownCurve(*p);

    PinkNoise noise;
    juce::AudioBuffer<float> a(4, kBlock), b(4, kBlock);
    juce::MidiBuffer midi;
    std::vector<float> chunk(4096);
    for (int block = 0; block < 200; ++block)
    {
        // Aprende del sidechain la mitad del tiempo: la salida no cambia nunca.
        tapped->matchSidechain().setEnabled(block % 100 >= 50);
        for (int i = 0; i < kBlock; ++i)
        {
            const float s = noise.next();
            for (int ch = 0; ch < 4; ++ch)
                a.setSample(ch, i, ch < 2 ? s : 0.5f * s);
        }
        b.makeCopyOf(a);
        plain->processBlock(a, midi);
        tapped->processBlock(b, midi);
        REQUIRE(sameSamples(a, b));
        // La cola del que no aprende sigue vacía.
        REQUIRE(plain->matchSidechain().read(chunk.data(), 4096) == 0);
        const int written = tapped->matchSidechain().read(chunk.data(), 4096);
        CHECK(written == (tapped->matchSidechain().enabled() ? kBlock : 0));
    }
}
