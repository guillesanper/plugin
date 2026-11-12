// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EqEditor.h"

#include "Draw.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include "esceq/core/parameters.h"

namespace esceq::ui {

namespace {

using namespace theme;

constexpr int kRefreshHz = 30;

juce::Rectangle<float> toJuce(const Rect& r)
{
    return {r.x, r.y, r.width, r.height};
}

juce::Rectangle<int> toInt(const Rect& r)
{
    return toJuce(r).toNearestInt();
}

std::vector<Segmented::Option> sizeOptions()
{
    return {{"S", "Small window"}, {"M", "Medium window"}, {"L", "Large window"}};
}

} // namespace

EqEditor::EqEditor(PluginProcessor& owner)
    : AudioProcessorEditor(owner),
      processor_(owner),
      curve_(owner),
      meter_(owner),
      panel_(owner),
      size_(sizeOptions(), "Window size")
{
    setLookAndFeel(&lookAndFeel_);
    setTitle("esceQ");
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);

    analyzer_.addItem("Analyzer off", 1);
    analyzer_.addItem("Analyzer post", 2);
    analyzer_.addItem("Analyzer pre + post", 3);
    analyzer_.setSelectedId(2, juce::dontSendNotification);
    analyzer_.onChange = [this] {
        const int id = analyzer_.getSelectedId();
        curve_.setAnalyzerMode(id == 1 ? AnalyzerMode::Off : id == 3 ? AnalyzerMode::PreAndPost : AnalyzerMode::Post);
    };
    addAndMakeVisible(analyzer_);

    for (int range : {6, 12, 18, 30})
        range_.addItem(juce::String::fromUTF8("\xC2\xB1") + juce::String(range) + " dB", range);
    range_.setSelectedId(18, juce::dontSendNotification);
    range_.onChange = [this] { curve_.setRangeDb(range_.getSelectedId()); };
    addAndMakeVisible(range_);

    size_.onSelect = [this](int index) {
        const auto size = windowSize(static_cast<SizePreset>(index));
        setSize(size.width, size.height);
    };
    addAndMakeVisible(size_);

    output_.attach(owner.parameter(core::kOutputGain), core::parameters()[core::kOutputGain]);
    addAndMakeVisible(output_);

    bypass_.attach(owner.parameter(core::kBypass));
    bypass_.setAccent(colour::warning);
    addAndMakeVisible(bypass_);

    curve_.onSelectionChanged = [this](int band) { panel_.setBand(band); };
    addAndMakeVisible(curve_);
    addAndMakeVisible(meter_);
    addAndMakeVisible(panel_);

    // Orden de tabulación: barra superior, curva y panel.
    int order = 1;
    for (juce::Component* c : std::initializer_list<juce::Component*>{&analyzer_, &range_, &size_, &output_, &bypass_, &curve_,
                                                                      &panel_})
        c->setExplicitFocusOrder(order++);

    setResizable(true, true);
    setResizeLimits(kMinWindow.width, kMinWindow.height, kMaxWindow.width, kMaxWindow.height);
    const auto size = windowSize(kDefaultSizePreset);
    setSize(size.width, size.height);

    startTimerHz(kRefreshHz);
}

EqEditor::~EqEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void EqEditor::paint(juce::Graphics& g)
{
    g.fillAll(colour::window);

    // Marca: un punto de banda sobre una curva, y el nombre con la Q en el color propio.
    auto logo = toJuce(topBar_.logo);
    const auto mark = logo.removeFromLeft(22.0f).withSizeKeepingCentre(22.0f, 22.0f);
    juce::Path wave;
    wave.startNewSubPath(mark.getX() + 1.0f, mark.getCentreY() + 4.0f);
    wave.cubicTo(mark.getX() + 7.0f, mark.getCentreY() + 4.0f, mark.getX() + 7.0f, mark.getCentreY() - 5.0f,
                 mark.getCentreX(), mark.getCentreY() - 5.0f);
    wave.cubicTo(mark.getRight() - 7.0f, mark.getCentreY() - 5.0f, mark.getRight() - 7.0f, mark.getCentreY() + 4.0f,
                 mark.getRight() - 1.0f, mark.getCentreY() + 4.0f);
    g.setColour(colour::textMuted);
    g.strokePath(wave, juce::PathStrokeType(stroke::thin, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colour::accent);
    g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({mark.getCentreX(), mark.getCentreY() - 5.0f}));

    logo.removeFromLeft(space::x2);
    const auto logoFont = font(type::logo, Weight::SemiBold);
    g.setFont(logoFont);
    g.setColour(colour::text);
    g.drawText("esce", logo, juce::Justification::centredLeft, false);
    g.setColour(colour::accent);
    g.drawText("Q", logo.withTrimmedLeft(juce::GlyphArrangement::getStringWidth(logoFont, "esce")),
               juce::Justification::centredLeft, false);

    draw::caption(g, "Out", toJuce(topBar_.output).withWidth(26.0f), juce::Justification::centredLeft);
}

void EqEditor::resized()
{
    layout_ = editorLayout(static_cast<float>(getWidth()), static_cast<float>(getHeight()));
    topBar_ = topBarLayout(layout_.topBar);

    analyzer_.setBounds(toInt(topBar_.analyzer));
    range_.setBounds(toInt(topBar_.range));
    size_.setBounds(toInt(topBar_.size));
    // La barra y su valor son un solo control; a su izquierda va la etiqueta "OUT".
    output_.setBounds(toInt(topBar_.output).withTrimmedLeft(26).getUnion(toInt(topBar_.outputValue)));
    bypass_.setBounds(toInt(topBar_.bypass));

    curve_.setBounds(toInt(layout_.curve));
    meter_.setBounds(toInt(layout_.meter));
    panel_.setBounds(toInt(layout_.panel));

    // El selector de tamaño marca el preset solo si la ventana lo cumple exactamente.
    const auto preset = matchingPreset({getWidth(), getHeight()});
    size_.setSelected(preset ? static_cast<int>(*preset) : -1);
}

void EqEditor::timerCallback()
{
    curve_.refresh();
    meter_.refresh();
    panel_.refresh();
}

} // namespace esceq::ui
