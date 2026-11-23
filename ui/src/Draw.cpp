// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Draw.h"

#include <algorithm>
#include <cmath>

namespace esceq::ui::draw {

namespace {

using namespace theme;

juce::Colour body(const ControlState& state)
{
    if (state.pressed)
        return colour::controlDown;
    return state.hover ? colour::controlHover : colour::control;
}

/// Atenuación de un control desactivado o sin efecto.
float alphaFor(const ControlState& state)
{
    if (!state.enabled)
        return 0.32f;
    return state.dimmed ? 0.55f : 1.0f;
}

juce::Path arcPath(juce::Point<float> centre, float arcRadius, float from, float to)
{
    juce::Path path;
    path.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, std::min(from, to), std::max(from, to), true);
    return path;
}

juce::Path roundedCorners(juce::Rectangle<float> r, float cornerRadius, Corners corners)
{
    juce::Path path;
    const bool left = corners == Corners::All || corners == Corners::Left;
    const bool right = corners == Corners::All || corners == Corners::Right;
    path.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), cornerRadius, cornerRadius, left, right, left,
                             right);
    return path;
}

} // namespace

void card(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour(colour::card);
    g.fillRoundedRectangle(bounds, radius::card);
    g.setColour(colour::line);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius::card, stroke::hairline);
}

