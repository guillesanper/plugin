// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BandControls.h"

#include "Palette.h"
#include "PluginProcessor.h"

#include "esceq/core/band_model.h"
#include "esceq/core/parameters.h"

#include <initializer_list>
#include <utility>

namespace esceq::ui {

namespace {

constexpr int kLeftColumnWidth = 160;
constexpr int kDynamicColumnWidth = 104;
constexpr int kMaxKnobWidth = 96;

juce::String toJuce(std::string_view text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

juce::String idOf(int band, core::BandParam param)
{
    return toJuce(core::parameters()[core::bandParameter(band, param)].id);
}

juce::String idOf(int band, core::DynParam param)
{
    return toJuce(core::parameters()[core::dynamicParameter(band, param)].id);
}

void setupKnob(juce::Slider& slider, juce::Label& label, const juce::String& name, juce::Component& parent)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 18);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    parent.addAndMakeVisible(slider);

    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, palette::textDim);
    parent.addAndMakeVisible(label);
}

void fillChoices(juce::ComboBox& box, std::span<const std::string_view> choices)
{
    // Los identificadores de los elementos (1..n) corresponden a los índices
    // de las opciones (0..n-1), como espera ComboBoxAttachment.
    int id = 1;
    for (const auto name : choices)
        box.addItem(toJuce(name), id++);
}

} // namespace

BandControls::BandControls(PluginProcessor& processor)
    : processor_(processor)
{
    title_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    addAndMakeVisible(title_);

    hint_.setText("Double-click the curve to add a band. Drag nodes to move them, use the wheel for Q, "
                  "double-click a node to remove it, right-click for its type and dynamics.",
                  juce::dontSendNotification);
    hint_.setColour(juce::Label::textColourId, palette::textDim);
    hint_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(hint_);

    addAndMakeVisible(enabled_);
    fillChoices(type_, core::kFilterTypeChoices);
    fillChoices(slope_, core::kSlopeChoices);
    addAndMakeVisible(type_);
    addAndMakeVisible(slope_);

    setupKnob(frequency_, frequencyLabel_, "FREQ", *this);
    setupKnob(gain_, gainLabel_, "GAIN", *this);
    setupKnob(q_, qLabel_, "Q", *this);

    addAndMakeVisible(dynamic_);
    fillChoices(source_, core::kSidechainChoices);
    addAndMakeVisible(source_);
    sourceWarning_.setText("No sidechain", juce::dontSendNotification);
    sourceWarning_.setFont(juce::FontOptions(11.0f));
    sourceWarning_.setColour(juce::Label::textColourId, palette::warning);
    sourceWarning_.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(sourceWarning_);

    setupKnob(threshold_, thresholdLabel_, "THRESH", *this);
    setupKnob(range_, rangeLabel_, "RANGE", *this);
    setupKnob(attack_, attackLabel_, "ATTACK", *this);
    setupKnob(release_, releaseLabel_, "RELEASE", *this);
    setupKnob(knee_, kneeLabel_, "KNEE", *this);

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
    dynamicAttachment_.reset();
    sourceAttachment_.reset();
    thresholdAttachment_.reset();
    rangeAttachment_.reset();
    attackAttachment_.reset();
    releaseAttachment_.reset();
    kneeAttachment_.reset();

    const bool hasBand = band >= 0;
    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &enabled_, &type_, &slope_, &frequency_, &gain_, &q_, &frequencyLabel_, &gainLabel_, &qLabel_, &title_,
             &dynamic_, &source_, &threshold_, &range_, &attack_, &release_, &knee_, &thresholdLabel_, &rangeLabel_,
             &attackLabel_, &releaseLabel_, &kneeLabel_})
        c->setVisible(hasBand);
    hint_.setVisible(!hasBand);
    sourceWarning_.setVisible(false);

    if (hasBand)
    {
        auto& state = processor_.state();
        using core::BandParam;
        using core::DynParam;
        enabledAttachment_ = std::make_unique<Apvts::ButtonAttachment>(state, idOf(band, BandParam::Enabled), enabled_);
        typeAttachment_ = std::make_unique<Apvts::ComboBoxAttachment>(state, idOf(band, BandParam::Type), type_);
        slopeAttachment_ = std::make_unique<Apvts::ComboBoxAttachment>(state, idOf(band, BandParam::Slope), slope_);
        frequencyAttachment_ = std::make_unique<Apvts::SliderAttachment>(state, idOf(band, BandParam::Frequency), frequency_);
        gainAttachment_ = std::make_unique<Apvts::SliderAttachment>(state, idOf(band, BandParam::Gain), gain_);
        qAttachment_ = std::make_unique<Apvts::SliderAttachment>(state, idOf(band, BandParam::Q), q_);

        dynamicAttachment_ = std::make_unique<Apvts::ButtonAttachment>(state, idOf(band, DynParam::Enabled), dynamic_);
        sourceAttachment_ = std::make_unique<Apvts::ComboBoxAttachment>(state, idOf(band, DynParam::Source), source_);
        thresholdAttachment_ = std::make_unique<Apvts::SliderAttachment>(state, idOf(band, DynParam::Threshold), threshold_);
        rangeAttachment_ = std::make_unique<Apvts::SliderAttachment>(state, idOf(band, DynParam::Range), range_);
        attackAttachment_ = std::make_unique<Apvts::SliderAttachment>(state, idOf(band, DynParam::Attack), attack_);
        releaseAttachment_ = std::make_unique<Apvts::SliderAttachment>(state, idOf(band, DynParam::Release), release_);
        kneeAttachment_ = std::make_unique<Apvts::SliderAttachment>(state, idOf(band, DynParam::Knee), knee_);

        title_.setText("Band " + juce::String(band + 1), juce::dontSendNotification);
        title_.setColour(juce::Label::textColourId, palette::band(band));
        const auto colour = palette::band(band);
        for (juce::Slider* s : {&frequency_, &gain_, &q_, &threshold_, &range_, &attack_, &release_, &knee_})
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

    // La dinámica solo actúa en los tipos con ganancia; sus controles se
    // atenúan también mientras está apagada, pero siguen siendo editables.
    const bool supported = core::usesDynamics(type);
    const bool active = supported && settings.dynamics.enabled;
    dynamic_.setEnabled(supported);
    source_.setEnabled(supported);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&threshold_, &range_, &attack_, &release_, &knee_,
                                                                       &thresholdLabel_, &rangeLabel_, &attackLabel_,
                                                                       &releaseLabel_, &kneeLabel_})
        c->setAlpha(active ? 1.0f : 0.45f);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&threshold_, &range_, &attack_, &release_, &knee_})
        c->setEnabled(supported);

    // Aviso útil en FL Studio: detección externa elegida, pero sin nada conectado.
    sourceWarning_.setVisible(active && settings.dynamics.external && !processor_.sidechainConnected());
}

