// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MatchPanel.h"

#include "Draw.h"
#include "JuceConvert.h"
#include "Theme.h"

#include <algorithm>
#include <cmath>

namespace esceq::ui {

namespace {

using namespace theme;

/// Amount, en tantos por ciento: lo que se arrastra y se escribe.
constexpr core::ParameterSpec kAmountSpec{
    "", "Amount", "%", core::ParameterKind::Continuous, 0.0f, 100.0f, 50.0f, core::ParameterScale::Linear, {}};

/// Ancho del texto de los segundos útiles ("10.0 s / 10 s"), a la derecha de su barra.
constexpr float kProgressTextWidth = 84.0f;

std::vector<Segmented::Option> smoothingOptions()
{
    std::vector<Segmented::Option> options;
    for (const auto& option : kMatchSmoothingOptions)
        options.push_back({toJuce(std::string(option.text)), toJuce(std::string(option.accessibleName))});
    return options;
}

juce::Rectangle<int> toInt(const Rect& r)
{
    return toJuce(r).toNearestInt();
}

} // namespace

MatchPanel::MatchPanel()
    : smoothing_(smoothingOptions(), "Smoothing")
{
    setTitle("Match");
    setDescription("Match this track to a reference");
    setWantsKeyboardFocus(true);
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);

    close_.setTooltip("Close (Esc). What was learned is kept while the plugin is open");
    close_.onClick = [this] {
        if (onClose)
            onClose();
    };

    reference_.setTooltip("Reference: another esceQ in this project (its output), or the sidechain input");
    reference_.setTextWhenNothingSelected("No reference");
    reference_.setTextWhenNoChoicesAvailable("No reference");
    reference_.onChange = [this] {
        const int index = reference_.getSelectedId() - 1;
        if (index >= 0 && static_cast<std::size_t>(index) < options_.size() && onReference)
            onReference(options_[static_cast<std::size_t>(index)].reference);
    };

    learn_.setClickingTogglesState(false);
    learn_.onClick = [this] {
        if (onLearn)
            onLearn();
    };

    amount_.setTooltip("How much of the difference is matched");
    amount_.bindLocal(kAmountSpec, 50.0f,
                      [this](float percent) {
                          settings_.amount = static_cast<double>(percent) / 100.0;
                          sendSettings();
                      },
                      {[](float percent) { return toJuce(matchAmountText(static_cast<double>(percent) / 100.0)); },
                       [](const juce::String& text) -> std::optional<float> {
                           const auto amount = parseMatchAmount(text.toStdString());
                           if (!amount)
                               return std::nullopt;
                           return static_cast<float>(*amount * 100.0);
                       }});

    smoothing_.setTooltip("Smoothing of both spectra before they are compared");
    smoothing_.onSelect = [this](int index) {
        settings_.smoothing = static_cast<core::MatchSmoothing>(index);
        sendSettings();
    };

    bands_.setTooltip("Bands that can be created, at most the free ones");
    bands_.onChange = [this] {
        const int count = bands_.getSelectedId();
        if (count > 0)
        {
            settings_.maxBands = count;
            sendSettings();
        }
    };

    low_.setTooltip("Lowest frequency that is matched (a note can be typed: A1)");
    high_.setTooltip("Highest frequency that is matched (a note can be typed: C8)");
    low_.bindLocal(matchRangeSpec(false), static_cast<float>(settings_.lowHz), [this](float hz) {
        settings_ = withMatchRange(settings_, false, static_cast<double>(hz));
        sendSettings();
    });
    high_.bindLocal(matchRangeSpec(true), static_cast<float>(settings_.highHz), [this](float hz) {
        settings_ = withMatchRange(settings_, true, static_cast<double>(hz));
        sendSettings();
    });

    apply_.setClickingTogglesState(false);
    apply_.onClick = [this] {
        if (onApply)
            onApply();
    };

    int order = 1;
    for (juce::Component* c : std::initializer_list<juce::Component*>{&reference_, &learn_, &amount_, &smoothing_,
                                                                     &bands_, &low_, &high_, &apply_, &close_})
    {
        c->setExplicitFocusOrder(order++);
        addAndMakeVisible(*c);
    }
}

MatchPanel::~MatchPanel() = default;

void MatchPanel::setPanelLayout(const MatchPanelLayout& layout)
{
    layout_ = layout;
    setBounds(toInt(layout.card));
    resized();
}

Rect MatchPanel::local(const Rect& r) const noexcept
{
    return {r.x - layout_.card.x, r.y - layout_.card.y, r.width, r.height};
}

void MatchPanel::resized()
{
    close_.setBounds(toInt(local(layout_.close)));
    reference_.setBounds(toInt(local(layout_.reference)));
    learn_.setBounds(toInt(local(layout_.learn)));
    amount_.setBounds(toInt(local(layout_.amount)));
    smoothing_.setBounds(toInt(local(layout_.smoothing)));
    bands_.setBounds(toInt(local(layout_.bands)));
    low_.setBounds(toInt(local(layout_.rangeLow)));
    high_.setBounds(toInt(local(layout_.rangeHigh)));
    apply_.setBounds(toInt(local(layout_.apply)));
}

