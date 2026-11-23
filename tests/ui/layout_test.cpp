// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/layout.h"
#include "esceq/ui/notes.h"

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
        for (const bool open : {true, false})
        {
            INFO("cajon abierto: " << open);
            const auto layout = editorLayout(window.width, window.height, false, false, open);
            checkDisjointInside({{"barra", layout.topBar},
                                 {"curva", layout.curve},
                                 {"cajon", layout.drawer},
                                 {"medidor", layout.meter},
                                 {"panel", layout.panel}},
                                window);
            // La curva se queda con casi todo el alto.
            CHECK(layout.curve.height >= 300.0f);
            CHECK(layout.curve.width >= 800.0f);
        }
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
                             {"ampliar", bar.expand},
                             {"rango", bar.range},
                             {"bypass", bar.bypass}},
                            editor.topBar);
        // La salida y Auto pasaron al cajon de la etapa de salida (ADR 0027):
        // el navegador de presets tiene su ancho maximo en todos los tamanos.
        CHECK(bar.presets.width == 320.0f);
        // Los botones de icono son cuadrados y del mismo tamaño.
        CHECK(bar.compare.width == bar.undo.width);
        CHECK(bar.compare.width == bar.compare.height);
        CHECK(bar.options.width == bar.compare.width);
        CHECK(bar.options.height == bar.compare.height);
        // Los controles de la derecha siguen pegados al bypass.
        CHECK(bar.range.x + bar.range.width + 12.0f == bar.bypass.x);
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
                                    {"tiempos automaticos", panel.autoTiming},
                                    {"espectral", panel.spectral},
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
        // El interruptor de los tiempos automáticos, entero, con su texto ("Auto").
        CHECK(panel.autoTiming.height == 24.0f);
        CHECK(panel.autoTiming.width >= 62.0f); // 28 del interruptor, 8 de separacion y 26 de "Auto"
        // A su derecha, en la misma fila, el de la dinamica espectral ("Spectral", 47 px).
        CHECK(panel.spectral.height == 24.0f);
        CHECK(panel.spectral.y == panel.autoTiming.y);
        CHECK(panel.spectral.x >= panel.autoTiming.x + panel.autoTiming.width + 8.0f);
        CHECK(panel.spectral.width >= 83.0f);
    }
}

