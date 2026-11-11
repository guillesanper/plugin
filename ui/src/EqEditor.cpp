// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EqEditor.h"

#include "Palette.h"
#include "PluginProcessor.h"

#include "esceq/core/parameters.h"

namespace esceq::ui {

namespace {

constexpr int kHeaderHeight = 40;
constexpr int kControlsHeight = 140;
constexpr int kRefreshHz = 30;

juce::String idOf(std::size_t index)
{
    const auto id = core::parameters()[index].id;
    return juce::String::fromUTF8(id.data(), static_cast<int>(id.size()));
}

} // namespace

EqEditor::EqEditor(PluginProcessor& owner)
    : AudioProcessorEditor(owner),
      processor_(owner),
      curve_(owner),
      controls_(owner)
{
    setLookAndFeel(&lookAndFeel_);

    title_.setText("esceQ", juce::dontSendNotification);
    title_.setFont(juce::FontOptions(20.0f, juce::Font::bold));
    title_.setColour(juce::Label::textColourId, palette::curve);
    addAndMakeVisible(title_);

    analyzer_.addItem("Analyzer: off", 1);
    analyzer_.addItem("Analyzer: post", 2);
    analyzer_.addItem("Analyzer: pre + post", 3);
    analyzer_.setSelectedId(2, juce::dontSendNotification);
    analyzer_.onChange = [this] {
        const int id = analyzer_.getSelectedId();
        curve_.setAnalyzerMode(id == 1 ? AnalyzerMode::Off : id == 3 ? AnalyzerMode::PreAndPost : AnalyzerMode::Post);
    };
    addAndMakeVisible(analyzer_);

    for (int range : {6, 12, 18, 30})
        range_.addItem("Range: " + juce::String(range) + " dB", range);
    range_.setSelectedId(18, juce::dontSendNotification);
    range_.onChange = [this] { curve_.setRangeDb(range_.getSelectedId()); };
    addAndMakeVisible(range_);

    output_.setSliderStyle(juce::Slider::LinearHorizontal);
    output_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    addAndMakeVisible(output_);
    outputLabel_.setText("Output", juce::dontSendNotification);
    outputLabel_.setColour(juce::Label::textColourId, palette::textDim);
    outputLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(outputLabel_);
    addAndMakeVisible(bypass_);

    outputAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        owner.state(), idOf(core::kOutputGain), output_);
    bypassAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        owner.state(), idOf(core::kBypass), bypass_);

    curve_.onSelectionChanged = [this](int band) { controls_.setBand(band); };
    addAndMakeVisible(curve_);
    addAndMakeVisible(controls_);

    setResizable(true, true);
    setResizeLimits(900, 460, 2400, 1400);
    setSize(1100, 620);

    startTimerHz(kRefreshHz);
}

EqEditor::~EqEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void EqEditor::paint(juce::Graphics& g)
{
    g.fillAll(palette::panel);
}

void EqEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop(kHeaderHeight).reduced(10, 7);
    title_.setBounds(header.removeFromLeft(90));
    bypass_.setBounds(header.removeFromRight(80));
    header.removeFromRight(8);
    output_.setBounds(header.removeFromRight(200));
    outputLabel_.setBounds(header.removeFromRight(56));
    header.removeFromRight(12);
    range_.setBounds(header.removeFromRight(130));
    header.removeFromRight(8);
    analyzer_.setBounds(header.removeFromRight(170));

    controls_.setBounds(area.removeFromBottom(kControlsHeight));
    curve_.setBounds(area.reduced(8, 0).withTrimmedBottom(8));
}

void EqEditor::timerCallback()
{
    curve_.refresh();
    controls_.refresh();
}

} // namespace esceq::ui
