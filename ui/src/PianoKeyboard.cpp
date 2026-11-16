// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PianoKeyboard.h"

#include "JuceConvert.h"
#include "Theme.h"

#include <cmath>

namespace esceq::ui {

namespace {

using namespace theme;

/// El eje de las teclas: el de la curva, que deja 1 px de borde a cada lado.
CurveMapping keyMapping(juce::Rectangle<float> bounds)
{
    return CurveMapping({bounds.getX() + 1.0f, bounds.getY(), bounds.getWidth() - 2.0f, bounds.getHeight()}, 1.0);
}

} // namespace

PianoKeyboard::PianoKeyboard()
{
    setTitle("Piano keyboard");
    setDescription("Notes along the frequency axis");
}

void PianoKeyboard::setA4(double a4Hz)
{
    // Solo cambia cuando el usuario la elige: las teclas se recolocan siempre.
    a4Hz_ = a4Hz;
    updateKeys();
}

void PianoKeyboard::resized()
{
    updateKeys();
}

void PianoKeyboard::updateKeys()
{
    keys_ = pianoKeys(keyMapping(getLocalBounds().toFloat()), a4Hz_);
    repaint();
}

void PianoKeyboard::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    juce::Graphics::ScopedSaveState save(g);
    juce::Path shape;
    shape.addRoundedRectangle(bounds, radius::small);
    g.reduceClipRegion(shape);
    g.fillAll(colour::control);

    const float blackHeight = std::round(kBlackKeyFraction * bounds.getHeight());
    const auto keyRect = [&](const PianoKey& key, float height) {
        return juce::Rectangle<float>(key.left, bounds.getY(), key.right - key.left, height);
    };

    // Blancas: la señalada, y una raya entre cada dos.
    for (const auto& key : keys_)
    {
        if (key.black)
            continue;
        if (key.midi == hovered_)
        {
            g.setColour(colour::accent.withAlpha(0.85f));
            g.fillRect(keyRect(key, bounds.getHeight()));
        }
        g.setColour(colour::plot);
        g.fillRect(juce::Rectangle<float>(std::round(key.right) - 0.5f, bounds.getY(), 1.0f, bounds.getHeight()));
    }

    // Octavas en los do: en su semitono y el del si de al lado, donde no hay negras.
    g.setFont(font(type::caption, Weight::Medium, true));
    const auto mapping = keyMapping(bounds);
    for (const auto& key : keys_)
    {
        if (key.black || ((key.midi % 12) + 12) % 12 != 0)
            continue;
        const float right = mapping.xForFrequency(frequencyForNote(key.midi + 0.5, a4Hz_)) - 1.0f;
        const float left = mapping.xForFrequency(frequencyForNote(key.midi - 1.5, a4Hz_)) + 1.0f;
        const auto text = toJuce(noteName(key.midi));
        if (right > bounds.getRight() - 1.0f || left < bounds.getX() + 1.0f ||
            juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), text) > right - left)
            continue;
        g.setColour(key.midi == hovered_ ? colour::plot : colour::textDim);
        g.drawText(text, juce::Rectangle<float>(left, bounds.getY(), right - left, bounds.getHeight()),
                   juce::Justification::centredRight, false);
    }

    // Negras, encima, más cortas.
    for (const auto& key : keys_)
    {
        if (!key.black)
            continue;
        g.setColour(key.midi == hovered_ ? colour::accent : colour::plot);
        g.fillRect(keyRect(key, blackHeight).reduced(0.5f, 0.0f));
    }
}

void PianoKeyboard::mouseMove(const juce::MouseEvent& e)
{
    const auto bounds = getLocalBounds().toFloat();
    setHoveredNote(pianoKeyAt(keys_, {bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()},
                              {e.position.x, e.position.y}));
}

void PianoKeyboard::mouseExit(const juce::MouseEvent&)
{
    setHoveredNote(std::nullopt);
}

void PianoKeyboard::setHoveredNote(std::optional<int> midi)
{
    if (midi == hovered_)
        return;
    hovered_ = midi;
    repaint();
    if (onHoveredNoteChanged)
        onHoveredNoteChanged(hovered_);
}

} // namespace esceq::ui
