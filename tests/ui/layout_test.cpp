// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/layout.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

using namespace esceq::ui;

namespace {

using Named = std::pair<std::string, Rect>;

bool inside(const Rect& inner, const Rect& outer)
{
    constexpr float e = 0.01f;
    return inner.x >= outer.x - e && inner.y >= outer.y - e && inner.x + inner.width <= outer.x + outer.width + e &&
           inner.y + inner.height <= outer.y + outer.height + e;
}

bool overlap(const Rect& a, const Rect& b)
{
    constexpr float e = 0.01f;
    return a.x + a.width > b.x + e && b.x + b.width > a.x + e && a.y + a.height > b.y + e && b.y + b.height > a.y + e;
}

/// Todos dentro de `bounds`, con tamano positivo y sin solaparse.
void checkDisjointInside(const std::vector<Named>& rects, const Rect& bounds)
{
    for (std::size_t i = 0; i < rects.size(); ++i)
    {
        INFO(rects[i].first);
        CHECK(rects[i].second.width > 0.0f);
        CHECK(rects[i].second.height > 0.0f);
        CHECK(inside(rects[i].second, bounds));
        for (std::size_t j = i + 1; j < rects.size(); ++j)
        {
            INFO("contra " << rects[j].first);
            CHECK_FALSE(overlap(rects[i].second, rects[j].second));
        }
    }
}

/// Tamanos de ventana a comprobar: los limites, los predefinidos y un barrido.
std::vector<WindowSize> sizesToCheck()
{
    std::vector<WindowSize> sizes{kMinWindow, kMaxWindow, {kMinWindow.width, kMaxWindow.height},
                                  {kMaxWindow.width, kMinWindow.height}};
    for (int i = 0; i < kNumSizePresets; ++i)
        sizes.push_back(windowSize(static_cast<SizePreset>(i)));
    for (int width = kMinWindow.width; width <= kMaxWindow.width; width += 37)
        sizes.push_back({width, kMinWindow.height + (width * 7) % (kMaxWindow.height - kMinWindow.height)});
    return sizes;
}

} // namespace

TEST_CASE("Disposicion: tamanos predefinidos dentro de los limites y distintos")
{
    WindowSize previous{0, 0};
    for (int i = 0; i < kNumSizePresets; ++i)
    {
        const auto preset = static_cast<SizePreset>(i);
        const auto size = windowSize(preset);
        CHECK(clampWindow(size) == size);
        CHECK(size.width > previous.width);
        CHECK(size.height > previous.height);
        CHECK(matchingPreset(size) == preset);
        previous = size;
    }
    CHECK_FALSE(matchingPreset({1001, 601}).has_value());
    CHECK(clampWindow({10, 10}) == kMinWindow);
    CHECK(clampWindow({99999, 99999}) == kMaxWindow);
    CHECK(clampWindow({1200, 99999}) == WindowSize{1200, kMaxWindow.height});
}

TEST_CASE("Disposicion: las zonas de la ventana no se solapan en ningun tamano")
{
    for (const auto size : sizesToCheck())
    {
        INFO(size.width << " x " << size.height);
        const Rect window{0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height)};
        const auto layout = editorLayout(window.width, window.height);
        checkDisjointInside({{"barra", layout.topBar}, {"curva", layout.curve}, {"medidor", layout.meter}, {"panel", layout.panel}},
                            window);
        // La curva se queda con casi todo el alto.
        CHECK(layout.curve.height >= 300.0f);
        CHECK(layout.curve.width >= 800.0f);
    }
}

TEST_CASE("Disposicion: la barra superior cabe entera y no se solapa en ningun tamano")
{
    for (const auto size : sizesToCheck())
    {
        INFO(size.width << " x " << size.height);
        const auto editor = editorLayout(static_cast<float>(size.width), static_cast<float>(size.height));
        const auto bar = topBarLayout(editor.topBar);
        checkDisjointInside({{"logo", bar.logo},
                             {"presets", bar.presets},
                             {"deshacer", bar.undo},
                             {"rehacer", bar.redo},
                             {"comparar", bar.compare},
                             {"opciones", bar.options},
                             {"fase", bar.phase},
                             {"analizador", bar.analyzer},
                             {"rango", bar.range},
                             {"salida", bar.output},
                             {"valor de salida", bar.outputValue},
                             {"compensacion", bar.autoGain},
                             {"bypass", bar.bypass}},
                            editor.topBar);
        CHECK(bar.presets.width >= 140.0f); // sitio para el nombre de un preset
        CHECK(bar.presets.width <= 320.0f);
        // Los botones de icono son cuadrados y del mismo tamaño.
        CHECK(bar.compare.width == bar.undo.width);
        CHECK(bar.compare.width == bar.compare.height);
        CHECK(bar.options.width == bar.compare.width);
        CHECK(bar.options.height == bar.compare.height);
        // La compensación, junto al bypass y de su misma altura.
        CHECK(bar.autoGain.width >= 92.0f);
        CHECK(bar.autoGain.height == bar.bypass.height);
        CHECK(bar.autoGain.x > bar.outputValue.x);
        CHECK(bar.autoGain.x < bar.bypass.x);
    }
}

