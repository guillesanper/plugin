// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Igualar a una referencia en core (core/match.h): ajustes, aprendizaje, el
// hilo del ajuste y la cola del sidechain. Sin esperas por tiempo: el hilo se
// prueba en manual o con los avisos de MatchWorker::Observer.
#include "esceq/core/match.h"

#include "esceq/dsp/auto_gain.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace esceq;
using namespace esceq::core;
using Catch::Matchers::WithinAbs;

namespace
{

constexpr std::size_t kPoints = static_cast<std::size_t>(kSessionPoints);

std::vector<float> flat(float db)
{
    return std::vector<float>(kPoints, db);
}

/// Pista plana y referencia con dos campanas y un shelf encima.
MatchRequest makeRequest(int maxBands = 8)
{
    const std::vector<dsp::BandSettings> known = {
        {true, {dsp::FilterType::LowShelf, 120.0, 4.0, 0.707}},
        {true, {dsp::FilterType::Bell, 900.0, -3.0, 1.2}},
        {true, {dsp::FilterType::Bell, 5000.0, 4.0, 1.0}},
    };
    const auto& f = sessionFrequencies();
    std::vector<double> curve(f.size());
    dsp::staticCurveDb(known, 48000.0, 2, f, curve);

    MatchRequest request;
    request.frequencies.assign(f.begin(), f.end());
    request.trackDb = flat(-40.0f);
    request.referenceDb = flat(-40.0f);
    for (std::size_t i = 0; i < f.size(); ++i)
        request.referenceDb[i] += static_cast<float>(curve[i]);
    request.resolutionHz = 48000.0 / 8192.0;
    request.sampleRate = 48000.0;
    MatchSettings settings;
    settings.amount = 1.0;
    settings.maxBands = maxBands;
    request.settings = toCurveMatchSettings(settings, kNumBands);
    return request;
}

void checkSameBands(const std::vector<dsp::FilterParams>& a, const std::vector<dsp::FilterParams>& b)
{
    REQUIRE(a.size() == b.size());
    for (std::size_t k = 0; k < a.size(); ++k)
    {
        // Campo a campo: FilterParams tiene relleno.
        CHECK(a[k].type == b[k].type);
        CHECK(a[k].frequencyHz == b[k].frequencyHz);
        CHECK(a[k].gainDb == b[k].gainDb);
        CHECK(a[k].q == b[k].q);
        CHECK(a[k].slopeDbPerOct == b[k].slopeDbPerOct);
        CHECK(a[k].algorithm == b[k].algorithm);
    }
}

MatchObservation observe(std::span<const float> track, std::span<const float> reference, double dt)
{
    return {.trackDb = track,
            .referenceDb = reference,
            .trackHasSignal = true,
            .referenceHasSignal = true,
            .trackSampleRate = 48000.0,
            .referenceSampleRate = 48000.0,
            .dtSeconds = dt};
}

} // namespace

// --- Ajustes ---

TEST_CASE("Igualacion: ajustes por defecto y saneados")
{
    const MatchSettings defaults;
    CHECK(defaults.amount == 0.5);
    CHECK(defaults.smoothing == MatchSmoothing::ThirdOctave);
    CHECK(defaults.maxBands == 8);
    CHECK(defaults.lowHz == 30.0);
    CHECK(defaults.highHz == 16000.0);
    CHECK(sanitize(defaults) == defaults);

    CHECK(smoothingOctaves(MatchSmoothing::SixthOctave) == 1.0 / 6.0);
    CHECK(smoothingOctaves(MatchSmoothing::ThirdOctave) == 1.0 / 3.0);
    CHECK(smoothingOctaves(MatchSmoothing::Octave) == 1.0);

    MatchSettings wild;
    wild.amount = 3.0;
    wild.maxBands = 99;
    wild.lowHz = 5.0;
    wild.highHz = 1.0e6;
    auto sane = sanitize(wild);
    CHECK(sane.amount == 1.0);
    CHECK(sane.maxBands == kNumBands);
    CHECK(sane.lowHz == 20.0);
    CHECK(sane.highHz == 20000.0);

    wild.amount = std::nan("");
    wild.maxBands = 0;
    wild.lowHz = std::nan("");
    wild.highHz = -1.0;
    sane = sanitize(wild);
    CHECK(sane.amount == 0.5);
    CHECK(sane.maxBands == 1);
    CHECK(sane.lowHz == 30.0);
    // Un rango de menos de 1/3 de octava se ensancha por arriba...
    CHECK_THAT(sane.highHz, WithinAbs(30.0 * std::exp2(1.0 / 3.0), 1e-9));

    // ...o, en el tope, por abajo.
    wild.lowHz = 19990.0;
    wild.highHz = 20000.0;
    sane = sanitize(wild);
    CHECK(sane.highHz == 20000.0);
    CHECK_THAT(sane.lowHz, WithinAbs(20000.0 / std::exp2(1.0 / 3.0), 1e-9));
}

