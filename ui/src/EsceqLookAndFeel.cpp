// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EsceqLookAndFeel.h"

#include "Draw.h"
#include "Theme.h"

namespace esceq::ui {

using namespace theme;

EsceqLookAndFeel::EsceqLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, colour::window);

    setColour(juce::ComboBox::textColourId, colour::text);
    setColour(juce::ComboBox::backgroundColourId, colour::control);
    setColour(juce::ComboBox::outlineColourId, colour::lineStrong);

    setColour(juce::PopupMenu::backgroundColourId, colour::card);
    setColour(juce::PopupMenu::textColourId, colour::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colour::controlDown);
    setColour(juce::PopupMenu::highlightedTextColourId, colour::text);
    setColour(juce::PopupMenu::headerTextColourId, colour::textDim);

    setColour(juce::TextEditor::backgroundColourId, colour::plot);
    setColour(juce::TextEditor::textColourId, colour::text);
    setColour(juce::TextEditor::highlightColourId, colour::accent.withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, colour::text);
    setColour(juce::TextEditor::outlineColourId, colour::lineStrong);
    setColour(juce::TextEditor::focusedOutlineColourId, colour::accent);
    setColour(juce::CaretComponent::caretColourId, colour::accent);

    setColour(juce::Label::textColourId, colour::text);
    setColour(juce::TextButton::textColourOffId, colour::textMuted);
    setColour(juce::TextButton::textColourOnId, colour::text);
    setColour(juce::TooltipWindow::textColourId, colour::text);

    setColour(juce::AlertWindow::backgroundColourId, colour::card);
    setColour(juce::AlertWindow::textColourId, colour::text);
    setColour(juce::AlertWindow::outlineColourId, colour::lineStrong);
}

juce::Typeface::Ptr EsceqLookAndFeel::getTypefaceForFont(const juce::Font& f)
{
    // Cualquier texto que no pida una tipografía concreta sale en Inter:
    // así la interfaz se ve igual en Windows, macOS y Linux.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return theme::defaultTypeface();
    return LookAndFeel_V4::getTypefaceForFont(f);
}

// ---------------------------------------------------------------------------
// Desplegables

void EsceqLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int, int, int, int,
                                    juce::ComboBox& box)
{
    draw::ControlState state;
    state.hover = box.isMouseOver(true);
    state.pressed = isButtonDown || box.isPopupActive();
    state.enabled = box.isEnabled();
    state.focused = box.hasKeyboardFocus(true) && !box.isPopupActive() && box.getProperties().contains("keyboardFocus");
    draw::dropdown(g, juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)), state);
}

juce::Font EsceqLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return font(type::body);
}

void EsceqLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(juce::roundToInt(space::x2) - 2, 0, box.getWidth() - 26, box.getHeight());
    label.setFont(getComboBoxFont(box));
    label.setColour(juce::Label::textColourId, box.isEnabled() ? colour::text : colour::textDim);
}

// ---------------------------------------------------------------------------
// Menús

void EsceqLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(colour::card);
    g.setColour(colour::lineStrong);
    g.drawRect(0, 0, width, height, 1);
}

void EsceqLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                         bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                         const juce::String& shortcutKeyText, const juce::Drawable*, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour(colour::line);
        g.fillRect(area.reduced(juce::roundToInt(space::x2), 0).withSizeKeepingCentre(area.getWidth() - 16, 1));
        return;
    }

    const auto row = area.toFloat().reduced(space::x1, 1.0f);
    if (isHighlighted && isActive)
    {
        g.setColour(colour::controlDown);
        g.fillRoundedRectangle(row, radius::small + 1.0f);
    }

    juce::Colour ink = textColour != nullptr ? *textColour : colour::text;
    if (!isActive)
        ink = colour::textDim.withAlpha(0.6f);

    // Marca de la opción elegida.
    auto content = row.reduced(space::x2, 0.0f);
    const auto tickArea = content.removeFromLeft(14.0f);
    if (isTicked)
    {
        juce::Path tick;
        const auto c = tickArea.getCentre();
        tick.startNewSubPath(c.x - 4.0f, c.y);
        tick.lineTo(c.x - 1.0f, c.y + 3.0f);
        tick.lineTo(c.x + 4.5f, c.y - 3.5f);
        g.setColour(colour::accent);
        g.strokePath(tick, juce::PathStrokeType(stroke::thin, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    content.removeFromLeft(space::x1);

    if (hasSubMenu)
    {
        const auto arrowArea = content.removeFromRight(10.0f);
        const auto c = arrowArea.getCentre();
        juce::Path arrow;
        arrow.startNewSubPath(c.x - 2.0f, c.y - 3.5f);
        arrow.lineTo(c.x + 1.5f, c.y);
        arrow.lineTo(c.x - 2.0f, c.y + 3.5f);
        g.setColour(colour::textMuted);
        g.strokePath(arrow, juce::PathStrokeType(stroke::thin, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setFont(getPopupMenuFont());
    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour(colour::textDim);
        g.drawText(shortcutKeyText, content, juce::Justification::centredRight, false);
    }
    g.setColour(ink);
    g.drawText(text, content, juce::Justification::centredLeft, true);
}

void EsceqLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area,
                                                  const juce::String& sectionName)
{
    draw::caption(g, sectionName, area.toFloat().reduced(space::x3, 0.0f), juce::Justification::centredLeft);
}

juce::Font EsceqLookAndFeel::getPopupMenuFont()
{
    return font(type::body);
}

void EsceqLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int, int& idealWidth,
                                                 int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = 9;
        return;
    }
    idealHeight = 26;
    idealWidth = juce::roundToInt(juce::GlyphArrangement::getStringWidth(getPopupMenuFont(), text)) + 64;
}

// ---------------------------------------------------------------------------
// Campos de texto y botones

void EsceqLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), radius::small + 1.0f);
}

void EsceqLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour(editor.hasKeyboardFocus(true) ? colour::accent : colour::lineStrong);
    g.drawRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f),
                           radius::small + 1.0f, stroke::hairline);
}

void EsceqLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted,
                                            bool down)
{
    draw::ControlState state;
    state.hover = highlighted;
    state.pressed = down;
    state.enabled = button.isEnabled();
    const auto bounds = button.getLocalBounds().toFloat();
    g.setColour((down ? colour::controlDown : highlighted ? colour::controlHover : colour::control)
                    .withMultipliedAlpha(state.enabled ? 1.0f : 0.4f));
    g.fillRoundedRectangle(bounds, radius::control);
    g.setColour(colour::lineStrong.withMultipliedAlpha(state.enabled ? 1.0f : 0.4f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius::control, stroke::hairline);
    if (button.hasKeyboardFocus(true) && button.getProperties().contains("keyboardFocus"))
        draw::focusRing(g, bounds, radius::control);
}

void EsceqLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool highlighted, bool)
{
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.setColour((highlighted ? colour::text : colour::textMuted).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f));
    g.drawText(button.getButtonText(), button.getLocalBounds().reduced(4, 0), juce::Justification::centred, true);
}

juce::Font EsceqLookAndFeel::getTextButtonFont(juce::TextButton&, int)
{
    return font(type::body, Weight::Medium);
}

juce::Font EsceqLookAndFeel::getLabelFont(juce::Label& label)
{
    // Las etiquetas conservan la fuente que se les haya puesto; solo se
    // garantiza que salga en Inter.
    return label.getFont();
}

void EsceqLookAndFeel::drawCornerResizer(juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    // Tres puntos en diagonal, discretos.
    g.setColour(isMouseOver || isMouseDragging ? colour::textMuted : colour::lineStrong);
    const auto fw = static_cast<float>(w);
    const auto fh = static_cast<float>(h);
    for (const auto& [dx, dy] : {std::pair{4.0f, 4.0f}, {8.0f, 4.0f}, {4.0f, 8.0f}})
        g.fillEllipse(fw - dx - 2.0f, fh - dy - 2.0f, 2.0f, 2.0f);
}

void EsceqLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const juce::Rectangle<float> bounds(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.setColour(colour::card);
    g.fillRect(bounds);
    g.setColour(colour::lineStrong);
    g.drawRect(bounds, 1.0f);
    g.setColour(colour::text);
    g.setFont(font(type::small));
    g.drawFittedText(text, bounds.reduced(6.0f, 2.0f).toNearestInt(), juce::Justification::centred, 4);
}

} // namespace esceq::ui
