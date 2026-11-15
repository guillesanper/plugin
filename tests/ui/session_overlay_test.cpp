// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/session_overlay.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace esceq;
using namespace esceq::ui;

namespace
{

core::InstanceSpectrum instance(core::InstanceId id, int number, std::string name = {}, bool signal = true)
{
    core::InstanceSpectrum s;
    s.id = id;
    s.number = number;
    s.trackName = std::move(name);
    s.hasSignal = signal;
    return s;
}

std::vector<float> plateau(std::span<const double> f, double lo, double hi, float level, float floor = -120.0f)
{
    std::vector<float> out(f.size(), floor);
    for (std::size_t i = 0; i < f.size(); ++i)
        if (f[i] >= lo && f[i] <= hi)
            out[i] = level;
    return out;
}

} // namespace

TEST_CASE("Comparar: entradas del menu")
{
    std::vector<core::InstanceSpectrum> all{instance(10, 3, "Vocals"), instance(4, 1), instance(7, 2, "Bass", false)};
    core::CompareSelection selection;
    selection.toggle(10);

    const auto items = compareMenuItems(all, 4, selection);
    REQUIRE(items.size() == 2);
    // Sin la propia, por número de instancia.
    CHECK(items[0] == CompareMenuItem{.id = 7, .name = "Bass", .ticked = false, .hasSignal = false});
    CHECK(items[1] == CompareMenuItem{.id = 10, .name = "Vocals", .ticked = true, .hasSignal = true});

    CHECK(compareMenuItems(all, 99, selection).size() == 3);
    CHECK(compareMenuItems(all, 99, selection)[0].name == "esceQ 1");
    CHECK(compareMenuItems({}, 4, selection).empty());
}

TEST_CASE("Comparar: contador del boton")
{
    core::CompareSelection selection;
    CHECK(compareBadge(selection).empty());
    selection.toggle(1);
    selection.toggle(2);
    CHECK(compareBadge(selection) == "2");
}

TEST_CASE("Comparar: colisiones con varias instancias")
{
    const auto& f = core::sessionFrequencies();
    const auto own = plateau(f, 60.0, 4000.0, -12.0f);

    SECTION("sin otras instancias no hay nada")
    {
        const auto overlay = computeCollisions(f, own, {});
        CHECK(overlay.severity.size() == f.size());
        CHECK(*std::max_element(overlay.severity.begin(), overlay.severity.end()) == 0.0f);
        CHECK(std::all_of(overlay.culprit.begin(), overlay.culprit.end(), [](int c) { return c == -1; }));
        CHECK(overlay.zones.empty());
    }

    SECTION("cada zona se atribuye a la instancia que la pisa")
    {
        const auto bass = plateau(f, 60.0, 200.0, -6.0f);
        const auto keys = plateau(f, 1000.0, 3000.0, -6.0f);
        const auto hats = plateau(f, 8000.0, 16000.0, -6.0f); // fuera de la propia
        const std::vector<std::span<const float>> others{bass, keys, hats};

        const auto overlay = computeCollisions(f, own, others);
        REQUIRE(overlay.zones.size() == 2);
        for (const auto& zone : overlay.zones)
        {
            if (zone.region.peakHz < 500.0)
                CHECK(zone.culprit == 0);
            else
                CHECK(zone.culprit == 1);
            CHECK(zone.region.peakSeverity > 0.9f);
        }
        // Donde solo suenan los platos, no hay colisión.
        for (std::size_t i = 0; i < f.size(); ++i)
            if (f[i] > 8000.0)
                CHECK(overlay.severity[i] == 0.0f);
        // El culpable por punto coincide.
        const auto at = [&](double hz)
        {
            const auto it = std::lower_bound(f.begin(), f.end(), hz);
            return overlay.culprit[static_cast<std::size_t>(it - f.begin())];
        };
        CHECK(at(100.0) == 0);
        CHECK(at(2000.0) == 1);
    }

    SECTION("una zona ancha pesa mas que un pico estrecho")
    {
        const auto wide = plateau(f, 300.0, 1200.0, -6.0f);
        const auto narrow = plateau(f, 590.0, 610.0, 0.0f); // más alta, pero estrecha
        const std::vector<std::span<const float>> others{narrow, wide};
        const auto overlay = computeCollisions(f, own, others);
        REQUIRE(overlay.zones.size() == 2);
        CHECK(overlay.zones[0].culprit == 1);
        CHECK(overlay.zones[1].culprit == 0);
        CHECK(overlay.zones[0].weight > overlay.zones[1].weight);
        // El peso es la severidad integrada en octavas: unas dos octavas a severidad 1.
        CHECK(overlay.zones[0].weight > 1.5);
        CHECK(overlay.zones[0].weight < 2.5);
    }

    SECTION("dos colisiones contiguas con pistas distintas no se funden")
    {
        // Como un bajo y unos teclados que se tocan en 300 Hz: cada zona lleva
        // el nombre de su pista y su pico dentro de ella.
        const auto bass = plateau(f, 60.0, 300.0, -6.0f);
        const auto keys = plateau(f, 300.0, 3000.0, -6.0f);
        const std::vector<std::span<const float>> others{bass, keys};
        const auto overlay = computeCollisions(f, own, others);
        REQUIRE(overlay.zones.size() == 2);
        for (const auto& zone : overlay.zones)
        {
            const auto& source = zone.culprit == 0 ? bass : keys;
            const auto it = std::lower_bound(f.begin(), f.end(), zone.region.peakHz);
            CHECK(source[static_cast<std::size_t>(it - f.begin())] == -6.0f);
        }
        CHECK(overlay.zones[0].culprit == 1); // la de los teclados es más ancha
    }

    SECTION("espectros de otro tamano se ignoran")
    {
        const std::vector<float> wrong(5, 0.0f);
        const std::vector<std::span<const float>> others{wrong};
        const auto overlay = computeCollisions(f, own, others);
        CHECK(overlay.zones.empty());
        CHECK(*std::max_element(overlay.severity.begin(), overlay.severity.end()) == 0.0f);

        const std::vector<float> shortOwn(3, 0.0f);
        CHECK(computeCollisions(f, shortOwn, others).zones.empty());
    }

    SECTION("el umbral de los tramos se puede ajustar")
    {
        const auto other = plateau(f, 60.0, 4000.0, -12.0f - 9.0f); // enmascara a medias
        const std::vector<std::span<const float>> others{other};
        CollisionViewSettings strict;
        strict.regionThreshold = 0.95f;
        CHECK(computeCollisions(f, own, others, strict).zones.empty());
        CollisionViewSettings loose;
        loose.regionThreshold = 0.05f;
        CHECK_FALSE(computeCollisions(f, own, others, loose).zones.empty());
    }
}