TEST_CASE("Igualacion: como mucho las bandas libres")
{
    MatchSettings settings; // 8
    CHECK(effectiveMaxBands(settings, 24) == 8);
    CHECK(effectiveMaxBands(settings, 3) == 3);
    CHECK(effectiveMaxBands(settings, 0) == 0);
    settings.maxBands = 24;
    settings.amount = 0.75;
    settings.smoothing = MatchSmoothing::Octave;
    settings.lowHz = 40.0;
    settings.highHz = 12000.0;
    const auto dsp = toCurveMatchSettings(settings, 10);
    CHECK(dsp.maxBands == 10);
    CHECK(dsp.amount == 0.75);
    CHECK(dsp.smoothingOctaves == 1.0);
    CHECK(dsp.lowHz == 40.0);
    CHECK(dsp.highHz == 12000.0);
    CHECK(dsp.presence == dsp::CollisionSettings{});
}

// --- Aprendizaje ---

TEST_CASE("Igualacion: el aprendizaje es la media en potencia ponderada por el tiempo")
{
    MatchLearner learner;
    CHECK(learner.empty());
    CHECK(learner.trackDb() == flat(kSessionFloorDb));
    const auto first = learner.revision();

    // 0,1 s a −20 dB y 0,2 s a −30 dB: 10·log10((0,1·10^−2 + 0,2·10^−3) / 0,3).
    const auto a = flat(-20.0f);
    const auto b = flat(-30.0f);
    REQUIRE(learner.add(observe(a, b, 0.1)));
    REQUIRE(learner.add(observe(b, a, 0.2)));
    CHECK(learner.revision() != first);
    CHECK_THAT(learner.usefulSeconds(), WithinAbs(0.3, 1e-12));
    const double expectedTrack = 10.0 * std::log10((0.1 * 1e-2 + 0.2 * 1e-3) / 0.3);
    const double expectedReference = 10.0 * std::log10((0.1 * 1e-3 + 0.2 * 1e-2) / 0.3);
    for (const float db : learner.trackDb())
        CHECK_THAT(db, WithinAbs(expectedTrack, 1e-4));
    for (const float db : learner.referenceDb())
        CHECK_THAT(db, WithinAbs(expectedReference, 1e-4));

    learner.reset();
    CHECK(learner.empty());
    CHECK(learner.usefulSeconds() == 0.0);
    CHECK(learner.referenceDb() == flat(kSessionFloorDb));
    CHECK(learner.resolutionHz() == 0.0);
}

TEST_CASE("Igualacion: el silencio de cualquiera de las dos no suma segundos")
{
    MatchLearner learner;
    const auto level = flat(-30.0f);
    auto o = observe(level, level, 0.05);
    o.trackHasSignal = false;
    CHECK_FALSE(learner.add(o));
    o.trackHasSignal = true;
    o.referenceHasSignal = false;
    CHECK_FALSE(learner.add(o));
    o.trackHasSignal = false;
    CHECK_FALSE(learner.add(o));
    CHECK(learner.empty());
    const auto revision = learner.revision();
    CHECK(learner.revision() == revision);
}

TEST_CASE("Igualacion: Apply no se activa antes de 10 s utiles")
{
    MatchLearner learner;
    const auto level = flat(-30.0f);
    // A 20 Hz, como publica la sesión: 199 pasos no bastan, 200 sí.
    for (int i = 0; i < 199; ++i)
        REQUIRE(learner.add(observe(level, level, SpectrumSession::kUpdateSeconds)));
    CHECK_FALSE(learner.ready());
    REQUIRE(learner.add(observe(level, level, SpectrumSession::kUpdateSeconds)));
    CHECK(learner.ready());
    CHECK_THAT(learner.usefulSeconds(), WithinAbs(kMatchRequiredSeconds, 1e-9));
}