void BandControls::paint(juce::Graphics& g)
{
    g.fillAll(palette::panel);
    g.setColour(palette::gridStrong);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));

    if (band_ >= 0 && !dynamicSeparator_.isEmpty())
    {
        g.setColour(palette::grid);
        g.fillRect(dynamicSeparator_);
    }
}

void BandControls::resized()
{
    auto area = getLocalBounds().reduced(12, 8);
    hint_.setBounds(area);

    auto left = area.removeFromLeft(kLeftColumnWidth);
    title_.setBounds(left.removeFromTop(26));
    enabled_.setBounds(left.removeFromTop(26));
    left.removeFromTop(4);
    type_.setBounds(left.removeFromTop(24));
    left.removeFromTop(6);
    slope_.setBounds(left.removeFromTop(24));

    area.removeFromLeft(12);
    const int knobWidth = std::min(kMaxKnobWidth, (area.getWidth() - kDynamicColumnWidth - 24) / 8);
    auto placeKnob = [&area, knobWidth](juce::Slider& slider, juce::Label& label) {
        auto column = area.removeFromLeft(knobWidth);
        label.setBounds(column.removeFromTop(16));
        slider.setBounds(column);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, knobWidth - 4, 18);
    };

    placeKnob(frequency_, frequencyLabel_);
    placeKnob(gain_, gainLabel_);
    placeKnob(q_, qLabel_);

    area.removeFromLeft(10);
    dynamicSeparator_ = area.removeFromLeft(1).withTrimmedTop(4).withTrimmedBottom(4);
    area.removeFromLeft(12);

    auto dynamicColumn = area.removeFromLeft(kDynamicColumnWidth);
    dynamicColumn.removeFromTop(26);
    dynamic_.setBounds(dynamicColumn.removeFromTop(26));
    dynamicColumn.removeFromTop(4);
    source_.setBounds(dynamicColumn.removeFromTop(24));
    sourceWarning_.setBounds(dynamicColumn.removeFromTop(20));
    area.removeFromLeft(4);

    placeKnob(threshold_, thresholdLabel_);
    placeKnob(range_, rangeLabel_);
    placeKnob(attack_, attackLabel_);
    placeKnob(release_, releaseLabel_);
    placeKnob(knee_, kneeLabel_);
}

} // namespace esceq::ui
