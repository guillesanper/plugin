// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/layout.h"

#include "esceq/ui/notes.h"

#include <algorithm>
#include <cmath>

namespace esceq::ui {

namespace {

// Rejilla de 4 px.
constexpr float kS1 = 4.0f;
constexpr float kS2 = 8.0f;
constexpr float kS3 = 12.0f;
constexpr float kS4 = 16.0f;

constexpr float kTopBarHeight = 48.0f;
constexpr float kPanelHeight = 168.0f;
constexpr float kMeterWidth = 52.0f;
constexpr float kControlHeight = 24.0f;
constexpr float kIconButton = 28.0f;

constexpr float kBandCardWidth = 180.0f;
constexpr float kDynamicsSideWidth = 104.0f;
constexpr float kDynamicsMeterWidth = 28.0f;
constexpr float kGroupTitleHeight = 14.0f;

/// Corta `amount` por la izquierda de `r` y lo devuelve.
Rect takeLeft(Rect& r, float amount) noexcept
{
    const float w = std::clamp(amount, 0.0f, r.width);
    const Rect out{r.x, r.y, w, r.height};
    r.x += w;
    r.width -= w;
    return out;
}

Rect takeRight(Rect& r, float amount) noexcept
{
    const float w = std::clamp(amount, 0.0f, r.width);
    r.width -= w;
    return {r.x + r.width, r.y, w, r.height};
}

Rect takeTop(Rect& r, float amount) noexcept
{
    const float h = std::clamp(amount, 0.0f, r.height);
    const Rect out{r.x, r.y, r.width, h};
    r.y += h;
    r.height -= h;
    return out;
}

Rect takeBottom(Rect& r, float amount) noexcept
{
    const float h = std::clamp(amount, 0.0f, r.height);
    r.height -= h;
    return {r.x, r.y + r.height, r.width, h};
}

Rect inset(Rect r, float dx, float dy) noexcept
{
    const float w = std::max(0.0f, r.width - 2.0f * dx);
    const float h = std::max(0.0f, r.height - 2.0f * dy);
    return {r.x + dx, r.y + dy, w, h};
}

/// Franja de altura `height` centrada verticalmente en `r`.
Rect centredRow(Rect r, float height) noexcept
{
    const float h = std::min(height, r.height);
    return {r.x, r.y + std::floor(0.5f * (r.height - h)), r.width, h};
}

} // namespace

WindowSize windowSize(SizePreset preset) noexcept
{
    switch (preset)
    {
    case SizePreset::Small:
        return {1120, 580};
    case SizePreset::Large:
        return {1480, 860};
    case SizePreset::Medium:
        break;
    }
    return {1180, 700};
}

std::optional<SizePreset> matchingPreset(WindowSize size) noexcept
{
    for (int i = 0; i < kNumSizePresets; ++i)
        if (windowSize(static_cast<SizePreset>(i)) == size)
            return static_cast<SizePreset>(i);
    return std::nullopt;
}

WindowSize clampWindow(WindowSize size) noexcept
{
    return {std::clamp(size.width, kMinWindow.width, kMaxWindow.width),
            std::clamp(size.height, kMinWindow.height, kMaxWindow.height)};
}

EditorLayout editorLayout(float width, float height, bool pianoKeyboard, bool expanded) noexcept
{
    Rect area{0.0f, 0.0f, std::max(width, 0.0f), std::max(height, 0.0f)};
    EditorLayout layout;
    layout.topBar = takeTop(area, kTopBarHeight);

    if (expanded)
    {
        Rect middle = inset(area, kS3, 0.0f);
        takeBottom(middle, kS3);
        if (pianoKeyboard)
        {
            layout.keyboard = takeBottom(middle, kPianoKeyboardHeight);
            takeBottom(middle, kS1);
        }
        layout.curve = middle;
        return layout;
    }

    Rect panel = area;
    panel.y = area.y + std::max(0.0f, area.height - kPanelHeight);
    panel.height = std::min(kPanelHeight, area.height);
    layout.panel = panel;
    area.height -= panel.height;

    Rect middle = inset(area, kS3, 0.0f);
    layout.meter = takeRight(middle, kMeterWidth);
    takeRight(middle, kS2);
    if (pianoKeyboard)
    {
        layout.keyboard = takeBottom(middle, kPianoKeyboardHeight);
        takeBottom(middle, kS1);
    }
    layout.curve = middle;
    return layout;
}

TopBarLayout topBarLayout(Rect bounds) noexcept
{
    Rect row = centredRow(inset(bounds, kS3, 0.0f), 28.0f);
    TopBarLayout layout;

    // Los anchos de la marca, el rango y el bypass dejan sitio al botón de la
    // vista ampliada sin tocar la ventana mínima (ADR 0018).
    layout.logo = takeLeft(row, 88.0f);
    takeLeft(row, kS3);

    layout.bypass = takeRight(row, 64.0f);
    takeRight(row, kS2);
    layout.autoGain = takeRight(row, 92.0f); // "Auto -12.0 dB" sin cortarse
    takeRight(row, kS3);
    layout.outputValue = takeRight(row, 60.0f);
    layout.output = takeRight(row, 96.0f);
    takeRight(row, kS3);
    layout.range = takeRight(row, 72.0f); // "±30 dB"
    takeRight(row, kS2);
    layout.expand = takeRight(row, kIconButton);
    takeRight(row, kS2);
    layout.analyzer = takeRight(row, 124.0f);
    takeRight(row, kS2);
    layout.phase = takeRight(row, 112.0f); // "Linear Medium" sin estrecharse
    takeRight(row, kS2);
    layout.options = takeRight(row, kIconButton);
    takeRight(row, kS2);
    layout.compare = takeRight(row, kIconButton);
    takeRight(row, kS4);

    // Lo que queda es para los presets (hasta un máximo) y deshacer/rehacer.
    constexpr float kHistoryButton = kIconButton;
    constexpr float kMaxPresetsWidth = 320.0f;
    const float presets = std::clamp(row.width - 2.0f * kHistoryButton - kS2, 0.0f, kMaxPresetsWidth);
    layout.presets = takeLeft(row, presets);
    takeLeft(row, kS2);
    layout.undo = takeLeft(row, kHistoryButton);
    layout.redo = takeLeft(row, kHistoryButton);
    return layout;
}

PanelLayout panelLayout(Rect bounds) noexcept
{
    PanelLayout layout;

    Rect area = bounds;
    takeTop(area, kS2);
    area = inset(area, kS3, 0.0f);
    area.height = std::max(0.0f, area.height - kS3);

    // Tarjeta de la banda: ancho fijo.
    layout.bandCard = takeLeft(area, kBandCardWidth);
    takeLeft(area, kS2);
    {
        Rect inner = inset(layout.bandCard, kS3, kS3);
        const float gap = std::max(kS1, std::floor((inner.height - 4.0f * kControlHeight) / 3.0f));
        Rect header = takeTop(inner, kControlHeight);
        layout.enabled = takeRight(header, 36.0f);
        takeRight(header, kS2);
        layout.bandTitle = header;
        takeTop(inner, gap);
        layout.type = takeTop(inner, kControlHeight);
        takeTop(inner, gap);
        layout.slope = takeTop(inner, kControlHeight);
        takeTop(inner, gap);
        layout.channel = takeTop(inner, kControlHeight);
    }

    // El resto se reparte entre las ocho celdas de potenciómetro: tres en la
    // tarjeta de EQ y cinco en la de dinámica, todas del mismo ancho.
    const float fixedWidth = 2.0f * kS3                                       // relleno de la tarjeta de EQ
                             + kS2                                           // separación entre tarjetas
                             + 2.0f * kS3 + kDynamicsSideWidth + kS2         // relleno y columna lateral
                             + kS2 + kDynamicsMeterWidth;                    // medidor
    const float cell = std::max(0.0f, (area.width - fixedWidth) / static_cast<float>(kNumEqKnobs + kNumDynamicKnobs));

    // Las tarjetas empiezan en píxeles enteros; dentro, las celdas se reparten
    // el ancho exacto (pueden diferir en menos de un píxel entre tarjetas).
    layout.eqCard = takeLeft(area, std::round(2.0f * kS3 + static_cast<float>(kNumEqKnobs) * cell));
    takeLeft(area, kS2);
    layout.dynamicsCard = area;

    {
        Rect inner = inset(layout.eqCard, kS3, kS3);
        layout.eqTitle = takeTop(inner, kGroupTitleHeight);
        takeTop(inner, kS1);
        const float eqCell = inner.width / static_cast<float>(kNumEqKnobs);
        for (std::size_t i = 0; i < layout.eqKnobs.size(); ++i)
            layout.eqKnobs[i] = {inner.x + static_cast<float>(i) * eqCell, inner.y, eqCell, inner.height};
    }
    {
        Rect inner = inset(layout.dynamicsCard, kS3, kS3);
        Rect title = takeTop(inner, kGroupTitleHeight);
        takeTop(inner, kS1);

        Rect side = takeLeft(inner, kDynamicsSideWidth);
        takeLeft(inner, kS2);
        layout.dynamicsMeter = takeRight(inner, kDynamicsMeterWidth);
        takeRight(inner, kS2);

        layout.dynamicsTitle = {title.x, title.y, kDynamicsSideWidth, title.height};
        // Interruptor, fuente con su aviso debajo y tiempos automáticos: con
        // la altura del panel (168 px) caben con 2 px de sobra.
        takeTop(side, kS2);
        layout.dynamicsEnabled = takeTop(side, kControlHeight);
        takeTop(side, kS1);
        layout.source = takeTop(side, kControlHeight);
        layout.sourceWarning = takeTop(side, 16.0f);
        takeTop(side, kS1);
        layout.autoTiming = takeTop(side, kControlHeight);

        const float dynamicCell = inner.width / static_cast<float>(kNumDynamicKnobs);
        for (std::size_t i = 0; i < layout.dynamicKnobs.size(); ++i)
            layout.dynamicKnobs[i] = {inner.x + static_cast<float>(i) * dynamicCell, inner.y, dynamicCell, inner.height};
    }
    return layout;
}

MatchPanelLayout matchPanelLayout(Rect curve) noexcept
{
    MatchPanelLayout layout;
    const float right = curve.x + std::max(0.0f, curve.width - kGainScaleWidth);
    const float width = std::min(kMatchCardWidth, std::max(0.0f, right - curve.x - kS2));
    const float height = std::min(kMatchCardHeight, std::max(0.0f, curve.height - 2.0f * kS2));
    layout.card = {right - width, curve.y + kS2, width, height};

    Rect inner = inset(layout.card, kS3, kS3);
    Rect title = takeTop(inner, 20.0f);
    layout.close = takeRight(title, 20.0f);
    takeRight(title, kS2);
    layout.title = title;
    takeTop(inner, kS1);

    // Una fila con la etiqueta (o el botón) a la izquierda y el control a la derecha.
    bool first = true;
    auto row = [&](Rect& left, Rect& control) {
        if (!first)
            takeTop(inner, kS1); // compacta: la tarjeta tapa lo menos posible de la curva
        first = false;
        Rect r = takeTop(inner, kControlHeight);
        left = takeLeft(r, kMatchLabelWidth);
        takeLeft(r, kS2);
        control = r;
    };
    row(layout.referenceLabel, layout.reference);
    row(layout.learn, layout.progress);
    row(layout.amountLabel, layout.amount);
    row(layout.smoothingLabel, layout.smoothing);
    row(layout.bandsLabel, layout.bands);
    layout.bands.width = std::min(layout.bands.width, kMatchLabelWidth); // "24" no pide más
    Rect range;
    row(layout.rangeLabel, range);
    const float field = std::floor(std::max(0.0f, range.width - kS3) * 0.5f);
    layout.rangeLow = takeLeft(range, field);
    layout.rangeHigh = takeRight(range, field);
    layout.rangeDash = range;
    row(layout.apply, layout.status);
    return layout;
}

KnobCellLayout knobCellLayout(Rect cell) noexcept
{
    constexpr float kGap = kS1;
    const float available = cell.height - kKnobLabelHeight - kKnobValueHeight - 2.0f * kGap;
    const float diameter = std::clamp(std::floor(std::min(available, cell.width - kS4)), kMinKnobDiameter, kMaxKnobDiameter);
    const float content = kKnobLabelHeight + kGap + diameter + kGap + kKnobValueHeight;
    const float top = cell.y + std::floor(std::max(0.0f, 0.5f * (cell.height - content)));

    KnobCellLayout layout;
    layout.label = {cell.x, top, cell.width, kKnobLabelHeight};
    layout.knob = {cell.x + std::floor(0.5f * (cell.width - diameter)), top + kKnobLabelHeight + kGap, diameter, diameter};
    layout.value = {cell.x, layout.knob.y + diameter + kGap, cell.width, kKnobValueHeight};
    return layout;
}

} // namespace esceq::ui
