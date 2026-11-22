// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Igualar a una referencia en la lógica de la interfaz (ui/logic/match.h):
// las referencias, el aprendizaje con el audio de verdad (analizador de 8192
// puntos), cuándo se pide la propuesta, los motivos de Apply desactivado y
// los cambios que crean las bandas. La sesión y el hilo del ajuste van en
// manual: nada depende del tiempo.
#include "esceq/ui/match.h"
#include "esceq/ui/control_logic.h"

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
    values[core::spectralParameter(1)] = 1.0f;
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
                                 core::channelParameter(band), core::autoTimingParameter(band),
                                 core::spectralParameter(band)})
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

// --- Panel ---

TEST_CASE("Igualacion (ui): los nodos en fantasma van en las bandas que creara Apply")
{
    auto values = core::defaultValues();
    values[enabledOf(0)] = 1.0f;
    values[enabledOf(2)] = 1.0f;
    CHECK(matchTargetBands(values, 3) == std::vector<int>{1, 3, 4});
    CHECK(matchTargetBands(values, 0).empty());

    const std::vector<dsp::FilterParams> bands = {
        {dsp::FilterType::Bell, 200.0, 2.0, 1.0},
        {dsp::FilterType::Bell, 2000.0, -2.0, 1.0},
        {dsp::FilterType::HighShelf, 8000.0, 1.0, 0.7},
    };
    const auto apply = matchApplyChanges(values, bands);
    const auto after = withChanges(values, apply.changes);
    for (const int band : matchTargetBands(values, 3))
        CHECK(after[enabledOf(band)] == 1.0f);
    CHECK(*apply.firstBand == matchTargetBands(values, 3).front());

    // Si no caben todas, las que hay.
    for (int band = 0; band < core::kNumBands; ++band)
        values[enabledOf(band)] = band >= 22 ? 0.0f : 1.0f;
    CHECK(matchTargetBands(values, 5) == std::vector<int>{22, 23});
}

TEST_CASE("Igualacion (ui): textos del panel")
{
    CHECK(kMatchTitle == "Match");
    CHECK(matchLearnText(false) == "Learn");
    CHECK(matchLearnText(true) == "Stop");

    MatchStatus s;
    s.canLearn = false;
    s.applyBlock = MatchBlock::NoReference;
    CHECK(matchLearnTooltip(s) == "No reference");
    CHECK(matchApplyTooltip(s, 0) == "No reference");
    CHECK(matchStatusLine(s, 0) == MatchStatusLine{"No reference", false});

    s.canLearn = true;
    s.applyBlock = MatchBlock::LearnFirst;
    CHECK(matchLearnTooltip(s).find("Learn") == 0);
    CHECK(matchApplyTooltip(s, 0) == "Learn 10 s first");
    CHECK(matchStatusLine(s, 0) == MatchStatusLine{"Learn 10 s first", false});
    s.learning = true;
    CHECK(matchLearnTooltip(s).find("Stop") == 0);
    CHECK(matchStatusLine(s, 0) == MatchStatusLine{"Learning\xE2\x80\xA6", false});

    s.showCurves = true;
    s.applyBlock = MatchBlock::Fitting;
    CHECK(matchStatusLine(s, 0) == MatchStatusLine{"Fitting\xE2\x80\xA6", false});
    CHECK(matchApplyTooltip(s, 0) == "Fitting\xE2\x80\xA6");
    s.applyBlock = MatchBlock::NoFreeBands;
    CHECK(matchStatusLine(s, 0).text == "No free bands");
    s.applyBlock = MatchBlock::NothingToMatch;
    CHECK(matchStatusLine(s, 0).text == "Nothing to match");
    s.applyBlock = MatchBlock::None;
    s.canApply = true;
    CHECK(matchStatusLine(s, 4) == MatchStatusLine{"4 bands proposed", false});
    CHECK(matchStatusLine(s, 1).text == "1 band proposed");
    CHECK(matchApplyTooltip(s, 4) == "Create 4 bands in the free bands (one undo step)");
    CHECK(matchApplyTooltip(s, 1) == "Create 1 band in the free bands (one undo step)");

    // Un aviso se ve antes que lo demás, salvo mientras se vuelve a aprender.
    s.learning = false;
    s.notice = MatchNotice::ReferenceRemoved;
    CHECK(matchStatusLine(s, 4) == MatchStatusLine{"Reference removed", true});
    s.notice = MatchNotice::SidechainDisconnected;
    CHECK(matchStatusLine(s, 4) == MatchStatusLine{"Sidechain disconnected", true});

    CHECK(matchProgressFraction(0.0) == 0.0);
    CHECK_THAT(matchProgressFraction(5.0), WithinAbs(0.5, 1e-6));
    CHECK(matchProgressFraction(10.0) == 1.0);
    CHECK(matchProgressFraction(30.0) == 1.0);
    CHECK(matchProgressFraction(-1.0) == 0.0);
    CHECK(matchProgressFraction(std::nan("")) == 0.0);

    CHECK(kMatchSmoothingOptions.size() == 3);
    CHECK(kMatchSmoothingOptions[static_cast<std::size_t>(core::MatchSmoothing::SixthOctave)].text == "1/6");
    CHECK(kMatchSmoothingOptions[static_cast<std::size_t>(core::MatchSmoothing::ThirdOctave)].text == "1/3");
    CHECK(kMatchSmoothingOptions[static_cast<std::size_t>(core::MatchSmoothing::Octave)].text == "1 oct");
}