TEST_CASE("Igualacion: lo que no es una observacion valida no cuenta")
{
    MatchLearner learner;
    const auto level = flat(-30.0f);
    CHECK_FALSE(learner.add(observe(level, level, 0.0)));
    CHECK_FALSE(learner.add(observe(level, level, -1.0)));
    CHECK_FALSE(learner.add(observe(level, level, std::nan(""))));
    const std::vector<float> shorter(kPoints - 1, -30.0f);
    CHECK_FALSE(learner.add(observe(shorter, level, 0.05)));
    auto withNan = level;
    withNan[10] = std::nanf("");
    CHECK_FALSE(learner.add(observe(level, withNan, 0.05)));
    CHECK(learner.empty());

    // Un salto de un segundo (la interfaz se ha parado) pesa como mucho kMaxStepSeconds.
    REQUIRE(learner.add(observe(level, level, 1.0)));
    CHECK(learner.usefulSeconds() == MatchLearner::kMaxStepSeconds);
}

TEST_CASE("Igualacion: la resolucion es la de la mayor frecuencia de muestreo")
{
    MatchLearner learner;
    const auto level = flat(-30.0f);
    auto o = observe(level, level, 0.05);
    o.trackSampleRate = 44100.0;
    o.referenceSampleRate = 96000.0;
    REQUIRE(learner.add(o));
    CHECK(learner.resolutionHz() == 96000.0 / 8192.0);
    o.referenceSampleRate = 0.0; // no se sabe
    REQUIRE(learner.add(o));
    CHECK(learner.resolutionHz() == 96000.0 / 8192.0);
}

// --- Hilo del ajuste ---

TEST_CASE("Igualacion: en manual, solo runPending calcula, y publica la ultima propuesta")
{
    MatchWorker worker(MatchWorker::Threading::Manual);
    CHECK(worker.latest() == nullptr);
    CHECK_FALSE(worker.busy());
    CHECK(worker.lastSubmitted() == 0);

    const auto id = worker.submit(makeRequest());
    CHECK(id == 1);
    CHECK(worker.busy());
    CHECK(worker.latest() == nullptr);
    CHECK(worker.runPending() == 1);
    CHECK_FALSE(worker.busy());
    const auto proposal = worker.latest();
    REQUIRE(proposal != nullptr);
    CHECK(proposal->id == id);
    CHECK_FALSE(proposal->result.cancelled);
    CHECK_FALSE(proposal->result.bands.empty());
    CHECK(worker.runPending() == 0);

    // Lo mismo que dsp::matchCurve con esa entrada, bit a bit.
    const auto request = makeRequest();
    const auto direct = dsp::matchCurve(request.input(), request.settings);
    checkSameBands(proposal->result.bands, direct.bands);
    CHECK(proposal->result.offsetDb == direct.offsetDb);
}

TEST_CASE("Igualacion: dos peticiones seguidas, solo se calcula la ultima")
{
    MatchWorker worker(MatchWorker::Threading::Manual);
    worker.submit(makeRequest(1));
    const auto second = worker.submit(makeRequest(3));
    CHECK(second == 2);
    CHECK(worker.lastSubmitted() == 2);
    CHECK(worker.runPending() == 1);
    REQUIRE(worker.latest() != nullptr);
    CHECK(worker.latest()->id == second);
    CHECK(worker.latest()->result.bands.size() == 3);
}

TEST_CASE("Igualacion: una peticion nueva cancela la anterior en la iteracion siguiente")
{
    std::vector<MatchProposal> finished;
    MatchWorker* self = nullptr;
    std::uint64_t second = 0;
    MatchWorker::Observer observer;
    // Llega una petición nueva justo cuando empieza la primera.
    observer.started = [&](std::uint64_t id)
    {
        if (id == 1)
            second = self->submit(makeRequest());
    };
    observer.finished = [&](const MatchProposal& p) { finished.push_back(p); };
    MatchWorker worker(MatchWorker::Threading::Manual, observer);
    self = &worker;

    worker.submit(makeRequest());
    CHECK(worker.runPending() == 2);
    REQUIRE(finished.size() == 2);
    // La primera vuelve en su primera iteración, sin calcular nada, y no se publica.
    CHECK(finished[0].id == 1);
    CHECK(finished[0].result.cancelled);
    CHECK(finished[0].result.evaluations == 0);
    CHECK(finished[0].result.bands.empty());
    CHECK(finished[1].id == second);
    CHECK_FALSE(finished[1].result.cancelled);
    REQUIRE(worker.latest() != nullptr);
    CHECK(worker.latest()->id == second);
}

