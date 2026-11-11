// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BandControls.h"

#include "Palette.h"
#include "PluginProcessor.h"

#include "esceq/core/band_model.h"
#include "esceq/core/parameters.h"

namespace esceq::ui {

namespace {

juce::String toJuce(std::string_view text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

juce::String idOf(int band, core::BandParam param)
{
    return toJuce(core::parameters()[core::bandParameter(band, param)].id);
}

void setupKnob(juce::Slider& slider, juce::Label& label, const juce::String& name, juce::Component& parent)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    parent.addAndMakeVisible(slider);

    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, palette::textDim);
    parent.addAndMakeVisible(label);
}

} // namespace

BandControls::BandControls(PluginProcessor& processor)
    : processor_(processor)
{
    title_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    addAndMakeVisible(title_);

    hint_.setText("Double-click the curve to add a band. Drag nodes to move them, use the wheel for Q, "
                  "double-click a node to remove it, right-click for its type.",
                  juce::dontSendNotification);
    hint_.setColour(juce::Label::textColourId, palette::textDim);
    hint_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(hint_);

    addAndMakeVisible(enabled_);

    // Los identificadores de los elementos (1..n) corresponden a los índices
    // de las opciones (0..n-1), como espera ComboBoxAttachment.
    int id = 1;
    for (const auto name : core::kFilterTypeChoices)
        type_.addItem(toJuce(name), id++);
    id = 1;
    for (const auto name : core::kSlopeChoices)
        slope_.addItem(toJuce(name), id++);
    addAndMakeVisible(type_);
    addAndMakeVisible(slope_);

    setupKnob(frequency_, frequencyLabel_, "FREQ", *this);
    setupKnob(gain_, gainLabel_, "GAIN", *this);
    setupKnob(q_, qLabel_, "Q", *this);

    setBand(-1);
}

BandControls::~BandControls() = default;

void BandControls::setBand(int band)
{
    band_ = band;

    // Primero se sueltan los enlaces antiguos: dos enlaces sobre el mismo
    // control lo dejarían escribiendo en dos parámetros.
    enabledAttachment_.reset();
    typeAttachment_.reset();
    slopeAttachment_.reset();
    frequencyAttachment_.reset();
    gainAttachment_.reset();
    qAttachment_.reset();

    const bool hasBand = band >= 0;
    for (juce::Component* c : std::initializer_list<juce::Component*>{&enabled_, &type_, &slope_, &frequency_, &gain_, &q_,
                                                                       &frequencyLabel_, &gainLabel_, &qLabel_, &title_})
        c->setVisible(hasBand);
    hint_.setVisible(!hasBand);

    if (hasBand)
    {
        auto& state = processor_.state();
        enabledAttachment_ = std::make_unique<Apvts::ButtonAttachment>(state, idOf(band, core::BandParam::Enabled), enabled_);
        typeAttachment_ = std::make_unique<Apvts::ComboBoxAttachment>(state, idOf(band, core::BandParam::Type), type_);
        slopeAttachment_ = std::make_unique<Apvts::ComboBoxAttachment>(state, idOf(band, core::BandParam::Slope), slope_);
        frequencyAttachment_ =
            std::make_unique<Apvts::SliderAttachment>(state, idOf(band, core::BandParam::Frequency), frequency_);
        gainAttachment_ = std::make_unique<Apvts::SliderAttachment>(state, idOf(band, core::BandParam::Gain), gain_);
        qAttachment_ = std::make_unique<Apvts::SliderAttachment>(state, idOf(band, core::BandParam::Q), q_);

        title_.setText("Band " + juce::String(band + 1), juce::dontSendNotification);
        title_.setColour(juce::Label::textColourId, palette::band(band));
        const auto colour = palette::band(band);
        for (juce::Slider* s : {&frequency_, &gain_, &q_})
        {
            s->setColour(juce::Slider::rotarySliderFillColourId, colour);
            s->setColour(juce::Slider::thumbColourId, colour);
        }
    }

    refresh();
    repaint();
}

void BandControls::refresh()
{
    if (band_ < 0)
        return;

    const auto settings = core::bandSettings(processor_.currentValues(), band_);
    const auto type = settings.filter.type;
    gain_.setEnabled(core::usesGain(type));
    gainLabel_.setEnabled(core::usesGain(type));
    slope_.setEnabled(core::usesSlope(type));
    q_.setEnabled(core::usesQ(type, settings.filter.slopeDbPerOct));
    qLabel_.setEnabled(q_.isEnabled());
}

void BandControls::paint(juce::Graphics& g)
{
    g.fillAll(palette::panel);
    g.setColour(palette::gridStrong);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));
}

void BandControls::resized()
{
    auto area = getLocalBounds().reduced(12, 8);
    hint_.setBounds(area);

    auto left = area.removeFromLeft(170);
    title_.setBounds(left.removeFromTop(26));
    enabled_.setBounds(left.removeFromTop(26));
    left.removeFromTop(4);
    type_.setBounds(left.removeFromTop(24));
    left.removeFromTop(6);
    slope_.setBounds(left.removeFromTop(24));

    area.removeFromLeft(16);
    const int knobWidth = std::min(110, area.getWidth() / 3);
    for (auto [slider, label] : {std::pair{&frequency_, &frequencyLabel_}, {&gain_, &gainLabel_}, {&q_, &qLabel_}})
    {
        auto column = area.removeFromLeft(knobWidth);
        label->setBounds(column.removeFromTop(16));
        slider->setBounds(column);
    }
}

} // namespace esceq::ui
