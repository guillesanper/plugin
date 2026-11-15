// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/spectrum_session.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>
#include <vector>

using namespace esceq::core;
using Catch::Matchers::WithinAbs;

namespace
{

using Threading = SpectrumSession::Threading;

/// Un bloque estéreo de un seno, con la fase continua entre llamadas.
struct SineSource
{
    double hz = 1000.0;
    double sampleRate = 48000.0;
    float amplitude = 0.5f;
    double phase = 0.0;
    std::vector<float> left = std::vector<float>(512);
    std::vector<float> right = std::vector<float>(512);

    void feed(SpectrumSession::Member& member, int blocks = 1)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (std::size_t i = 0; i < left.size(); ++i)
            {
                left[i] = right[i] = amplitude * static_cast<float>(std::sin(phase));
                phase += 2.0 * std::numbers::pi * hz / sampleRate;
            }
            const float* channels[] = {left.data(), right.data()};
            member.feed(channels, 2, static_cast<int>(left.size()));
        }
    }
};

const InstanceSpectrum* find(const std::vector<InstanceSpectrum>& all, InstanceId id)
{
    const auto it = std::find_if(all.begin(), all.end(), [id](const auto& s) { return s.id == id; });
    return it == all.end() ? nullptr : &*it;
}

/// Frecuencia del punto más alto del espectro.
double peakFrequency(const InstanceSpectrum& s)
{
    const auto it = std::max_element(s.levelsDb.begin(), s.levelsDb.end());
    return sessionFrequencies()[static_cast<std::size_t>(it - s.levelsDb.begin())];
}

float peakLevel(const InstanceSpectrum& s)
{
    return *std::max_element(s.levelsDb.begin(), s.levelsDb.end());
}

} // namespace

TEST_CASE("Sesion: rejilla de frecuencias comun")
{
    const auto& f = sessionFrequencies();
    REQUIRE(f.size() == static_cast<std::size_t>(kSessionPoints));
    CHECK_THAT(f.front(), WithinAbs(kSessionMinHz, 1e-9));
    CHECK_THAT(f.back(), WithinAbs(kSessionMaxHz, 1e-6));
    CHECK(std::is_sorted(f.begin(), f.end()));
    // Logarítmica: la razón entre puntos consecutivos es constante.
    CHECK_THAT(f[1] / f[0], WithinAbs(f[f.size() - 1] / f[f.size() - 2], 1e-9));
    CHECK(&sessionFrequencies() == &f);
}

TEST_CASE("Sesion: apuntarse y salir")
{
    auto session = SpectrumSession::create(Threading::Manual);
    CHECK(session->size() == 0);

    auto a = session->join();
    auto b = session->join();
    auto c = session->join();
    CHECK(session->size() == 3);
    CHECK(a->id() != b->id());
    CHECK(b->id() != c->id());

    std::vector<InstanceSpectrum> all;
    session->snapshot(all);
    REQUIRE(all.size() == 3);
    CHECK(all[0].number == 1);
    CHECK(all[1].number == 2);
    CHECK(all[2].number == 3);
    CHECK(all[0].levelsDb.size() == static_cast<std::size_t>(kSessionPoints));
    CHECK_FALSE(all[0].hasSignal);

    SECTION("al salir, el numero queda libre y el identificador no se reutiliza")
    {
        const auto oldId = b->id();
        b.reset();
        CHECK(session->size() == 2);
        auto d = session->join();
        session->snapshot(all);
        const auto* entry = find(all, d->id());
        REQUIRE(entry != nullptr);
        CHECK(entry->number == 2);
        CHECK(d->id() != oldId);
        CHECK(find(all, oldId) == nullptr);
    }

    SECTION("snapshot reutiliza el vector y se encoge si salen instancias")
    {
        a.reset();
        c.reset();
        session->snapshot(all);
        REQUIRE(all.size() == 1);
        CHECK(all[0].id == b->id());
    }
}

TEST_CASE("Sesion: la sesion vive mientras viva un Member o un Viewer")
{
    std::weak_ptr<SpectrumSession> weak;
    std::unique_ptr<SpectrumSession::Member> member;
    SpectrumSession::Viewer viewer;
    {
        auto session = SpectrumSession::create(Threading::Background);
        weak = session;
        member = session->join();
        viewer = session->watch();
    }
    CHECK_FALSE(weak.expired());
    member.reset();
    CHECK_FALSE(weak.expired());
    viewer.release();
    CHECK(weak.expired()); // y el hilo se ha parado sin bloquear
}

TEST_CASE("Sesion: la del proceso es unica mientras alguien la usa")
{
    auto first = SpectrumSession::shared();
    auto second = SpectrumSession::shared();
    CHECK(first == second);

    std::weak_ptr<SpectrumSession> weak = first;
    first.reset();
    second.reset();
    CHECK(weak.expired());

    auto third = SpectrumSession::shared();
    CHECK(third != nullptr);
    CHECK(third->size() == 0);
}

