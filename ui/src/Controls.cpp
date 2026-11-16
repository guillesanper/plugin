// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Controls.h"

#include "JuceConvert.h"

#include "esceq/ui/control_logic.h"
#include "esceq/ui/layout.h"

#include <cmath>

namespace esceq::ui {

namespace {

using namespace theme;

constexpr float kBarValueWidth = 60.0f;

Rect toLogic(juce::Rectangle<float> r)
{
    return {r.getX(), r.getY(), r.getWidth(), r.getHeight()};
}

/// Lo que ve un lector de pantalla de un ParamControl: un deslizador con
/// rango, valor numérico y valor como texto ("1.20 kHz").
class ParamValueInterface final : public juce::AccessibilityValueInterface
{
public:
    explicit ParamValueInterface(ParamControl& control) : control_(control) {}

    bool isReadOnly() const override { return control_.spec() == nullptr || !control_.isEnabled(); }
    double getCurrentValue() const override { return control_.value(); }
    juce::String getCurrentValueAsString() const override { return control_.valueText(); }
    void setValue(double newValue) override { control_.setValueFromUser(static_cast<float>(newValue)); }
    void setValueAsString(const juce::String& newValue) override { control_.setTextFromUser(newValue); }

    AccessibleValueRange getRange() const override
    {
        const auto* spec = control_.spec();
        if (spec == nullptr)
            return {};
        return {{static_cast<double>(spec->min), static_cast<double>(spec->max)},
                static_cast<double>(spec->max - spec->min) * static_cast<double>(kKeyStep)};
    }

private:
    ParamControl& control_;
};

} // namespace

// ---------------------------------------------------------------------------
// ParamControl

ParamControl::ParamControl(Style style, const juce::String& label)
    : style_(style),
      label_(label)
{
    setTitle(label);
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(false);
}

ParamControl::~ParamControl()
{
    // Si la ventana se cierra en mitad de un arrastre, el host y el historial
    // de deshacer deben saber que el gesto ha terminado.
    if (dragging_ && moved_ && attachment_ != nullptr)
        attachment_->endGesture();
}

void ParamControl::attach(juce::RangedAudioParameter& parameter, const core::ParameterSpec& spec)
{
    hideEditor(false);
    attachment_.reset();
    spec_ = &spec;
    attachment_ = std::make_unique<juce::ParameterAttachment>(parameter, [this](float value) {
        value_ = value;
        repaint();
        if (auto* handler = getAccessibilityHandler())
            handler->notifyAccessibilityEvent(juce::AccessibilityEvent::valueChanged);
    });
    attachment_->sendInitialUpdate();
    setTitle(toJuce(spec.name));
}

void ParamControl::detach()
{
    hideEditor(false);
    if (dragging_ && moved_ && attachment_ != nullptr)
        attachment_->endGesture();
    dragging_ = false;
    attachment_.reset();
    spec_ = nullptr;
}

void ParamControl::setAccent(juce::Colour accent)
{
    if (accent_ == accent)
        return;
    accent_ = accent;
    repaint();
}

void ParamControl::setDimmed(bool dimmed)
{
    if (dimmed_ == dimmed)
        return;
    dimmed_ = dimmed;
    repaint();
}

void ParamControl::setValueOverride(const juce::String& text)
{
    if (valueOverride_ == text)
        return;
    valueOverride_ = text;
    repaint();
}

void ParamControl::setLabel(const juce::String& label)
{
    if (label_ == label)
        return;
    label_ = label;
    repaint();
}

juce::String ParamControl::valueText() const
{
    if (valueOverride_.isNotEmpty())
        return valueOverride_;
    return spec_ != nullptr ? toJuce(core::formatValue(*spec_, value_)) : juce::String();
}

void ParamControl::setValueFromUser(float value)
{
    if (attachment_ != nullptr && spec_ != nullptr && isEnabled())
        attachment_->setValueAsCompleteGesture(core::sanitize(*spec_, value));
}

bool ParamControl::setTextFromUser(const juce::String& text)
{
    if (spec_ == nullptr)
        return false;
    const auto value = commitText(*spec_, text.toStdString(), a4Hz_);
    if (!value)
        return false;
    setValueFromUser(*value);
    return true;
}

juce::Rectangle<float> ParamControl::controlArea() const
{
    const auto bounds = getLocalBounds().toFloat();
    if (style_ == Style::Bar)
        return bounds.withTrimmedRight(kBarValueWidth);
    return toJuce(knobCellLayout(toLogic(bounds)).knob);
}

juce::Rectangle<float> ParamControl::valueArea() const
{
    const auto bounds = getLocalBounds().toFloat();
    if (style_ == Style::Bar)
        return bounds.withLeft(bounds.getRight() - kBarValueWidth);
    return toJuce(knobCellLayout(toLogic(bounds)).value);
}

void ParamControl::resized()
{
    if (editor_ != nullptr)
        editor_->setBounds(valueArea().toNearestInt());
}

void ParamControl::paint(juce::Graphics& g)
{
    draw::ControlState state;
    state.hover = hover_;
    state.pressed = dragging_;
    state.focused = keyboardFocus_ && hasKeyboardFocus(false);
    state.enabled = isEnabled() && spec_ != nullptr;
    state.dimmed = dimmed_;

    const float textAlpha = !state.enabled ? 0.35f : (dimmed_ ? 0.6f : 1.0f);

    if (style_ == Style::Knob)
    {
        const auto layout = knobCellLayout(toLogic(getLocalBounds().toFloat()));
        draw::caption(g, label_, toJuce(layout.label), juce::Justification::centred,
                      (hover_ || dragging_ ? colour::textMuted : colour::textDim).withMultipliedAlpha(textAlpha));
        if (spec_ != nullptr)
            draw::knob(g, toJuce(layout.knob), valueArc(*spec_, value_), isBipolar(*spec_), accent_, state);
    }
    else if (spec_ != nullptr)
    {
        draw::bar(g, controlArea(), valueSpan(*spec_, value_), isBipolar(*spec_), accent_, state);
    }

    if (editor_ == nullptr && spec_ != nullptr)
    {
        g.setColour(colour::text.withMultipliedAlpha(textAlpha));
        g.setFont(font(type::body, Weight::Regular, true));
        g.drawText(valueText(), valueArea(),
                   style_ == Style::Knob ? juce::Justification::centred : juce::Justification::centredRight, false);
    }
}

void ParamControl::mouseEnter(const juce::MouseEvent&)
{
    hover_ = true;
    repaint();
}

void ParamControl::mouseExit(const juce::MouseEvent&)
{
    hover_ = false;
    repaint();
}

void ParamControl::mouseDown(const juce::MouseEvent& e)
{
    if (attachment_ == nullptr || spec_ == nullptr || e.mods.isPopupMenu())
        return;

    if (e.mods.isCommandDown())
    {
        resetToDefault();
        return;
    }

    dragging_ = true;
    moved_ = false;
    dragPosition_ = core::toNormalized(*spec_, value_);
    dragLast_ = e.position;
    repaint();
}

void ParamControl::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_ || attachment_ == nullptr || spec_ == nullptr)
        return;

    if (!moved_)
    {
        if (e.getDistanceFromDragStart() < 2)
            return; // un clic con el pulso algo tembloroso no es un arrastre
        moved_ = true;
        attachment_->beginGesture();
    }

    const auto delta = e.position - dragLast_;
    dragLast_ = e.position;

    // Potenciómetro: arriba y a la derecha suben. Barra: solo en horizontal,
    // y su ancho entero equivale a todo el rango.
    float pixels = delta.x - delta.y;
    if (style_ == Style::Bar)
        pixels = delta.x * kDragPixelsForFullRange / std::max(1.0f, controlArea().getWidth() - 12.0f);

    dragPosition_ = dragNormalized(dragPosition_, pixels, e.mods.isShiftDown());
    attachment_->setValueAsPartOfGesture(core::fromNormalized(*spec_, dragPosition_));
}