TEST_CASE("Comparar: texto de las etiquetas")
{
    CHECK(collisionLabel("Bass", 120.0) == "Bass \xC2\xB7 120 Hz");
    CHECK(collisionLabel("Vocals", 2500.0) == "Vocals \xC2\xB7 2.50 kHz");
    CHECK(collisionLabel("esceQ 2", 45.0) == "esceQ 2 \xC2\xB7 45.0 Hz");
}

TEST_CASE("Comparar: colocacion de las etiquetas")
{
    const Rect area{10.0f, 0.0f, 500.0f, 300.0f};

    SECTION("etiquetas separadas se colocan centradas")
    {
        const std::vector<float> xs{100.0f, 300.0f};
        const std::vector<float> ws{60.0f, 60.0f};
        const auto placed = placeLabels(xs, ws, area, 8.0f, 20.0f, 4.0f);
        REQUIRE(placed.size() == 2);
        REQUIRE(placed[0]);
        REQUIRE(placed[1]);
        CHECK(placed[0]->x == 70.0f);
        CHECK(placed[0]->y == 8.0f);
        CHECK(placed[0]->height == 20.0f);
        CHECK(placed[1]->x == 270.0f);
    }

    SECTION("si se solapan, gana la primera")
    {
        const std::vector<float> xs{200.0f, 230.0f, 400.0f};
        const std::vector<float> ws{80.0f, 80.0f, 80.0f};
        const auto placed = placeLabels(xs, ws, area, 0.0f, 20.0f, 4.0f);
        CHECK(placed[0].has_value());
        CHECK_FALSE(placed[1].has_value());
        CHECK(placed[2].has_value());
    }

    SECTION("el margen cuenta como solape")
    {
        const std::vector<float> xs{100.0f, 162.0f};
        const std::vector<float> ws{60.0f, 60.0f};
        CHECK_FALSE(placeLabels(xs, ws, area, 0.0f, 20.0f, 4.0f)[1].has_value());
        CHECK(placeLabels(xs, ws, area, 0.0f, 20.0f, 0.0f)[1].has_value());
    }

    SECTION("se quedan dentro del area")
    {
        const std::vector<float> xs{0.0f, 600.0f};
        const std::vector<float> ws{60.0f, 60.0f};
        const auto placed = placeLabels(xs, ws, area, 0.0f, 20.0f, 4.0f);
        REQUIRE(placed[0]);
        REQUIRE(placed[1]);
        CHECK(placed[0]->x == area.x);
        CHECK(placed[1]->x + placed[1]->width == area.x + area.width);
    }

    SECTION("una etiqueta que no cabe, o sin ancho, no se coloca")
    {
        const std::vector<float> xs{100.0f, 200.0f};
        const std::vector<float> ws{600.0f, 0.0f};
        const auto placed = placeLabels(xs, ws, area, 0.0f, 20.0f, 4.0f);
        CHECK_FALSE(placed[0].has_value());
        CHECK_FALSE(placed[1].has_value());
        CHECK(placeLabels({}, {}, area, 0.0f, 20.0f, 4.0f).empty());
    }
}