namespace {

bool same(const Rect& a, const Rect& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

} // namespace

TEST_CASE("Disposicion: sin el teclado de notas, la de siempre, pixel a pixel")
{
    // Medidas de la ventana mediana antes de la escala de notas (PLAN.md 5.1),
    // con el cajon de la etapa de salida (ADR 0027) entre la curva y el
    // medidor: abierto, la curva cede 168 px (160 y la separacion); plegado, 24.
    const auto medium = editorLayout(1180.0f, 700.0f);
    CHECK(same(medium.topBar, {0.0f, 0.0f, 1180.0f, 48.0f}));
    CHECK(same(medium.curve, {12.0f, 48.0f, 928.0f, 484.0f}));
    CHECK(same(medium.drawer, {948.0f, 48.0f, 160.0f, 484.0f}));
    CHECK(same(medium.meter, {1116.0f, 48.0f, 52.0f, 484.0f}));
    CHECK(same(medium.panel, {0.0f, 532.0f, 1180.0f, 168.0f}));
    CHECK(same(medium.keyboard, {}));
    const auto folded = editorLayout(1180.0f, 700.0f, false, false, false);
    CHECK(same(folded.curve, {12.0f, 48.0f, 1072.0f, 484.0f}));
    CHECK(same(folded.drawer, {1092.0f, 48.0f, 16.0f, 484.0f}));
    CHECK(same(folded.meter, medium.meter));
    CHECK(same(folded.panel, medium.panel));

    for (const auto size : sizesToCheck())
    {
        INFO(size.width << " x " << size.height);
        const auto w = static_cast<float>(size.width);
        const auto h = static_cast<float>(size.height);
        const auto hidden = editorLayout(w, h, false);
        const auto byDefault = editorLayout(w, h);
        CHECK(same(hidden.curve, byDefault.curve));
        CHECK(same(hidden.meter, byDefault.meter));
        CHECK(same(hidden.panel, byDefault.panel));
        CHECK(same(hidden.keyboard, {}));
        // La curva llega hasta el panel, como antes.
        CHECK(hidden.curve.y + hidden.curve.height == hidden.panel.y);
    }
}

TEST_CASE("Disposicion: el teclado de notas va bajo la curva, con su mismo ancho")
{
    for (const auto size : sizesToCheck())
    {
        INFO(size.width << " x " << size.height);
        const auto w = static_cast<float>(size.width);
        const auto h = static_cast<float>(size.height);
        const auto hidden = editorLayout(w, h, false);
        const auto shown = editorLayout(w, h, true);
        const Rect window{0.0f, 0.0f, w, h};
        checkDisjointInside({{"barra", shown.topBar},
                             {"curva", shown.curve},
                             {"teclado", shown.keyboard},
                             {"medidor", shown.meter},
                             {"panel", shown.panel}},
                            window);

        CHECK(shown.keyboard.height == kPianoKeyboardHeight);
        CHECK(shown.keyboard.x == shown.curve.x);
        CHECK(shown.keyboard.width == shown.curve.width);
        CHECK(shown.keyboard.y > shown.curve.y + shown.curve.height); // con una separacion
        CHECK(shown.keyboard.y + shown.keyboard.height == shown.panel.y);
        // Solo la curva cede su alto; lo demas no se mueve.
        CHECK(same(shown.topBar, hidden.topBar));
        CHECK(same(shown.meter, hidden.meter));
        CHECK(same(shown.panel, hidden.panel));
        CHECK(shown.curve.x == hidden.curve.x);
        CHECK(shown.curve.width == hidden.curve.width);
        CHECK(shown.curve.height >= 300.0f);
    }
}

TEST_CASE("Disposicion: tamanos absurdos no producen medidas negativas")
{
    for (const auto& [w, h] : {std::pair{0.0f, 0.0f}, {50.0f, 30.0f}, {-10.0f, 400.0f}})
    {
        for (const bool keyboard : {false, true})
        {
            const auto shown = editorLayout(w, h, keyboard);
            CHECK(shown.curve.height >= 0.0f);
            CHECK(shown.keyboard.height >= 0.0f);
            CHECK(shown.keyboard.width >= 0.0f);
        }
        const auto editor = editorLayout(w, h);
        const auto panel = panelLayout(editor.panel);
        const auto bar = topBarLayout(editor.topBar);
        const auto drawer = outputDrawerLayout(editor.drawer, true);
        for (const Rect& r : {editor.drawer, drawer.toggle, drawer.saturationMode, drawer.drive, drawer.mix,
                              drawer.gain, drawer.autoGain})
        {
            CHECK(r.width >= 0.0f);
            CHECK(r.height >= 0.0f);
        }
        for (const Rect& r : {editor.topBar, editor.curve, editor.meter, editor.panel, panel.bandCard, panel.eqCard,
                              panel.dynamicsCard, panel.type, panel.eqKnobs[0], panel.dynamicKnobs[4], panel.dynamicsMeter,
                              bar.logo, bar.presets, bar.phase, bar.bypass})
        {
            CHECK(r.width >= 0.0f);
            CHECK(r.height >= 0.0f);
        }
    }
}

TEST_CASE("Disposicion: la vista ampliada es la ventana menos la barra superior")
{
    std::vector<WindowSize> sizes{kMinWindow};
    for (int i = 0; i < kNumSizePresets; ++i)
        sizes.push_back(windowSize(static_cast<SizePreset>(i)));
    for (const auto size : sizesToCheck())
        sizes.push_back(size);

    for (const auto size : sizes)
    {
        INFO(size.width << " x " << size.height);
        const auto w = static_cast<float>(size.width);
        const auto h = static_cast<float>(size.height);
        const Rect window{0.0f, 0.0f, w, h};
        const auto normal = editorLayout(w, h);
        const auto expanded = editorLayout(w, h, false, true);

        // La barra se queda; la curva ocupa todo lo de debajo, con el margen de 12 px de siempre.
        CHECK(same(expanded.topBar, normal.topBar));
        CHECK(same(expanded.curve, {12.0f, 48.0f, w - 24.0f, h - 48.0f - 12.0f}));
        CHECK(same(expanded.meter, {}));
        CHECK(same(expanded.drawer, {})); // se oculta con el medidor, abierto o plegado
        CHECK(same(editorLayout(w, h, false, true, false).curve, expanded.curve));
        CHECK(same(expanded.panel, {}));
        CHECK(same(expanded.keyboard, {}));

        // Con el teclado de notas, curva y teclado se reparten ese mismo sitio.
        const auto withKeyboard = editorLayout(w, h, true, true);
        checkDisjointInside(
            {{"barra", withKeyboard.topBar}, {"curva", withKeyboard.curve}, {"teclado", withKeyboard.keyboard}},
            window);
        CHECK(withKeyboard.curve.x == expanded.curve.x);
        CHECK(withKeyboard.curve.y == expanded.curve.y);
        CHECK(withKeyboard.curve.width == expanded.curve.width);
        CHECK(withKeyboard.keyboard.height == kPianoKeyboardHeight);
        CHECK(withKeyboard.keyboard.width == expanded.curve.width);
        CHECK(withKeyboard.keyboard.y + withKeyboard.keyboard.height == expanded.curve.y + expanded.curve.height);

        // Al volver, la disposicion es identica a la de antes.
        for (const bool keyboard : {false, true})
        {
            const auto before = editorLayout(w, h, keyboard);
            const auto back = editorLayout(w, h, keyboard, false);
            CHECK(same(back.topBar, before.topBar));
            CHECK(same(back.curve, before.curve));
            CHECK(same(back.meter, before.meter));
            CHECK(same(back.panel, before.panel));
            CHECK(same(back.keyboard, before.keyboard));
        }
    }
}

TEST_CASE("Disposicion: el boton de la vista ampliada va junto al analizador")
{
    for (const auto size : sizesToCheck())
    {
        INFO(size.width << " x " << size.height);
        const auto bar =
            topBarLayout(editorLayout(static_cast<float>(size.width), static_cast<float>(size.height)).topBar);
        CHECK(bar.expand.width == bar.compare.width);
        CHECK(bar.expand.height == bar.compare.height);
        CHECK(bar.expand.x > bar.analyzer.x + bar.analyzer.width);
        CHECK(bar.expand.x < bar.range.x);
        // Los textos de siempre siguen cabiendo: "Analyzer pre + post", "Linear Medium", "±30 dB", "Bypass".
        CHECK(bar.analyzer.width >= 124.0f);
        CHECK(bar.phase.width >= 112.0f);
        CHECK(bar.range.width >= 72.0f);
        CHECK(bar.bypass.width >= 64.0f);
    }
    // En la ventana minima, el navegador de presets tenia 140 px; sin la
    // salida ni Auto (en el cajon, ADR 0027) tiene su maximo y aun sobran 88 px.
    const auto minimum =
        topBarLayout(editorLayout(static_cast<float>(kMinWindow.width), static_cast<float>(kMinWindow.height)).topBar);
    CHECK(minimum.presets.width == 320.0f);
    CHECK(minimum.compare.x - (minimum.redo.x + minimum.redo.width) == 88.0f + 16.0f);
}

TEST_CASE("Disposicion: el panel Match cabe en la curva sin tapar la barra")
{
    std::vector<WindowSize> sizes{kMinWindow};
    for (int i = 0; i < kNumSizePresets; ++i)
        sizes.push_back(windowSize(static_cast<SizePreset>(i)));
    for (const auto size : sizesToCheck())
        sizes.push_back(size);

    for (const auto size : sizes)
        for (const bool keyboard : {false, true})
            for (const bool expanded : {false, true})
            {
                INFO(size.width << " x " << size.height << (keyboard ? ", con teclado" : "")
                                << (expanded ? ", ampliada" : ""));
                const auto w = static_cast<float>(size.width);
                const auto h = static_cast<float>(size.height);
                const auto editor = editorLayout(w, h, keyboard, expanded);
                const auto match = matchPanelLayout(editor.curve);

                // La tarjeta entera, dentro de la curva y sin la escala de dB.
                CHECK(match.card.width == kMatchCardWidth);
                CHECK(match.card.height == kMatchCardHeight);
                CHECK(inside(match.card, editor.curve));
                CHECK_FALSE(overlap(match.card, editor.topBar));
                CHECK(match.card.y >= editor.curve.y + 8.0f);
                CHECK(match.card.x + match.card.width <= editor.curve.x + editor.curve.width - kGainScaleWidth + 0.01f);
                if (keyboard)
                    CHECK_FALSE(overlap(match.card, editor.keyboard));

                // Sus controles, dentro y sin solaparse, con sitio para sus textos.
                checkDisjointInside({{"titulo", match.title},
                                     {"cerrar", match.close},
                                     {"etiqueta de la referencia", match.referenceLabel},
                                     {"referencia", match.reference},
                                     {"learn", match.learn},
                                     {"progreso", match.progress},
                                     {"etiqueta de amount", match.amountLabel},
                                     {"amount", match.amount},
                                     {"etiqueta de smoothing", match.smoothingLabel},
                                     {"smoothing", match.smoothing},
                                     {"etiqueta de max bands", match.bandsLabel},
                                     {"max bands", match.bands},
                                     {"etiqueta del rango", match.rangeLabel},
                                     {"minimo", match.rangeLow},
                                     {"guion", match.rangeDash},
                                     {"maximo", match.rangeHigh},
                                     {"apply", match.apply},
                                     {"estado", match.status}},
                                    match.card);
                CHECK(match.reference.width >= 168.0f); // "Sidechain (not connected)"
                CHECK(match.smoothing.width >= 3.0f * 52.0f); // "1/6", "1/3", "1 oct"
                CHECK(match.amount.width >= 160.0f);          // la barra y "100 %"
                CHECK(match.rangeLow.width >= 76.0f);         // "16.0 kHz", o una nota
                CHECK(match.rangeHigh.width == match.rangeLow.width);
                CHECK(match.status.width >= 160.0f); // "Nothing to match", "Sidechain disconnected"
                CHECK(match.close.width == match.close.height);
            }
}

TEST_CASE("Disposicion: el panel Match no se sale de una curva diminuta")
{
    for (const Rect curve : {Rect{}, Rect{10.0f, 20.0f, 100.0f, 50.0f}, Rect{0.0f, 0.0f, 400.0f, 120.0f}})
    {
        const auto match = matchPanelLayout(curve);
        CHECK(match.card.width >= 0.0f);
        CHECK(match.card.height >= 0.0f);
        CHECK(match.card.x >= curve.x);
        CHECK(match.card.y + match.card.height <= curve.y + std::max(curve.height, 8.0f) + 0.01f);
        CHECK(match.apply.width >= 0.0f);
        CHECK(match.status.width >= 0.0f);
    }
}

TEST_CASE("Disposicion: el cajon de la etapa de salida cabe abierto en todos los tamanos")
{
    for (const auto size : sizesToCheck())
        for (const bool keyboard : {false, true})
        {
            INFO(size.width << " x " << size.height << ", teclado: " << keyboard);
            const auto editor = editorLayout(static_cast<float>(size.width), static_cast<float>(size.height), keyboard);
            // Junto al medidor y con su mismo alto (el del teclado incluido).
            CHECK(editor.drawer.width == kOutputDrawerWidth);
            CHECK(editor.drawer.y == editor.meter.y);
            CHECK(editor.drawer.height == editor.meter.height);
            CHECK(editor.drawer.x + editor.drawer.width + 8.0f == editor.meter.x);
            CHECK(editor.curve.x + editor.curve.width + 8.0f == editor.drawer.x);

            const auto drawer = outputDrawerLayout(editor.drawer, true);
            CHECK(same(drawer.card, editor.drawer));
            checkDisjointInside({{"cabecera", drawer.toggle},
                                 {"titulo", drawer.saturationTitle},
                                 {"modo", drawer.saturationMode},
                                 {"drive", drawer.drive},
                                 {"mezcla", drawer.mix},
                                 {"ganancia", drawer.gain},
                                 {"auto", drawer.autoGain}},
                                drawer.card);
            // En el orden de la senal: la saturacion arriba, la salida abajo.
            CHECK(drawer.saturationMode.y < drawer.drive.y);
            CHECK(drawer.drive.y + drawer.drive.height < drawer.gain.y);
            CHECK(drawer.gain.y + drawer.gain.height <= drawer.autoGain.y);
            CHECK(drawer.drive.y == drawer.mix.y);
            // El selector Off/Soft/Warm y "Auto -12.0 dB" caben (Auto media 92 px en la barra).
            CHECK(drawer.saturationMode.width >= 120.0f);
            CHECK(drawer.saturationMode.height == 24.0f);
            CHECK(drawer.autoGain.width >= 92.0f);
            CHECK(drawer.autoGain.height == 24.0f);
            // Cada celda tiene ancho para su etiqueta y su valor, y alto para un potenciometro.
            for (const Rect& cell : {drawer.drive, drawer.mix, drawer.gain})
            {
                CHECK(cell.width >= kMinKnobCellWidth);
                const auto parts = knobCellLayout(cell);
                CHECK(parts.knob.width >= kMinKnobDiameter);
                CHECK(parts.knob.width <= kMaxKnobDiameter);
                CHECK(inside(parts.label, cell));
                CHECK(inside(parts.knob, cell));
                CHECK(inside(parts.value, cell));
            }
        }
}

TEST_CASE("Disposicion: plegado, el cajon es una pestana y la curva recupera el sitio")
{
    for (const auto size : sizesToCheck())
    {
        INFO(size.width << " x " << size.height);
        const auto w = static_cast<float>(size.width);
        const auto h = static_cast<float>(size.height);
        const auto open = editorLayout(w, h);
        const auto folded = editorLayout(w, h, false, false, false);
        CHECK(folded.drawer.width == kOutputDrawerTabWidth);
        CHECK(folded.drawer.x + folded.drawer.width + 8.0f == folded.meter.x);
        CHECK(folded.curve.width == open.curve.width + kOutputDrawerWidth - kOutputDrawerTabWidth);
        // Lo demas no se mueve.
        CHECK(same(folded.topBar, open.topBar));
        CHECK(same(folded.meter, open.meter));
        CHECK(same(folded.panel, open.panel));
        CHECK(folded.curve.x == open.curve.x);

        const auto tab = outputDrawerLayout(folded.drawer, false);
        CHECK(same(tab.card, folded.drawer));
        CHECK(same(tab.toggle, folded.drawer));
        for (const Rect& r : {tab.saturationTitle, tab.saturationMode, tab.drive, tab.mix, tab.gain, tab.autoGain})
            CHECK(same(r, {}));
    }
}