TEST_CASE("Disposicion: el panel de banda no solapa controles ni corta textos en ningun tamano")
{
    for (const auto size : sizesToCheck())
    {
        INFO(size.width << " x " << size.height);
        const auto editor = editorLayout(static_cast<float>(size.width), static_cast<float>(size.height));
        const auto panel = panelLayout(editor.panel);

        // Las tres tarjetas, dentro del panel y sin tocarse.
        checkDisjointInside({{"banda", panel.bandCard}, {"eq", panel.eqCard}, {"dinamica", panel.dynamicsCard}}, editor.panel);

        // Lo de dentro de cada tarjeta, dentro de ella y sin solaparse.
        checkDisjointInside({{"titulo", panel.bandTitle},
                             {"on", panel.enabled},
                             {"tipo", panel.type},
                             {"pendiente", panel.slope},
                             {"canal", panel.channel}},
                            panel.bandCard);

        std::vector<Named> eq{{"titulo eq", panel.eqTitle}};
        for (std::size_t i = 0; i < panel.eqKnobs.size(); ++i)
            eq.emplace_back("celda eq " + std::to_string(i), panel.eqKnobs[i]);
        checkDisjointInside(eq, panel.eqCard);

        std::vector<Named> dynamics{{"titulo dinamica", panel.dynamicsTitle},
                                    {"interruptor", panel.dynamicsEnabled},
                                    {"fuente", panel.source},
                                    {"aviso", panel.sourceWarning},
                                    {"medidor", panel.dynamicsMeter}};
        for (std::size_t i = 0; i < panel.dynamicKnobs.size(); ++i)
            dynamics.emplace_back("celda dinamica " + std::to_string(i), panel.dynamicKnobs[i]);
        checkDisjointInside(dynamics, panel.dynamicsCard);

        // Cada celda de potenciometro tiene sitio para su etiqueta y su valor,
        // y las ocho miden lo mismo (con un pixel de margen por el redondeo).
        const float reference = panel.eqKnobs[0].width;
        auto checkCell = [&](const Rect& cell) {
            CHECK(cell.width >= kMinKnobCellWidth);
            CHECK(cell.width - reference < 1.0f);
            CHECK(reference - cell.width < 1.0f);
            const auto parts = knobCellLayout(cell);
            checkDisjointInside({{"etiqueta", parts.label}, {"potenciometro", parts.knob}, {"valor", parts.value}}, cell);
            CHECK(parts.knob.width == parts.knob.height);
            CHECK(parts.knob.width >= kMinKnobDiameter);
            CHECK(parts.knob.width <= kMaxKnobDiameter);
            CHECK(parts.label.width == cell.width); // el texto dispone de todo el ancho de la celda
            CHECK(parts.value.width == cell.width);
        };
        for (const auto& cell : panel.eqKnobs)
            checkCell(cell);
        for (const auto& cell : panel.dynamicKnobs)
            checkCell(cell);

        // Los desplegables tienen ancho para su opcion mas larga ("High Shelf", "12 dB/oct", "External").
        CHECK(panel.type.width >= 120.0f);
        CHECK(panel.slope.width >= 120.0f);
        CHECK(panel.channel.width >= 150.0f); // cinco segmentos de 30 px
        CHECK(panel.source.width >= 96.0f);
    }
}

TEST_CASE("Disposicion: tamanos absurdos no producen medidas negativas")
{
    for (const auto& [w, h] : {std::pair{0.0f, 0.0f}, {50.0f, 30.0f}, {-10.0f, 400.0f}})
    {
        const auto editor = editorLayout(w, h);
        const auto panel = panelLayout(editor.panel);
        const auto bar = topBarLayout(editor.topBar);
        for (const Rect& r : {editor.topBar, editor.curve, editor.meter, editor.panel, panel.bandCard, panel.eqCard,
                              panel.dynamicsCard, panel.type, panel.eqKnobs[0], panel.dynamicKnobs[4], panel.dynamicsMeter,
                              bar.logo, bar.presets, bar.phase, bar.bypass})
        {
            CHECK(r.width >= 0.0f);
            CHECK(r.height >= 0.0f);
        }
    }
}