void ParamControl::mouseUp(const juce::MouseEvent& e)
{
    if (!dragging_)
        return;
    dragging_ = false;

    if (moved_ && attachment_ != nullptr)
        attachment_->endGesture();
    else if (!moved_ && valueArea().contains(e.position) && e.getNumberOfClicks() == 1)
        showEditor();

    moved_ = false;
    repaint();
}

void ParamControl::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!valueArea().contains(e.position))
        resetToDefault();
}

void ParamControl::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (attachment_ == nullptr || spec_ == nullptr || !isEnabled() || dragging_)
    {
        Component::mouseWheelMove(e, wheel);
        return;
    }
    const float delta = (std::abs(wheel.deltaY) > std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX) *
                        (wheel.isReversed ? -1.0f : 1.0f);
    attachment_->setValueAsCompleteGesture(wheelValue(*spec_, value_, delta, e.mods.isShiftDown()));
}

bool ParamControl::keyPressed(const juce::KeyPress& key)
{
    if (attachment_ == nullptr || spec_ == nullptr || !isEnabled())
        return false;

    int steps = 0;
    if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::rightKey))
        steps = 1;
    else if (key.isKeyCode(juce::KeyPress::downKey) || key.isKeyCode(juce::KeyPress::leftKey))
        steps = -1;
    else if (key.isKeyCode(juce::KeyPress::pageUpKey))
        steps = 10;
    else if (key.isKeyCode(juce::KeyPress::pageDownKey))
        steps = -10;

    if (steps != 0)
    {
        keyboardFocus_ = true;
        attachment_->setValueAsCompleteGesture(keyStepValue(*spec_, value_, steps, key.getModifiers().isShiftDown()));
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::returnKey))
    {
        showEditor();
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::deleteKey) || key.isKeyCode(juce::KeyPress::backspaceKey))
    {
        resetToDefault();
        return true;
    }
    return false;
}