TEST_CASE("Igualacion: cancelar conserva la ultima propuesta")
{
    MatchWorker worker(MatchWorker::Threading::Manual);
    worker.submit(makeRequest());
    worker.runPending();
    const auto before = worker.latest();
    REQUIRE(before != nullptr);
    worker.submit(makeRequest(1));
    worker.cancel();
    CHECK_FALSE(worker.busy());
    CHECK(worker.runPending() == 0);
    CHECK(worker.latest() == before);
}

TEST_CASE("Igualacion: el hilo propio calcula y publica")
{
    std::mutex mutex;
    std::condition_variable done;
    std::optional<MatchProposal> result;
    MatchWorker::Observer observer;
    observer.finished = [&](const MatchProposal& p)
    {
        const std::scoped_lock lock(mutex);
        result = p;
        done.notify_all();
    };
    MatchWorker worker(MatchWorker::Threading::Background, observer);
    CHECK(worker.runPending() == 0); // con hilo propio no hace nada
    const auto id = worker.submit(makeRequest());
    {
        std::unique_lock lock(mutex);
        done.wait(lock, [&] { return result.has_value(); });
    }
    CHECK(result->id == id);
    REQUIRE(worker.latest() != nullptr);
    CHECK(worker.latest()->id == id);
    const auto request = makeRequest();
    checkSameBands(worker.latest()->result.bands, dsp::matchCurve(request.input(), request.settings).bands);
}

TEST_CASE("Igualacion: parar el hilo durante un ajuste vuelve tras esa iteracion")
{
    std::mutex mutex;
    std::condition_variable cv;
    bool started = false;
    std::optional<MatchProposal> result;
    MatchWorker::Observer observer;
    MatchWorker* raw = nullptr;
    // Al empezar, avisa y espera a que el objeto empiece a destruirse: así
    // la parada llega con el ajuste en marcha, sin depender de cuánto tarde.
    observer.started = [&](std::uint64_t)
    {
        {
            const std::scoped_lock lock(mutex);
            started = true;
        }
        cv.notify_all();
        while (!raw->stopping())
            std::this_thread::yield();
    };
    observer.finished = [&](const MatchProposal& p) { result = p; };
    auto worker = std::make_unique<MatchWorker>(MatchWorker::Threading::Background, observer);
    raw = worker.get();

    worker->submit(makeRequest(24));
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [&] { return started; });
    }
    worker.reset(); // vuelve cuando el ajuste ve la cancelación
    REQUIRE(result.has_value());
    CHECK(result->result.cancelled);
    CHECK(result->result.iterations == 0);
    CHECK(result->result.evaluations == 0);
}

// --- Cola del sidechain ---

TEST_CASE("Igualacion: la cola del sidechain solo escribe encendida")
{
    MatchSidechainTap tap;
    std::vector<float> left(256, 0.5f), right(256, -0.25f);
    const float* channels[] = {left.data(), right.data()};
    std::vector<float> out(1024);

    CHECK_FALSE(tap.enabled());
    tap.write(channels, 2, 256);
    CHECK(tap.read(out.data(), 1024) == 0);
    tap.write(nullptr, 0, 256); // sin sidechain
    CHECK(tap.read(out.data(), 1024) == 0);

    tap.setEnabled(true);
    CHECK(tap.enabled());
    tap.write(channels, 2, 256);
    tap.write(nullptr, 0, 256);
    REQUIRE(tap.read(out.data(), 1024) == 256);
    CHECK_THAT(out[0], WithinAbs(0.125, 1e-7)); // la mezcla mono

    // Al volver a encenderla, lo que quedara de la vez anterior se descarta.
    tap.write(channels, 2, 256);
    tap.setEnabled(false);
    tap.write(channels, 2, 256);
    tap.setEnabled(true);
    CHECK(tap.read(out.data(), 1024) == 0);
}