TEST_CASE("Sesion: nombre y color de la pista")
{
    auto session = SpectrumSession::create(Threading::Manual);
    auto member = session->join();
    std::vector<InstanceSpectrum> all;

    session->snapshot(all);
    CHECK(all[0].displayName() == "esceQ 1");
    CHECK_FALSE(all[0].trackColour.has_value());

    auto revision = session->revision();
    member->setTrackName("Bass");
    CHECK(session->revision() != revision);
    member->setTrackColour(0xff112233u);
    session->snapshot(all);
    CHECK(all[0].trackName == "Bass");
    CHECK(all[0].displayName() == "Bass");
    CHECK(all[0].trackColour == 0xff112233u);

    SECTION("el mismo valor no cambia la revision")
    {
        revision = session->revision();
        member->setTrackName("Bass");
        member->setTrackColour(0xff112233u);
        CHECK(session->revision() == revision);
    }

    SECTION("un nombre vacio vuelve al numero")
    {
        member->setTrackName("");
        member->setTrackColour(std::nullopt);
        session->snapshot(all);
        CHECK(all[0].displayName() == "esceQ 1");
        CHECK_FALSE(all[0].trackColour.has_value());
    }
}

TEST_CASE("Sesion: entrar y salir cambia la revision")
{
    auto session = SpectrumSession::create(Threading::Manual);
    const auto r0 = session->revision();
    auto member = session->join();
    const auto r1 = session->revision();
    CHECK(r1 != r0);
    member.reset();
    CHECK(session->revision() != r1);
}

TEST_CASE("Sesion: sin nadie mirando no se procesa nada")
{
    auto session = SpectrumSession::create(Threading::Manual);
    auto member = session->join();
    member->setSampleRate(48000.0);
    CHECK_FALSE(session->watched());

    SineSource sine;
    sine.feed(*member, 20);
    const auto revision = session->revision();
    session->update(0.05);
    CHECK(session->revision() == revision);

    // Al empezar a mirar, no aparece nada de lo que se entregó antes.
    auto viewer = session->watch();
    session->update(0.05);
    std::vector<InstanceSpectrum> all;
    session->snapshot(all);
    CHECK(peakLevel(all[0]) == kSessionFloorDb);
    CHECK_FALSE(all[0].hasSignal);
}

TEST_CASE("Sesion: el espectro publicado refleja el audio")
{
    auto session = SpectrumSession::create(Threading::Manual);
    auto low = session->join();
    auto high = session->join();
    low->setSampleRate(48000.0);
    high->setSampleRate(48000.0);
    auto viewer = session->watch();
    session->update(0.05); // descarta lo anterior

    SineSource lowSine{.hz = 100.0};
    SineSource highSine{.hz = 4000.0};
    // 2 s: el promedio (0,4 s) ha olvidado los primeros espectros, con la ventana a medio llenar.
    for (int i = 0; i < 40; ++i)
    {
        lowSine.feed(*low, 5);
        highSine.feed(*high, 5);
        session->update(0.05);
    }

    std::vector<InstanceSpectrum> all;
    session->snapshot(all);
    const auto* l = find(all, low->id());
    const auto* h = find(all, high->id());
    REQUIRE(l != nullptr);
    REQUIRE(h != nullptr);
    CHECK(l->hasSignal);
    CHECK(h->hasSignal);
    CHECK(std::abs(std::log2(peakFrequency(*l) / 100.0)) < 0.1);
    CHECK(std::abs(std::log2(peakFrequency(*h) / 4000.0)) < 0.1);

    // Calibración: seno de amplitud 0,5 (-6 dB) más la inclinación de 4,5 dB/oct respecto a 1 kHz.
    const double expected = 20.0 * std::log10(0.5) + SpectrumSession::kTiltDbPerOctave * std::log2(4000.0 / 1000.0);
    CHECK_THAT(peakLevel(*h), WithinAbs(expected, 1.5));

    SECTION("sin audio, deja de tener senal y el espectro se apaga poco a poco")
    {
        const float before = peakLevel(*h);
        session->update(0.3); // dentro del tiempo de espera: sigue igual
        session->snapshot(all);
        CHECK(find(all, high->id())->hasSignal);
        CHECK(peakLevel(*find(all, high->id())) == before);

        session->update(0.3);
        session->snapshot(all);
        h = find(all, high->id());
        CHECK_FALSE(h->hasSignal);
        CHECK(peakLevel(*h) < before);
        CHECK(peakLevel(*h) > kSessionFloorDb);

        for (int i = 0; i < 200; ++i)
            session->update(0.05);
        session->snapshot(all);
        CHECK(peakLevel(*find(all, high->id())) < -100.0f);
    }

    SECTION("al dejar de mirar y volver, empieza de cero")
    {
        viewer.release();
        CHECK_FALSE(session->watched());
        viewer = session->watch();
        session->update(0.05);
        session->snapshot(all);
        CHECK(peakLevel(*find(all, low->id())) == kSessionFloorDb);
        CHECK_FALSE(find(all, low->id())->hasSignal);
    }
}