void ParamControl::focusGained(FocusChangeType cause)
{
    keyboardFocus_ = cause == focusChangedByTabKey;
    repaint();
}

void ParamControl::focusLost(FocusChangeType)
{
    keyboardFocus_ = false;
    repaint();
}

void ParamControl::resetToDefault()
{
    if (attachment_ != nullptr && spec_ != nullptr && isEnabled())
        attachment_->setValueAsCompleteGesture(spec_->defaultValue);
}

void ParamControl::showEditor()
{
    if (editor_ != nullptr || spec_ == nullptr || !isEnabled())
        return;

    editor_ = std::make_unique<juce::TextEditor>();
    editor_->setTitle(getTitle());
    editor_->setFont(font(type::body, Weight::Regular, true));
    editor_->setJustification(juce::Justification::centred);
    editor_->setIndents(2, 0);
    editor_->setSelectAllWhenFocused(true);
    editor_->setText(toJuce(editText(*spec_, value_)), false);
    editor_->onReturnKey = [this] { hideEditor(true); };
    editor_->onEscapeKey = [this] { hideEditor(false); };
    editor_->onFocusLost = [this] { hideEditor(true); };
    addAndMakeVisible(*editor_);
    editor_->setBounds(valueArea().toNearestInt());
    editor_->grabKeyboardFocus();
    repaint();
}

void ParamControl::hideEditor(bool commit)
{
    if (editor_ == nullptr)
        return;

    // El campo se quita ahora, pero se destruye más tarde: esta función se
    // llama desde sus propios avisos (Intro, Esc, pérdida de foco).
    std::shared_ptr<juce::TextEditor> editor(std::move(editor_));
    const auto text = editor->getText();
    const bool hadFocus = editor->hasKeyboardFocus(true);
    editor->onReturnKey = nullptr;
    editor->onEscapeKey = nullptr;
    editor->onFocusLost = nullptr;
    removeChildComponent(editor.get());
    juce::MessageManager::callAsync([editor] {});

    if (commit)
        setTextFromUser(text); // si no se entiende, el valor se queda como estaba
    if (hadFocus && isShowing())
        grabKeyboardFocus();
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> ParamControl::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<ParamValueInterface>(*this)});
}

// ---------------------------------------------------------------------------
// ToggleSwitch

ToggleSwitch::ToggleSwitch(Style style, const juce::String& label, const juce::String& accessibleName)
    : juce::Button(accessibleName),
      style_(style)
{
    setButtonText(label);
    setTitle(accessibleName);
    setClickingTogglesState(true);
    setWantsKeyboardFocus(true);
}

