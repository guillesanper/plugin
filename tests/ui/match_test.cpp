// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Igualar a una referencia en la lógica de la interfaz (ui/logic/match.h):
// las referencias, el aprendizaje con el audio de verdad (analizador de 8192
// puntos), cuándo se pide la propuesta, los motivos de Apply desactivado y
// los cambios que crean las bandas. La sesión y el hilo del ajuste van en
// manual: nada depende del tiempo.
#include "esceq/ui/match.h"

#include "esceq/core/band_model.h"
#include "esceq/dsp/band_filter.h"
#include "esceq/dsp/filter_design.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace esceq;
using namespace esceq::ui;
using Catch::Matchers::WithinAbs;
using Kind = core::MatchReference::Kind;

namespace
{

constexpr double kRate = 48000.0;
constexpr double kTick = 1.0 / 30.0; // el refresco de la interfaz
constexpr int kTickSamples = 1600;

std::size_t enabledOf(int band)
{
    return core::bandParameter(band, core::BandParam::Enabled);
}

/// Ruido blanco, siempre el mismo.
struct Noise
{
    std::uint32_t seed = 12345u;
    float next()
    {
        seed = seed * 1664525u + 1013904223u;
        return 0.25f * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
};

/// Una instancia con su interfaz: lo que el procesador guarda (estado, hilo
/// del ajuste en manual, cola del sidechain) y el controlador del panel.
struct Rig
{
    std::shared_ptr<core::SpectrumSession> session =
        core::SpectrumSession::create(core::SpectrumSession::Threading::Manual);
    std::unique_ptr<core::SpectrumSession::Member> self = session->join();
    core::MatchState state;
    core::MatchWorker worker{core::MatchWorker::Threading::Manual};
    core::MatchSidechainTap tap;
    std::unique_ptr<MatchController> controller =
        std::make_unique<MatchController>(state, worker, *session, tap);

    core::ParameterValues values = core::defaultValues();
    std::vector<core::InstanceSpectrum> instances;
    bool sidechainConnected = true;

    Noise noise;
    dsp::BandFilter coloured; ///< Lo que distingue la referencia de la pista.
    bool trackOn = true;
    bool sidechainOn = true;

    Rig()
    {
        // +5 dB a 1 kHz: lo que hay que igualar.
        coloured.setDesign(dsp::designBand({dsp::FilterType::Bell, 1000.0, 5.0, 1.0}, kRate));
        session->snapshot(instances);
    }

    MatchContext context()
    {
        MatchContext c;
        c.instances = instances;
        c.self = self->id();
        c.sidechainConnected = sidechainConnected;
        c.sampleRate = kRate;
        c.numChannels = 2;
        c.algorithm = dsp::FilterAlgorithm::Matched;
        c.values = &values;
        return c;
    }

    /// `seconds` de audio y de refrescos: la pista va a la cola "pre" y el
    /// sidechain a su cola (el hilo de audio), y la interfaz las lee.
    void play(double seconds)
    {
        const int ticks = static_cast<int>(std::lround(seconds / kTick));
        std::vector<float> track(kTickSamples), side(kTickSamples), read(kTickSamples);
        for (int t = 0; t < ticks; ++t)
        {
            for (int i = 0; i < kTickSamples; ++i)
            {
                const float n = noise.next();
                track[static_cast<std::size_t>(i)] = trackOn ? n : 0.0f;
                side[static_cast<std::size_t>(i)] = sidechainOn ? n : 0.0f;
            }
            float* sideChannels[] = {side.data()};
            coloured.process(sideChannels, 1, kTickSamples);
            if (sidechainConnected)
            {
                const float* channels[] = {side.data()};
                tap.write(channels, 1, kTickSamples);
            }
            controller->pushTrack(track.data(), kTickSamples);
            for (int n; (n = tap.read(read.data(), kTickSamples)) > 0;)
                controller->pushSidechain(read.data(), n);
            controller->tick(kTick, context());
        }
    }