TEST_CASE("Sesion: entradas que no aportan nada")
{
    auto session = SpectrumSession::create(Threading::Manual);
    auto member = session->join();
    auto viewer = session->watch();
    session->update(0.05);

    member->feed(nullptr, 0, 512);
    member->feed(nullptr, 2, 0);
    member->setSampleRate(0.0);  // se ignora
    member->setSampleRate(-1.0); // se ignora

    // Sin frecuencia de muestreo conocida se asume 48 kHz.
    SineSource sine{.hz = 1000.0};
    sine.feed(*member, 20);
    session->update(0.05);
    std::vector<InstanceSpectrum> all;
    session->snapshot(all);
    CHECK(std::abs(std::log2(peakFrequency(all[0]) / 1000.0)) < 0.1);

    SECTION("el silencio digital no cuenta como senal")
    {
        SineSource silence{.amplitude = 0.0f};
        for (int i = 0; i < 100; ++i)
        {
            silence.feed(*member, 5);
            session->update(0.05);
        }
        session->snapshot(all);
        CHECK_FALSE(all[0].hasSignal);
    }
}

TEST_CASE("Sesion: varios Viewer")
{
    auto session = SpectrumSession::create(Threading::Manual);
    SpectrumSession::Viewer empty;
    CHECK_FALSE(empty.active());
    empty.release(); // no hace nada

    auto a = session->watch();
    auto b = session->watch();
    CHECK(a.active());
    CHECK(session->watched());

    SpectrumSession::Viewer moved = std::move(a);
    CHECK(moved.active());
    CHECK_FALSE(a.active()); // NOLINT(bugprone-use-after-move): se comprueba a propósito
    moved.release();
    CHECK(session->watched()); // queda b
    b = SpectrumSession::Viewer{};
    CHECK_FALSE(session->watched());
}

TEST_CASE("Sesion: el trabajador en segundo plano publica los espectros")
{
    auto session = SpectrumSession::create(Threading::Background);
    auto member = session->join();
    member->setSampleRate(48000.0);
    auto viewer = session->watch();

    // Un hilo hace de hilo de audio, a ritmo real aproximado.
    std::atomic<bool> stop{false};
    std::thread audio(
        [&]
        {
            SineSource sine{.hz = 250.0};
            while (!stop.load())
            {
                sine.feed(*member);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

    std::vector<InstanceSpectrum> all;
    bool found = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!found && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        session->snapshot(all);
        found = all.size() == 1 && all[0].hasSignal && std::abs(std::log2(peakFrequency(all[0]) / 250.0)) < 0.1;
    }
    stop = true;
    audio.join();
    CHECK(found);
}

TEST_CASE("Sesion: hilos a la vez (para ThreadSanitizer)")
{
    auto session = SpectrumSession::create(Threading::Background);
    auto fixed = session->join();
    std::atomic<bool> stop{false};

    std::thread audio(
        [&]
        {
            SineSource sine;
            while (!stop.load())
                sine.feed(*fixed);
        });
    std::thread host(
        [&]
        {
            int n = 0;
            while (!stop.load())
            {
                auto member = session->join();
                member->setTrackName("Track " + std::to_string(n++));
                member->setTrackColour(static_cast<std::uint32_t>(n));
                member->setSampleRate(44100.0);
            }
        });
    std::thread ui(
        [&]
        {
            std::vector<InstanceSpectrum> all;
            while (!stop.load())
            {
                auto viewer = session->watch();
                session->snapshot(all);
                (void)session->revision();
            }
        });

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop = true;
    audio.join();
    host.join();
    ui.join();
    CHECK(session->size() == 1);
}

TEST_CASE("Sesion: seleccion de instancias a comparar")
{
    CompareSelection selection;
    CHECK(selection.showCollisions);
    CHECK_FALSE(selection.contains(3));

    selection.toggle(3);
    selection.toggle(5);
    CHECK(selection.contains(3));
    CHECK(selection.contains(5));
    CHECK(selection.shown == std::vector<InstanceId>{3, 5});

    selection.toggle(3);
    CHECK_FALSE(selection.contains(3));
    CHECK(selection.shown == std::vector<InstanceId>{5});

    std::vector<InstanceSpectrum> existing(2);
    existing[0].id = 5;
    existing[1].id = 9;
    CHECK_FALSE(selection.retainExisting(existing));
    selection.toggle(7);
    CHECK(selection.retainExisting(existing));
    CHECK(selection.shown == std::vector<InstanceId>{5});
    CHECK(selection.retainExisting({}));
    CHECK(selection.shown.empty());
}