ToggleSwitch::~ToggleSwitch() = default;

void ToggleSwitch::attach(juce::RangedAudioParameter& parameter)
{
    attachment_.reset();
    attachment_ = std::make_unique<juce::ButtonParameterAttachment>(parameter, *this);
}

void ToggleSwitch::detach()
{
    attachment_.reset();
}

void ToggleSwitch::setAccent(juce::Colour accent)
{
    if (accent_ == accent)
        return;
    accent_ = accent;
    repaint();
}

void ToggleSwitch::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    draw::ControlState state;
    state.hover = highlighted;
    state.pressed = down;
    state.enabled = isEnabled();
    state.focused = keyboardFocus_ && hasKeyboardFocus(false);

    auto bounds = getLocalBounds().toFloat();
    if (style_ == Style::Pill)
    {
        draw::pill(g, bounds, getButtonText(), getToggleState(), accent_, state);
        return;
    }

    if (getButtonText().isEmpty())
    {
        draw::toggle(g, bounds, getToggleState(), accent_, state);
        return;
    }
    draw::toggle(g, bounds.removeFromLeft(28.0f), getToggleState(), accent_, state);
    bounds.removeFromLeft(space::x2);
    g.setColour((getToggleState() ? colour::text : colour::textMuted).withMultipliedAlpha(isEnabled() ? 1.0f : 0.35f));
    g.setFont(font(type::body));
    g.drawText(getButtonText(), bounds, juce::Justification::centredLeft, false);
}

void ToggleSwitch::focusGained(FocusChangeType cause)
{
    keyboardFocus_ = cause == focusChangedByTabKey;
    repaint();
}

void ToggleSwitch::focusLost(FocusChangeType)
{
    keyboardFocus_ = false;
    repaint();
}

// ---------------------------------------------------------------------------
// Segmented

class Segmented::Segment final : public juce::Button
{
public:
    Segment(const Option& option, draw::Corners corners, const juce::Colour& accent)
        : juce::Button(option.accessibleName),
          text_(option.text),
          corners_(corners),
          accent_(accent)
    {
        setTitle(option.accessibleName);
        setClickingTogglesState(true);
        setRadioGroupId(1, juce::dontSendNotification);
        setWantsKeyboardFocus(true);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        draw::ControlState state;
        state.hover = highlighted;
        state.pressed = down;
        state.enabled = isEnabled();
        state.focused = keyboardFocus_ && hasKeyboardFocus(false);
        draw::pill(g, getLocalBounds().toFloat(), text_, getToggleState(), accent_, state, corners_);
    }

    void focusGained(FocusChangeType cause) override
    {
        keyboardFocus_ = cause == focusChangedByTabKey;
        repaint();
    }

    void focusLost(FocusChangeType) override
    {
        keyboardFocus_ = false;
        repaint();
    }

private:
    juce::String text_;
    draw::Corners corners_;
    const juce::Colour& accent_;
    bool keyboardFocus_ = false;
};

Segmented::Segmented(std::vector<Option> options, const juce::String& accessibleName)
{
    setTitle(accessibleName);
    setFocusContainerType(FocusContainerType::focusContainer);

    for (std::size_t i = 0; i < options.size(); ++i)
    {
        const auto corners = options.size() == 1 ? draw::Corners::All
                             : i == 0             ? draw::Corners::Left
                             : i + 1 == options.size() ? draw::Corners::Right
                                                       : draw::Corners::Middle;
        auto segment = std::make_unique<Segment>(options[i], corners, accent_);
        const int index = static_cast<int>(i);
        segment->onClick = [this, index] {
            if (segments_[static_cast<std::size_t>(index)]->getToggleState())
                choose(index);
        };
        addAndMakeVisible(*segment);
        segments_.push_back(std::move(segment));
    }
}

Segmented::~Segmented() = default;

void Segmented::attach(juce::RangedAudioParameter& parameter)
{
    attachment_.reset();
    attachment_ = std::make_unique<juce::ParameterAttachment>(
        parameter, [this](float value) { setSelected(juce::roundToInt(value)); });
    attachment_->sendInitialUpdate();
}