TEST_CASE("Igualacion (ui): Amount se escribe en tantos por ciento")
{
    CHECK(matchAmountText(0.5) == "50 %");
    CHECK(matchAmountText(0.0) == "0 %");
    CHECK(matchAmountText(1.0) == "100 %");
    CHECK(matchAmountText(1.7) == "100 %");
    CHECK(matchAmountText(std::nan("")) == "0 %");
    CHECK(parseMatchAmount("50") == 0.5);
    CHECK(parseMatchAmount("75 %") == 0.75);
    CHECK(parseMatchAmount(" 37.5% ") == 0.375);
    CHECK(parseMatchAmount("150") == 1.0);
    CHECK(parseMatchAmount("-3") == 0.0);
    CHECK_FALSE(parseMatchAmount("").has_value());
    CHECK_FALSE(parseMatchAmount("%").has_value());
    CHECK_FALSE(parseMatchAmount("half").has_value());
    CHECK_FALSE(parseMatchAmount("5 0").has_value());
    CHECK_FALSE(parseMatchAmount("nan").has_value());
}

TEST_CASE("Igualacion (ui): Max bands va de 1 a las bandas libres")
{
    core::MatchSettings settings;
    CHECK(matchMaxBandsLimit(24) == 24);
    CHECK(matchMaxBandsLimit(5) == 5);
    CHECK(matchMaxBandsLimit(0) == 1);
    CHECK(matchMaxBandsShown(settings, 24) == 8);
    CHECK(matchMaxBandsShown(settings, 5) == 5); // las libres, si son menos
    CHECK(matchMaxBandsShown(settings, 0) == 1);
    settings.maxBands = 20;
    CHECK(matchMaxBandsShown(settings, 24) == 20);
    // Lo que se muestra es lo que usa el ajuste.
    for (const int free : {1, 5, 8, 20, 24})
        CHECK(matchMaxBandsShown(settings, free) == core::effectiveMaxBands(settings, free));
}

TEST_CASE("Igualacion (ui): Range se escribe como el campo de frecuencia")
{
    for (const bool high : {false, true})
    {
        const auto& spec = matchRangeSpec(high);
        CHECK(spec.unit == "Hz");
        CHECK(spec.scale == core::ParameterScale::Logarithmic);
        CHECK(spec.min == 20.0f);
        CHECK(spec.max == 20000.0f);
        const core::MatchSettings defaults;
        CHECK(spec.defaultValue == static_cast<float>(high ? defaults.highHz : defaults.lowHz));
    }
    CHECK(core::formatValue(matchRangeSpec(false), 30.0f) == "30.0 Hz");
    CHECK(core::formatValue(matchRangeSpec(true), 16000.0f) == "16.0 kHz");
    // También una nota, como en el campo de frecuencia.
    const auto a3 = commitText(matchRangeSpec(false), "A3");
    REQUIRE(a3.has_value());
    CHECK_THAT(*a3, WithinAbs(220.0, 1e-3));
    CHECK_THAT(*commitText(matchRangeSpec(true), "12k"), WithinAbs(12000.0, 1e-3));

    core::MatchSettings settings;
    auto edited = withMatchRange(settings, false, 100.0);
    CHECK(edited.lowHz == 100.0);
    CHECK(edited.highHz == settings.highHz);
    edited = withMatchRange(settings, true, 5000.0);
    CHECK(edited.highHz == 5000.0);
    CHECK(edited.lowHz == settings.lowHz);

    // Un mínimo por encima del máximo empuja el máximo, y al revés: el
    // extremo que se edita se queda donde se escribe, salvo junto a los
    // topes, donde cede lo que falte para el rango mínimo.
    const double ratio = std::exp2(core::MatchSettings::kMinRangeOctaves);
    edited = withMatchRange(settings, false, 18000.0);
    CHECK_THAT(edited.highHz, WithinAbs(20000.0, 1e-6));
    CHECK_THAT(edited.lowHz, WithinAbs(20000.0 / ratio, 1e-6));
    edited = withMatchRange(settings, true, 25.0);
    CHECK_THAT(edited.lowHz, WithinAbs(20.0, 1e-6));
    CHECK_THAT(edited.highHz, WithinAbs(20.0 * ratio, 1e-6));
    edited = withMatchRange(settings, true, 1000.0);
    edited = withMatchRange(edited, false, 900.0);
    CHECK(edited.lowHz == 900.0);
    CHECK_THAT(edited.highHz, WithinAbs(900.0 * ratio, 1e-6));
    CHECK(withMatchRange(settings, true, std::nan("")) == core::sanitize(settings));
}