    /// Refrescos sin audio nuevo (el transporte parado).
    void idle(double seconds)
    {
        const int ticks = static_cast<int>(std::lround(seconds / kTick));
        for (int t = 0; t < ticks; ++t)
            controller->tick(kTick, context());
    }

    MatchStatus status() { return controller->status(context()); }

    void apply()
    {
        const auto changes = controller->applyChanges(context());
        REQUIRE(changes.has_value());
        values = withChanges(values, changes->changes);
    }
};

} // namespace

// --- Referencias ---

TEST_CASE("Igualacion (ui): el desplegable lista las otras instancias y el sidechain")
{
    std::vector<core::InstanceSpectrum> instances(3);
    instances[0].id = 7;
    instances[0].number = 1;
    instances[0].hasSignal = true;
    instances[1].id = 8; // la propia
    instances[1].number = 2;
    instances[2].id = 9;
    instances[2].number = 3;
    instances[2].trackName = "Bass";

    auto options = matchReferenceOptions(instances, 8, false);
    REQUIRE(options.size() == 3);
    CHECK(options[0].reference == core::MatchReference::ofInstance(7));
    CHECK(options[0].text() == "esceQ 1");
    CHECK(options[1].reference == core::MatchReference::ofInstance(9));
    CHECK(options[1].text() == "Bass (silent)");
    CHECK(options[2].reference == core::MatchReference::sidechain());
    CHECK_FALSE(options[2].enabled);
    CHECK(options[2].text() == "Sidechain (not connected)");

    // Por defecto, la primera con señal; si no, la primera; si no, el sidechain conectado.
    CHECK(defaultMatchReference(options) == core::MatchReference::ofInstance(7));
    instances[0].hasSignal = false;
    instances[2].hasSignal = true;
    CHECK(defaultMatchReference(matchReferenceOptions(instances, 8, false)) == core::MatchReference::ofInstance(9));
    instances[2].hasSignal = false;
    CHECK(defaultMatchReference(matchReferenceOptions(instances, 8, false)) == core::MatchReference::ofInstance(7));

    const std::vector<core::InstanceSpectrum> alone(instances.begin() + 1, instances.begin() + 2);
    options = matchReferenceOptions(alone, 8, true);
    REQUIRE(options.size() == 1);
    CHECK(options[0].text() == "Sidechain");
    CHECK(defaultMatchReference(options) == core::MatchReference::sidechain());
    CHECK(canLearnFrom(core::MatchReference::sidechain(), options));
    CHECK_FALSE(canLearnFrom(core::MatchReference::ofInstance(7), options));
    CHECK_FALSE(canLearnFrom(core::MatchReference::none(), options));
    options = matchReferenceOptions(alone, 8, false);
    CHECK(defaultMatchReference(options) == core::MatchReference::none());
}

TEST_CASE("Igualacion (ui): textos")
{
    CHECK(matchBlockText(MatchBlock::None).empty());
    CHECK(matchBlockText(MatchBlock::NoReference) == "No reference");
    CHECK(matchBlockText(MatchBlock::LearnFirst) == "Learn 10 s first");
    CHECK(matchBlockText(MatchBlock::NoFreeBands) == "No free bands");
    CHECK(matchBlockText(MatchBlock::Fitting) == "Fitting\xE2\x80\xA6");
    CHECK(matchBlockText(MatchBlock::NothingToMatch) == "Nothing to match");
    CHECK(matchNoticeText(MatchNotice::ReferenceRemoved) == "Reference removed");
    CHECK(matchNoticeText(MatchNotice::SidechainDisconnected) == "Sidechain disconnected");
    CHECK(matchProgressText(0.0) == "0.0 s / 10 s");
    CHECK(matchProgressText(3.24) == "3.2 s / 10 s");
    CHECK(matchProgressText(9.96) == "9.9 s / 10 s");
    CHECK(matchProgressText(9.9999999999) == "10.0 s / 10 s");
    CHECK(matchProgressText(42.0) == "42.0 s / 10 s");
}

TEST_CASE("Igualacion (ui): sin referencia, Learn esta desactivado")
{
    Rig rig;
    rig.sidechainConnected = false;
    const auto s = rig.status();
    CHECK_FALSE(s.canLearn);
    CHECK(s.learnBlock == MatchBlock::NoReference);
    CHECK_FALSE(s.canApply);
    CHECK(s.applyBlock == MatchBlock::NoReference);
    CHECK_FALSE(rig.controller->learn(rig.context()));
    CHECK_FALSE(rig.state.learning);
}

// --- Aprendizaje ---

TEST_CASE("Igualacion (ui): Apply, de Learn 10 s first a una propuesta")
{
    Rig rig;
    REQUIRE(rig.controller->learn(rig.context()));
    CHECK(rig.state.reference == core::MatchReference::sidechain());
    CHECK(rig.tap.enabled());
    CHECK(rig.status().learning);

    rig.play(5.0);
    auto s = rig.status();
    CHECK(s.applyBlock == MatchBlock::LearnFirst);
    CHECK_FALSE(s.showCurves);
    CHECK(s.usefulSeconds > 4.5);
    CHECK(s.usefulSeconds < 5.01);
    CHECK(rig.worker.lastSubmitted() == 0); // nada que ajustar aún

    rig.play(5.1);
    s = rig.status();
    CHECK(s.showCurves);
    CHECK(rig.worker.lastSubmitted() == 1); // en cuanto hay 10 s útiles
    CHECK(s.applyBlock == MatchBlock::Fitting);
    CHECK_FALSE(rig.controller->applyChanges(rig.context()).has_value());

    rig.controller->stop();
    CHECK_FALSE(rig.tap.enabled());
    rig.idle(kTick); // lo aprendido desde la última petición, en una nueva
    CHECK(rig.worker.lastSubmitted() == 2);
    rig.worker.runPending();
    s = rig.status();
    CHECK(s.applyBlock == MatchBlock::None);
    CHECK(s.canApply);
    const auto proposal = rig.controller->proposal();
    REQUIRE(proposal != nullptr);
    REQUIRE_FALSE(proposal->result.bands.empty());
    // +5 dB a 1 kHz, a la mitad (la cantidad por defecto): una campana de unos +2,5 dB.
    bool found = false;
    for (const auto& band : proposal->result.bands)
        if (band.type == dsp::FilterType::Bell && std::abs(std::log2(band.frequencyHz / 1000.0)) < 0.25)
            found = std::abs(band.gainDb - 2.5) < 0.6;
    CHECK(found);
    // Las curvas de la propuesta, con el algoritmo de la instancia.
    CHECK(proposal->result.bands.front().algorithm == dsp::FilterAlgorithm::Matched);

    // Sin bandas libres, no.
    auto full = rig.values;
    for (int band = 0; band < core::kNumBands; ++band)
        full[enabledOf(band)] = 1.0f;
    const auto saved = rig.values;
    rig.values = full;
    CHECK(rig.status().applyBlock == MatchBlock::NoFreeBands);
    rig.idle(1.0);
    CHECK(rig.worker.lastSubmitted() == 2);
    CHECK_FALSE(rig.worker.busy());
    rig.values = saved;
    rig.idle(0.2);
    rig.worker.runPending();
    CHECK(rig.status().canApply);
}

TEST_CASE("Igualacion (ui): el silencio de cualquiera de las dos no suma segundos")
{
    Rig rig;
    REQUIRE(rig.controller->learn(rig.context()));
    rig.trackOn = false;
    rig.play(2.0);
    CHECK(rig.state.learner.usefulSeconds() == 0.0);

    rig.trackOn = true;
    rig.sidechainOn = false;
    rig.play(2.0);
    // Al callarse, cuenta como mucho lo que dura la señal tras el último
    // audio audible (0,5 s), más lo que tarda en salir de la ventana del
    // analizador (8192 muestras) y un refresco.
    CHECK(rig.state.learner.usefulSeconds() <= core::SpectrumSession::kSignalHoldSeconds + 8192.0 / kRate + kTick);
    const double before = rig.state.learner.usefulSeconds();
    rig.play(2.0);
    CHECK(rig.state.learner.usefulSeconds() == before);

    // Con el transporte parado (sin audio) no avanza.
    rig.sidechainOn = true;
    rig.play(1.0);
    const double playing = rig.state.learner.usefulSeconds();
    CHECK(playing > before + 0.9);
    rig.idle(3.0);
    CHECK(rig.state.learner.usefulSeconds() == playing);
}

TEST_CASE("Igualacion (ui): aprender de otra instancia pondera por su reloj de audio")
{
    Rig rig;
    auto reference = rig.session->join();
    reference->setSampleRate(96000.0);
    rig.session->snapshot(rig.instances);
    rig.controller->selectReference(core::MatchReference::ofInstance(reference->id()));
    REQUIRE(rig.controller->learn(rig.context()));
    CHECK(rig.session->watched());
    CHECK_FALSE(rig.tap.enabled());

    // 6 s: la sesión a 20 Hz y la interfaz a 30 Hz, cada una con su audio.
    Noise noise;
    std::vector<float> block(800);
    std::vector<float> track(kTickSamples);
    double sessionClock = 0.0;
    double uiClock = 0.0;
    while (uiClock < 6.0 - 1e-9)
    {
        for (auto& x : track)
            x = rig.noise.next();
        rig.controller->pushTrack(track.data(), kTickSamples);
        uiClock += kTick;
        while (sessionClock + core::SpectrumSession::kUpdateSeconds <= uiClock + 1e-9)
        {
            for (int b = 0; b < 3; ++b)
            {
                for (auto& x : block)
                    x = noise.next();
                const float* channels[] = {block.data()};
                reference->feed(channels, 1, static_cast<int>(block.size()));
            }
            rig.session->update(core::SpectrumSession::kUpdateSeconds);
            sessionClock += core::SpectrumSession::kUpdateSeconds;
        }
        rig.session->snapshot(rig.instances);
        rig.controller->tick(kTick, rig.context());
    }
    // Cuenta el tiempo de la referencia, salvo su primer paso (antes de que
    // la pista tuviera señal) y el de la sesión que vuelve a empezar.
    CHECK(rig.state.learner.usefulSeconds() > 5.8);
    CHECK(rig.state.learner.usefulSeconds() <= 6.0 + 1e-9);
    CHECK(rig.state.learner.resolutionHz() == 96000.0 / 8192.0);

    // La referencia desaparece: se para, y lo aprendido se queda.
    const double learned = rig.state.learner.usefulSeconds();
    reference.reset();
    rig.session->snapshot(rig.instances);
    rig.idle(kTick);
    auto s = rig.status();
    CHECK_FALSE(s.learning);
    CHECK(s.notice == MatchNotice::ReferenceRemoved);
    CHECK(rig.state.learner.usefulSeconds() == learned);
    CHECK_FALSE(rig.session->watched());
}

TEST_CASE("Igualacion (ui): desconectar el sidechain para el aprendizaje")
{
    Rig rig;
    REQUIRE(rig.controller->learn(rig.context()));
    rig.play(1.0);
    const double learned = rig.state.learner.usefulSeconds();
    REQUIRE(learned > 0.5);
    rig.sidechainConnected = false;
    rig.play(kTick);
    const auto s = rig.status();
    CHECK_FALSE(s.learning);
    CHECK(s.notice == MatchNotice::SidechainDisconnected);
    CHECK(rig.state.learner.usefulSeconds() == learned);
    CHECK_FALSE(rig.tap.enabled());
    // Sin referencia, Learn no puede, y lo aprendido se queda.
    CHECK_FALSE(rig.controller->learn(rig.context()));
    CHECK(rig.state.learner.usefulSeconds() == learned);
    CHECK(rig.state.reference == core::MatchReference::sidechain());
}

TEST_CASE("Igualacion (ui): cambiar de referencia borra lo aprendido; cerrar el panel lo conserva")
{
    Rig rig;
    REQUIRE(rig.controller->learn(rig.context()));
    rig.play(1.0);
    REQUIRE(rig.state.learner.usefulSeconds() > 0.5);

    // Cerrar el panel (destruir el controlador) es Stop.
    rig.controller.reset();
    CHECK_FALSE(rig.state.learning);
    CHECK_FALSE(rig.tap.enabled());
    CHECK(rig.state.learner.usefulSeconds() > 0.5);
    rig.controller = std::make_unique<MatchController>(rig.state, rig.worker, *rig.session, rig.tap);
    CHECK(rig.state.learner.usefulSeconds() > 0.5);
    CHECK_FALSE(rig.status().learning);

    // La misma referencia no borra nada; otra, sí.
    rig.controller->selectReference(core::MatchReference::sidechain());
    CHECK(rig.state.learner.usefulSeconds() > 0.5);
    rig.controller->selectReference(core::MatchReference::ofInstance(99));
    CHECK(rig.state.learner.empty());
    CHECK(rig.state.reference == core::MatchReference::ofInstance(99));
}

// --- Peticiones y Apply ---

TEST_CASE("Igualacion (ui): la propuesta se vuelve a pedir 150 ms despues del ultimo cambio")
{
    Rig rig;
    REQUIRE(rig.controller->learn(rig.context()));
    rig.play(10.1);
    rig.controller->stop();
    rig.idle(kTick);
    rig.worker.runPending();
    const auto submitted = rig.worker.lastSubmitted();
    REQUIRE(rig.status().canApply);

    // Un ajuste que se mueve: mientras se mueve, nada; 150 ms después, una petición.
    auto settings = rig.state.settings;
    for (int i = 0; i < 10; ++i)
    {
        settings.amount = 0.52 + 0.02 * i;
        rig.controller->setSettings(settings);
        rig.idle(kTick);
        CHECK(rig.status().applyBlock == MatchBlock::Fitting);
    }
    CHECK(rig.worker.lastSubmitted() == submitted);
    rig.idle(0.1);
    CHECK(rig.worker.lastSubmitted() == submitted);
    rig.idle(0.1);
    CHECK(rig.worker.lastSubmitted() == submitted + 1);
    rig.worker.runPending();
    CHECK(rig.status().canApply);

    // Una banda que cambia, igual.
    rig.values[enabledOf(20)] = 1.0f;
    rig.idle(kTick);
    CHECK(rig.status().applyBlock == MatchBlock::Fitting);
    rig.idle(0.2);
    CHECK(rig.worker.lastSubmitted() == submitted + 2);
}

TEST_CASE("Igualacion (ui): mientras se aprende, se vuelve a pedir cada segundo util")
{
    Rig rig;
    REQUIRE(rig.controller->learn(rig.context()));
    rig.play(10.1);
    CHECK(rig.worker.lastSubmitted() == 1);
    rig.play(0.8);
    CHECK(rig.worker.lastSubmitted() == 1);
    rig.play(0.3);
    CHECK(rig.worker.lastSubmitted() == 2);
    rig.play(3.0);
    CHECK(rig.worker.lastSubmitted() == 5);
    // Solo la última se calcula.
    CHECK(rig.worker.runPending() == 1);
}

TEST_CASE("Igualacion (ui): aplicar dos veces no crea bandas la segunda")
{
    Rig rig;
    REQUIRE(rig.controller->learn(rig.context()));
    rig.play(10.1);
    rig.controller->stop();
    rig.idle(kTick);
    rig.worker.runPending();
    REQUIRE(rig.status().canApply);
    const auto proposed = rig.controller->proposal()->result.bands.size();

    rig.apply();
    CHECK(freeBandCount(rig.values) == core::kNumBands - static_cast<int>(proposed));
    CHECK(rig.status().applyBlock == MatchBlock::Fitting);
    rig.idle(0.2);
    rig.worker.runPending();
    const auto s = rig.status();
    CHECK(s.applyBlock == MatchBlock::NothingToMatch);
    CHECK_FALSE(s.canApply);
    REQUIRE(rig.controller->proposal() != nullptr);
    CHECK(rig.controller->proposal()->result.bands.empty());
    CHECK_FALSE(rig.controller->applyChanges(rig.context()).has_value());
}

TEST_CASE("Igualacion (ui): Apply crea las bandas en las libres, de la menor en adelante")
{
    auto values = core::defaultValues();
    // La banda 0 está en uso; la 1, apagada pero con ajustes de cuando sonaba.
    values[enabledOf(0)] = 1.0f;
    values[core::bandParameter(1, core::BandParam::Slope)] = 4.0f;
    values[core::dynamicParameter(1, core::DynParam::Enabled)] = 1.0f;
    values[core::dynamicParameter(1, core::DynParam::Threshold)] = -12.0f;
    values[core::channelParameter(1)] = 3.0f;
    values[core::autoTimingParameter(1)] = 1.0f;
    values[enabledOf(2)] = 1.0f;

    const std::vector<dsp::FilterParams> bands = {
        {dsp::FilterType::LowShelf, 90.0, 3.5, 0.8},
        {dsp::FilterType::Bell, 700.0, -2.25, 2.5},
        {dsp::FilterType::HighShelf, 9000.0, 1.5, 0.6},
    };
    const auto apply = matchApplyChanges(values, bands);
    REQUIRE(apply.firstBand.has_value());
    CHECK(*apply.firstBand == 1);
    const auto after = withChanges(values, apply.changes);

    const int expectedBands[] = {1, 3, 4};
    for (std::size_t k = 0; k < bands.size(); ++k)
    {
        const int band = expectedBands[k];
        const auto settings = core::bandSettings(after, band);
        CHECK(settings.enabled);
        CHECK(settings.filter.type == bands[k].type);
        CHECK_THAT(settings.filter.frequencyHz, WithinAbs(bands[k].frequencyHz, 1e-3));
        CHECK_THAT(settings.filter.gainDb, WithinAbs(bands[k].gainDb, 1e-6));
        CHECK_THAT(settings.filter.q, WithinAbs(bands[k].q, 1e-6));
        CHECK(settings.channel == dsp::ChannelMode::Stereo);
        CHECK_FALSE(settings.dynamics.enabled);
        // Lo demás, por defecto.
        const auto& specs = core::parameters();
        for (const auto index : {core::bandParameter(band, core::BandParam::Slope),
                                 core::dynamicParameter(band, core::DynParam::Threshold),
                                 core::dynamicParameter(band, core::DynParam::Range),
                                 core::dynamicParameter(band, core::DynParam::Attack),
                                 core::dynamicParameter(band, core::DynParam::Release),
                                 core::dynamicParameter(band, core::DynParam::Knee),
                                 core::dynamicParameter(band, core::DynParam::Source),
                                 core::channelParameter(band), core::autoTimingParameter(band)})
            CHECK(after[index] == specs[index].defaultValue);
    }
    // Las bandas que había y las que no se tocan siguen igual.
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        bool touched = false;
        for (const int band : expectedBands)
            touched = touched || core::bandParameter(band, core::BandParam::Enabled) == i ||
                      std::find_if(apply.changes.begin(), apply.changes.end(),
                                   [i](const auto& c) { return c.index == i; }) != apply.changes.end();
        if (!touched)
            CHECK(after[i] == values[i]);
    }
    // La activación de cada banda va tras sus valores.
    CHECK(apply.changes.back().index == enabledOf(4));

    // Las que no caben no se crean.
    auto almostFull = values;
    for (int band = 0; band < core::kNumBands; ++band)
        almostFull[enabledOf(band)] = band == 23 ? 0.0f : 1.0f;
    const auto one = matchApplyChanges(almostFull, bands);
    CHECK(*one.firstBand == 23);
    CHECK(freeBandCount(withChanges(almostFull, one.changes)) == 0);
    CHECK(core::bandSettings(withChanges(almostFull, one.changes), 23).filter.type == dsp::FilterType::LowShelf);
    CHECK_FALSE(matchApplyChanges(withChanges(almostFull, one.changes), bands).firstBand.has_value());
}