void Segmented::detach()
{
    attachment_.reset();
}

void Segmented::setAccent(juce::Colour accent)
{
    accent_ = accent;
    repaint();
}

void Segmented::setSelected(int index)
{
    selected_ = index;
    for (std::size_t i = 0; i < segments_.size(); ++i)
        segments_[i]->setToggleState(static_cast<int>(i) == index, juce::dontSendNotification);
}

void Segmented::choose(int index)
{
    if (index == selected_)
        return;
    selected_ = index;
    if (attachment_ != nullptr)
        attachment_->setValueAsCompleteGesture(static_cast<float>(index));
    if (onSelect)
        onSelect(index);
}

void Segmented::resized()
{
    if (segments_.empty())
        return;
    // Los segmentos se solapan un píxel para compartir el borde.
    const float width = static_cast<float>(getWidth() + static_cast<int>(segments_.size()) - 1) /
                        static_cast<float>(segments_.size());
    for (std::size_t i = 0; i < segments_.size(); ++i)
    {
        const int x0 = juce::roundToInt(static_cast<float>(i) * (width - 1.0f));
        const int x1 = i + 1 == segments_.size() ? getWidth() : juce::roundToInt(static_cast<float>(i + 1) * (width - 1.0f)) + 1;
        segments_[i]->setBounds(x0, 0, x1 - x0, getHeight());
    }
}

void Segmented::enablementChanged()
{
    for (auto& segment : segments_)
        segment->setEnabled(isEnabled());
}

// ---------------------------------------------------------------------------
// IconButton

IconButton::IconButton(draw::Icon icon, const juce::String& accessibleName, draw::Corners corners)
    : juce::Button(accessibleName),
      icon_(icon),
      corners_(corners)
{
    setTitle(accessibleName);
    setTooltip(accessibleName);
    setWantsKeyboardFocus(true);
}

void IconButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    draw::ControlState state;
    state.hover = highlighted;
    state.pressed = down;
    state.enabled = isEnabled();
    state.focused = keyboardFocus_ && hasKeyboardFocus(false);
    draw::iconButton(g, getLocalBounds().toFloat(), icon_, state, corners_, on_, badge_);
}

void IconButton::setOn(bool on)
{
    if (on == on_)
        return;
    on_ = on;
    repaint();
}

void IconButton::setBadge(const juce::String& badge)
{
    if (badge == badge_)
        return;
    badge_ = badge;
    repaint();
}

void IconButton::focusGained(FocusChangeType cause)
{
    keyboardFocus_ = cause == focusChangedByTabKey;
    repaint();
}

void IconButton::focusLost(FocusChangeType)
{
    keyboardFocus_ = false;
    repaint();
}

// ---------------------------------------------------------------------------
// Dropdown

Dropdown::Dropdown(const juce::String& accessibleName)
{
    setTitle(accessibleName);
    setScrollWheelEnabled(true);
}

Dropdown::~Dropdown() = default;

void Dropdown::attach(juce::RangedAudioParameter& parameter, const core::ParameterSpec& spec)
{
    attachment_.reset();
    if (getNumItems() != static_cast<int>(spec.choices.size()))
    {
        clear(juce::dontSendNotification);
        // Los identificadores (1..n) corresponden a los índices de las
        // opciones (0..n-1), como espera ComboBoxParameterAttachment.
        int id = 1;
        for (const auto choice : spec.choices)
            addItem(toJuce(choice), id++);
    }
    attachment_ = std::make_unique<juce::ComboBoxParameterAttachment>(parameter, *this);
}

void Dropdown::detach()
{
    attachment_.reset();
}

void Dropdown::focusGained(FocusChangeType cause)
{
    if (cause == focusChangedByTabKey)
        getProperties().set("keyboardFocus", true);
    juce::ComboBox::focusGained(cause);
    repaint();
}

void Dropdown::focusLost(FocusChangeType cause)
{
    getProperties().remove("keyboardFocus");
    juce::ComboBox::focusLost(cause);
    repaint();
}

} // namespace esceq::ui