TEST_CASE("Igualacion (ui): la referencia se dibuja desplazada a la mediana de la diferencia")
{
    // La referencia: la pista 20 dB más alta, con 6 dB más en cinco puntos.
    const std::size_t n = 21;
    std::vector<float> track(n), reference(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        track[i] = -40.0f - static_cast<float>(i) * 0.5f;
        reference[i] = track[i] + 20.0f + (i >= 8 && i < 13 ? 6.0f : 0.0f);
    }
    auto display = matchSpectraDisplay(track, reference);
    CHECK_THAT(display.shiftDb, WithinAbs(20.0, 1e-5));
    CHECK(display.trackDb == track);
    REQUIRE(display.referenceDb.size() == n);
    for (std::size_t i = 0; i < n; ++i)
        CHECK_THAT(display.referenceDb[i], WithinAbs(track[i] + (i >= 8 && i < 13 ? 6.0f : 0.0f), 1e-4));

    // Lo que no está presente en una de las dos no cuenta en la mediana.
    for (std::size_t i = 0; i < 12; ++i)
        reference[i] = core::kSessionFloorDb;
    display = matchSpectraDisplay(track, reference);
    CHECK_THAT(display.shiftDb, WithinAbs(20.0, 1e-5));

    // Sin nada presente, la de todos; sin puntos, nada.
    const std::vector<float> floor(n, core::kSessionFloorDb);
    CHECK_THAT(matchSpectraDisplay(floor, floor).shiftDb, WithinAbs(0.0, 1e-9));
    CHECK(matchSpectraDisplay({}, {}).referenceDb.empty());
}

TEST_CASE("Igualacion (ui): la curva objetivo se corta donde no cuenta y lleva el nivel libre")
{
    dsp::CurveMatchResult result;
    result.weights = {0.1, 0.1, 1.0, 1.0, 0.0, 0.0, 1.0, 0.5, 0.1, 0.0};
    result.targetDb = {1.0, 1.0, 2.0, 3.0, 0.0, 0.0, -1.0, -2.0, -2.0, -2.0};
    result.offsetDb = 0.25;
    CHECK(matchTargetRuns(result) == std::vector<MatchRun>{{0, 4}, {6, 9}});
    const auto curve = matchTargetCurve(result);
    REQUIRE(curve.size() == result.targetDb.size());
    for (std::size_t i = 0; i < curve.size(); ++i)
        CHECK(curve[i] == result.targetDb[i] + 0.25);
    CHECK(matchTargetRuns({}).empty());
}

TEST_CASE("Igualacion (ui): de extremo a extremo, lo que se dibuja")
{
    Rig rig;
    REQUIRE(rig.controller->learn(rig.context()));
    rig.play(10.5);
    rig.controller->stop();
    rig.idle(kTick);
    rig.worker.runPending();
    const auto proposal = rig.controller->proposal();
    REQUIRE(proposal != nullptr);
    const auto& result = proposal->result;

    // Los espectros: la referencia es la pista con +5 dB a 1 kHz, así que
    // desplazada a la mediana coincide con ella lejos de 1 kHz.
    const auto display = matchSpectraDisplay(rig.state.learner.trackDb(), rig.state.learner.referenceDb());
    const auto& frequencies = core::sessionFrequencies();
    REQUIRE(display.referenceDb.size() == frequencies.size());
    for (std::size_t i = 0; i < frequencies.size(); ++i)
        if (frequencies[i] > 60.0 && frequencies[i] < 150.0)
            CHECK_THAT(display.referenceDb[i], WithinAbs(display.trackDb[i], 1.5));

    // El objetivo, donde cuenta, y la propuesta cerca de él.
    const auto runs = matchTargetRuns(result);
    REQUIRE_FALSE(runs.empty());
    const auto target = matchTargetCurve(result);
    std::size_t checked = 0;
    for (const auto& run : runs)
        for (std::size_t i = run.begin; i < run.end; ++i)
            if (result.frequencies[i] > 200.0 && result.frequencies[i] < 12000.0 && result.weights[i] >= 1.0)
            {
                CHECK_THAT(result.proposedDb[i], WithinAbs(target[i], 1.0));
                ++checked;
            }
    CHECK(checked > 100);
    CHECK(matchTargetBands(rig.values, static_cast<int>(result.bands.size())).size() == result.bands.size());
}