void knob(juce::Graphics& g, juce::Rectangle<float> bounds, const ValueArc& arc, bool bipolar, juce::Colour accent,
          const ControlState& state)
{
    const float alpha = alphaFor(state);
    const auto centre = bounds.getCentre();
    const float outer = 0.5f * std::min(bounds.getWidth(), bounds.getHeight());
    const float arcRadius = outer - 0.5f * stroke::arc;
    const float bodyRadius = outer - stroke::arc - 3.0f;
    const juce::PathStrokeType arcStroke(stroke::arc, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // Recorrido completo.
    g.setColour(colour::track.withMultipliedAlpha(alpha));
    g.strokePath(arcPath(centre, arcRadius, -kArcHalfSpan, kArcHalfSpan), arcStroke);

    // Arco de valor: desde el centro en los bipolares, desde el mínimo en el resto.
    const juce::Colour valueColour = state.enabled ? accent : colour::textDim;
    if (std::abs(arc.to - arc.from) > 0.01f)
    {
        if (state.pressed || state.hover)
        {
            // Resplandor suave: el control "responde" antes de moverse.
            g.setColour(valueColour.withAlpha(state.pressed ? 0.28f : 0.16f));
            g.strokePath(arcPath(centre, arcRadius, arc.from, arc.to),
                         juce::PathStrokeType(stroke::arc + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(valueColour.withMultipliedAlpha(alpha));
        g.strokePath(arcPath(centre, arcRadius, arc.from, arc.to), arcStroke);
    }

    // Marca del cero en los bipolares.
    if (bipolar)
    {
        g.setColour((std::abs(arc.to - arc.from) > 0.01f ? colour::textDim : valueColour).withMultipliedAlpha(alpha));
        g.fillEllipse(juce::Rectangle<float>(3.0f, 3.0f).withCentre(centre.getPointOnCircumference(arcRadius, arc.from)));
    }

    // Cuerpo: un disco con un degradado muy leve y un borde fino.
    const auto disc = juce::Rectangle<float>(2.0f * bodyRadius, 2.0f * bodyRadius).withCentre(centre);
    const juce::Colour base = body(state);
    g.setGradientFill(juce::ColourGradient(base.brighter(0.10f).withMultipliedAlpha(alpha), centre.x, disc.getY(),
                                           base.darker(0.12f).withMultipliedAlpha(alpha), centre.x, disc.getBottom(), false));
    g.fillEllipse(disc);
    g.setColour(colour::lineStrong.withMultipliedAlpha(alpha));
    g.drawEllipse(disc.reduced(0.5f), stroke::hairline);

    // Indicador.
    const auto tip = centre.getPointOnCircumference(bodyRadius - 3.0f, arc.indicator);
    const auto tail = centre.getPointOnCircumference(bodyRadius * 0.42f, arc.indicator);
    g.setColour((state.enabled ? colour::text : colour::textDim).withMultipliedAlpha(alpha));
    g.drawLine(juce::Line<float>(tail, tip), stroke::medium);

    if (state.focused)
    {
        g.setColour(colour::accent);
        g.drawEllipse(juce::Rectangle<float>(2.0f * outer + 5.0f, 2.0f * outer + 5.0f).withCentre(centre), stroke::thin);
    }
}

void bar(juce::Graphics& g, juce::Rectangle<float> bounds, const ValueSpan& span, bool bipolar, juce::Colour accent,
         const ControlState& state)
{
    const float alpha = alphaFor(state);
    constexpr float kThumb = 12.0f;
    const auto lane = bounds.reduced(0.5f * kThumb, 0.0f);
    const auto track = lane.withSizeKeepingCentre(lane.getWidth(), stroke::arc + 1.0f);
    const float cornerRadius = 0.5f * track.getHeight();

    g.setColour(colour::track.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(track, cornerRadius);

    const float x0 = track.getX() + span.from * track.getWidth();
    const float x1 = track.getX() + span.to * track.getWidth();
    if (x1 - x0 > 0.5f)
    {
        g.setColour(accent.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(juce::Rectangle<float>(x0, track.getY(), x1 - x0, track.getHeight()), cornerRadius);
    }

    if (bipolar)
    {
        g.setColour(colour::textDim.withMultipliedAlpha(alpha));
        g.fillRect(juce::Rectangle<float>(1.0f, 8.0f).withCentre({track.getCentreX(), track.getCentreY()}));
    }

    const juce::Point<float> thumbCentre{track.getX() + span.position * track.getWidth(), track.getCentreY()};
    const float size = state.pressed || state.hover ? kThumb + 2.0f : kThumb;
    const auto thumb = juce::Rectangle<float>(size, size).withCentre(thumbCentre);
    if (state.pressed || state.hover)
    {
        g.setColour(accent.withAlpha(state.pressed ? 0.28f : 0.16f));
        g.fillEllipse(thumb.expanded(4.0f));
    }
    g.setColour(colour::text.withMultipliedAlpha(alpha));
    g.fillEllipse(thumb);
    g.setColour(colour::window.withMultipliedAlpha(alpha));
    g.drawEllipse(thumb.reduced(0.5f), stroke::hairline);

    if (state.focused)
        focusRing(g, bounds, radius::control);
}

void toggle(juce::Graphics& g, juce::Rectangle<float> bounds, bool on, juce::Colour accent, const ControlState& state)
{
    const float alpha = alphaFor(state);
    const auto track = juce::Rectangle<float>(28.0f, 16.0f).withCentre(bounds.getCentre());
    const float cornerRadius = 0.5f * track.getHeight();

    juce::Colour fill = on ? accent : colour::track;
    if (state.hover || state.pressed)
        fill = fill.brighter(on ? 0.12f : 0.25f);
    g.setColour(fill.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(track, cornerRadius);
    if (!on)
    {
        g.setColour(colour::lineStrong.withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(track.reduced(0.5f), cornerRadius, stroke::hairline);
    }

    const float thumbSize = track.getHeight() - 4.0f;
    const float x = on ? track.getRight() - 2.0f - thumbSize : track.getX() + 2.0f;
    g.setColour((on ? colour::window : colour::textMuted).withMultipliedAlpha(alpha));
    g.fillEllipse(x, track.getY() + 2.0f, thumbSize, thumbSize);

    if (state.focused)
        focusRing(g, track, cornerRadius);
}

void pill(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& text, bool on, juce::Colour accent,
          const ControlState& state, Corners corners)
{
    const float alpha = alphaFor(state);
    const auto shape = roundedCorners(bounds, radius::control, corners);

    g.setColour((on ? accent.withAlpha(state.hover ? 0.30f : 0.22f) : body(state)).withMultipliedAlpha(alpha));
    g.fillPath(shape);
    g.setColour((on ? accent.withAlpha(0.85f) : colour::lineStrong).withMultipliedAlpha(alpha));
    g.strokePath(roundedCorners(bounds.reduced(0.5f), radius::control, corners), juce::PathStrokeType(stroke::hairline));

    g.setColour((on ? accent.brighter(0.35f) : (state.hover ? colour::text : colour::textMuted)).withMultipliedAlpha(alpha));
    g.setFont(font(type::small, Weight::Medium));
    g.drawText(text, bounds, juce::Justification::centred, false);

    if (state.focused)
        focusRing(g, bounds, radius::control);
}

void iconButton(juce::Graphics& g, juce::Rectangle<float> bounds, Icon icon, const ControlState& state, Corners corners,
                bool on, const juce::String& badge)
{
    const float alpha = alphaFor(state);
    g.setColour(body(state).withMultipliedAlpha(alpha));
    g.fillPath(roundedCorners(bounds, radius::control, corners));
    g.setColour(colour::lineStrong.withMultipliedAlpha(alpha));
    g.strokePath(roundedCorners(bounds.reduced(0.5f), radius::control, corners), juce::PathStrokeType(stroke::hairline));

    const auto c = bounds.getCentre();
    juce::Path path;
    switch (icon)
    {
    case Icon::Previous:
    case Icon::Next:
    {
        const float d = icon == Icon::Next ? 1.0f : -1.0f;
        path.startNewSubPath(c.x - 2.0f * d, c.y - 4.0f);
        path.lineTo(c.x + 2.0f * d, c.y);
        path.lineTo(c.x - 2.0f * d, c.y + 4.0f);
        break;
    }
    case Icon::Compare:
    {
        // Dos espectros que se cruzan.
        path.startNewSubPath(c.x - 7.0f, c.y + 4.5f);
        path.cubicTo(c.x - 4.5f, c.y + 4.5f, c.x - 3.5f, c.y - 4.5f, c.x - 0.5f, c.y - 4.5f);
        path.cubicTo(c.x + 2.0f, c.y - 4.5f, c.x + 2.5f, c.y + 4.5f, c.x + 5.0f, c.y + 4.5f);
        path.startNewSubPath(c.x - 5.0f, c.y + 4.5f);
        path.cubicTo(c.x - 2.5f, c.y + 4.5f, c.x - 2.0f, c.y - 1.0f, c.x + 1.5f, c.y - 1.0f);
        path.cubicTo(c.x + 4.5f, c.y - 1.0f, c.x + 4.5f, c.y + 4.5f, c.x + 7.0f, c.y + 4.5f);
        break;
    }
    case Icon::Options:
    {
        // Tres deslizadores: líneas con su mando en distintas posiciones.
        for (const auto& [y, knob] : {std::pair{-4.5f, 2.5f}, std::pair{0.0f, -3.0f}, std::pair{4.5f, 0.5f}})
        {
            path.startNewSubPath(c.x - 7.0f, c.y + y);
            path.lineTo(c.x + 7.0f, c.y + y);
            path.startNewSubPath(c.x + knob, c.y + y - 2.0f);
            path.lineTo(c.x + knob, c.y + y + 2.0f);
        }
        break;
    }
    case Icon::Expand:
    {
        // Cuatro esquinas: hacia fuera para ampliar; encendido, hacia dentro para volver.
        constexpr float kOuter = 6.5f;
        constexpr float kInner = 2.0f;
        constexpr float kArm = 3.5f;
        for (const float sx : {-1.0f, 1.0f})
            for (const float sy : {-1.0f, 1.0f})
            {
                const juce::Point<float> corner{c.x + sx * (on ? kInner : kOuter), c.y + sy * (on ? kInner : kOuter)};
                const float toward = on ? 1.0f : -1.0f; // hacia fuera del centro (volver) o hacia él (ampliar)
                path.startNewSubPath(corner.x + toward * sx * kArm, corner.y);
                path.lineTo(corner);
                path.lineTo(corner.x, corner.y + toward * sy * kArm);
            }
        break;
    }
    case Icon::Close:
    {
        // Una cruz.
        constexpr float kArm = 3.5f;
        path.startNewSubPath(c.x - kArm, c.y - kArm);
        path.lineTo(c.x + kArm, c.y + kArm);
        path.startNewSubPath(c.x + kArm, c.y - kArm);
        path.lineTo(c.x - kArm, c.y + kArm);
        break;
    }
    case Icon::Undo:
    case Icon::Redo:
    {
        // Flecha curva: un arco con una punta en su extremo.
        const float d = icon == Icon::Redo ? 1.0f : -1.0f;
        path.startNewSubPath(c.x - 5.0f * d, c.y + 4.0f);
        path.cubicTo(c.x - 5.0f * d, c.y - 3.0f, c.x + 1.0f * d, c.y - 3.5f, c.x + 5.0f * d, c.y - 0.5f);
        path.startNewSubPath(c.x + 5.5f * d, c.y - 4.5f);
        path.lineTo(c.x + 5.5f * d, c.y);
        path.lineTo(c.x + 1.0f * d, c.y);
        break;
    }
    }
    const auto iconColour = on ? colour::accent : (state.hover && state.enabled ? colour::text : colour::textMuted);
    g.setColour(iconColour.withMultipliedAlpha(alpha));
    g.strokePath(path, juce::PathStrokeType(stroke::thin, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (badge.isNotEmpty())
    {
        const auto font = theme::font(type::caption, Weight::SemiBold, true);
        const float h = 12.0f;
        const float w = std::max(h, juce::GlyphArrangement::getStringWidth(font, badge) + 6.0f);
        const juce::Rectangle<float> pill{bounds.getRight() - w - 1.0f, bounds.getY() + 1.0f, w, h};
        g.setColour(colour::accent.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(pill, 0.5f * h);
        g.setColour(colour::plot);
        g.setFont(font);
        g.drawText(badge, pill, juce::Justification::centred, false);
    }

    if (state.focused)
        focusRing(g, bounds, radius::control);
}

void dropdown(juce::Graphics& g, juce::Rectangle<float> bounds, const ControlState& state)
{
    const float alpha = alphaFor(state);
    g.setColour(body(state).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, radius::control);
    g.setColour(colour::lineStrong.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius::control, stroke::hairline);

    // Flecha.
    const juce::Point<float> c{bounds.getRight() - 13.0f, bounds.getCentreY() + 0.5f};
    juce::Path chevron;
    chevron.startNewSubPath(c.x - 3.5f, c.y - 2.0f);
    chevron.lineTo(c.x, c.y + 1.5f);
    chevron.lineTo(c.x + 3.5f, c.y - 2.0f);
    g.setColour((state.hover ? colour::text : colour::textMuted).withMultipliedAlpha(alpha));
    g.strokePath(chevron, juce::PathStrokeType(stroke::thin, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (state.focused)
        focusRing(g, bounds, radius::control);
}

void drawerToggle(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title, bool open, bool active,
                  const ControlState& state)
{
    const float alpha = alphaFor(state);
    const auto textColour = (state.hover ? colour::text : colour::textMuted).withMultipliedAlpha(alpha);
    constexpr float kChevron = 3.0f;
    auto chevron = [&](juce::Point<float> c, float direction) {
        juce::Path path;
        path.startNewSubPath(c.x - kChevron * direction, c.y - 2.0f * kChevron);
        path.lineTo(c.x + kChevron * direction, c.y);
        path.lineTo(c.x - kChevron * direction, c.y + 2.0f * kChevron);
        g.setColour(textColour);
        g.strokePath(path,
                     juce::PathStrokeType(stroke::thin, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    if (open)
    {
        // La cabecera de la tarjeta: el título como los de grupo y la flecha a la derecha.
        caption(g, title, bounds, juce::Justification::centredLeft, state.hover ? colour::textMuted : colour::textDim);
        chevron({bounds.getRight() - 6.0f, bounds.getCentreY()}, 1.0f);
        if (state.focused)
            focusRing(g, bounds.expanded(4.0f, 2.0f), radius::control);
        return;
    }

    // La pestaña plegada: una tarjeta estrecha que se ilumina al pasar el ratón.
    g.setColour((state.hover || state.pressed ? body(state) : colour::card).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, radius::control);
    g.setColour(colour::line.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius::control, stroke::hairline);
    chevron({bounds.getCentreX(), bounds.getY() + 16.0f}, -1.0f);

    // El título, de abajo arriba, centrado en la pestaña.
    g.setFont(captionFont());
    g.setColour(colour::textDim.withMultipliedAlpha(alpha));
    const float textWidth = std::min(bounds.getHeight() - 64.0f, 120.0f);
    if (textWidth > 20.0f)
    {
        const auto centre = bounds.getCentre();
        juce::Graphics::ScopedSaveState save(g);
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, centre.x, centre.y));
        g.drawText(title.toUpperCase(), juce::Rectangle<float>(textWidth, bounds.getWidth()).withCentre(centre),
                   juce::Justification::centred, false);
    }

    if (active)
    {
        g.setColour(colour::accent.withMultipliedAlpha(alpha));
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({bounds.getCentreX(), bounds.getBottom() - 14.0f}));
    }
    if (state.focused)
        focusRing(g, bounds, radius::control);
}

void field(juce::Graphics& g, juce::Rectangle<float> bounds, const ControlState& state)
{
    const float alpha = alphaFor(state);
    g.setColour(body(state).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, radius::control);
    g.setColour(colour::lineStrong.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius::control, stroke::hairline);
    if (state.focused)
        focusRing(g, bounds, radius::control);
}

void progress(juce::Graphics& g, juce::Rectangle<float> bounds, double fraction, juce::Colour accent,
              const ControlState& state)
{
    const float alpha = alphaFor(state);
    g.setColour(colour::track.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, radius::small);
    const auto filled = bounds.withWidth(bounds.getWidth() * static_cast<float>(std::clamp(fraction, 0.0, 1.0)));
    if (filled.getWidth() > 0.0f)
    {
        g.setColour(accent.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(filled, radius::small);
    }
}

void focusRing(juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
{
    g.setColour(colour::accent);
    g.drawRoundedRectangle(bounds.expanded(2.0f), cornerRadius + 2.0f, stroke::thin);
}

void caption(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> bounds, juce::Justification justification,
             juce::Colour colour)
{
    g.setColour(colour);
    g.setFont(captionFont());
    g.drawText(text.toUpperCase(), bounds, justification, false);
}

} // namespace esceq::ui::draw