void MatchPanel::setA4(double a4Hz)
{
    low_.setA4(a4Hz);
    high_.setA4(a4Hz);
}

void MatchPanel::sendSettings()
{
    if (onSettings)
        onSettings(settings_);
}

void MatchPanel::update(const MatchStatus& status, const core::MatchSettings& settings, int freeBands,
                        int proposedBands)
{
    // Referencias: se rehacen solo si cambian (abrir el desplegable no debe parpadear).
    if (status.options != options_)
    {
        options_ = status.options;
        reference_.clear(juce::dontSendNotification);
        for (std::size_t i = 0; i < options_.size(); ++i)
        {
            const int id = static_cast<int>(i) + 1;
            reference_.addItem(toJuce(options_[i].text()), id);
            reference_.setItemEnabled(id, options_[i].enabled);
        }
    }
    const auto chosen = std::find_if(options_.begin(), options_.end(),
                                     [&](const auto& option) { return option.reference == status.reference; });
    const int chosenId = chosen != options_.end() ? static_cast<int>(chosen - options_.begin()) + 1 : 0;
    if (reference_.getSelectedId() != chosenId)
        reference_.setSelectedId(chosenId, juce::dontSendNotification);

    learn_.setButtonText(toJuce(matchLearnText(status.learning)));
    learn_.setToggleState(status.learning, juce::dontSendNotification);
    learn_.setEnabled(status.canLearn);
    learn_.setTooltip(toJuce(matchLearnTooltip(status)));

    settings_ = settings;
    amount_.setLocalValue(static_cast<float>(settings.amount * 100.0));
    smoothing_.setSelected(static_cast<int>(settings.smoothing));
    const int limit = matchMaxBandsLimit(freeBands);
    if (limit != bandsLimit_)
    {
        bandsLimit_ = limit;
        bands_.clear(juce::dontSendNotification);
        for (int count = 1; count <= limit; ++count)
            bands_.addItem(juce::String(count), count);
    }
    const int shown = matchMaxBandsShown(settings, freeBands);
    if (bands_.getSelectedId() != shown)
        bands_.setSelectedId(shown, juce::dontSendNotification);
    low_.setLocalValue(static_cast<float>(settings.lowHz));
    high_.setLocalValue(static_cast<float>(settings.highHz));

    apply_.setEnabled(status.canApply);
    apply_.setTooltip(toJuce(matchApplyTooltip(status, proposedBands)));

    const auto line = matchStatusLine(status, proposedBands);
    const bool repaintNeeded = line != statusLine_ || status.progress != status_.progress ||
                               status.learning != status_.learning;
    statusLine_ = line;
    status_ = status;
    if (repaintNeeded)
        repaint();
}

void MatchPanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    draw::card(g, bounds);

    g.setColour(colour::text);
    g.setFont(font(type::title, Weight::SemiBold));
    g.drawText(toJuce(std::string(kMatchTitle)), toJuce(local(layout_.title)), juce::Justification::centredLeft, false);

    for (const auto& [text, rect] :
         {std::pair{"Reference", layout_.referenceLabel}, std::pair{"Amount", layout_.amountLabel},
          std::pair{"Smoothing", layout_.smoothingLabel}, std::pair{"Max bands", layout_.bandsLabel},
          std::pair{"Range", layout_.rangeLabel}})
        draw::caption(g, text, toJuce(local(rect)), juce::Justification::centredLeft);

    // Segundos útiles: una barra fina y el texto, que llena con los 10 s que pide Apply.
    auto progress = toJuce(local(layout_.progress));
    const auto text = progress.removeFromRight(kProgressTextWidth);
    progress.removeFromRight(space::x2);
    draw::ControlState state;
    state.dimmed = !status_.learning;
    draw::progress(g, progress.withSizeKeepingCentre(progress.getWidth(), 4.0f),
                   matchProgressFraction(status_.usefulSeconds), colour::accent, state);
    g.setColour(status_.learning ? colour::text : colour::textMuted);
    g.setFont(font(type::body, Weight::Regular, true));
    g.drawText(toJuce(status_.progress), text, juce::Justification::centredRight, false);

    g.setColour(colour::textDim);
    g.drawText(juce::String::fromUTF8("\xE2\x80\x93"), toJuce(local(layout_.rangeDash)), juce::Justification::centred,
               false);

    g.setColour(statusLine_.warning ? colour::warning : colour::textMuted);
    g.setFont(font(type::small));
    g.drawText(toJuce(statusLine_.text), toJuce(local(layout_.status)), juce::Justification::centredLeft, true);
}

bool MatchPanel::keyPressed(const juce::KeyPress& key)
{
    if (key.isKeyCode(juce::KeyPress::escapeKey))
    {
        if (onClose)
            onClose();
        return true;
    }
    return false;
}

} // namespace esceq::ui
