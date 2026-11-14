// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EqEditor.h"

#include "Draw.h"
#include "JuceConvert.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include "esceq/core/parameters.h"
#include "esceq/ui/session_overlay.h"

#include <algorithm>

namespace esceq::ui {

namespace {

using namespace theme;

constexpr int kRefreshHz = 30;

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
      presets_(owner),
      size_(sizeOptions(), "Window size")
{
    setLookAndFeel(&lookAndFeel_);
    setTitle("esceQ");
    setFocusContainerType(FocusContainerType::keyboardFocusContainer);

    analyzer_.addItem("Analyzer off", 1);
    analyzer_.addItem("Analyzer post", 2);
    analyzer_.addItem("Analyzer pre + post", 3);
    analyzer_.onChange = [this] {
        const int id = analyzer_.getSelectedId();
        curve_.setAnalyzerMode(id == 1 ? AnalyzerMode::Off : id == 3 ? AnalyzerMode::PreAndPost : AnalyzerMode::Post);
        storeUiSettings();
    };
    addAndMakeVisible(analyzer_);

    compare_.setTooltip("Compare with other esceQ instances: overlay their spectrum and show where it collides with this one");
    compare_.onClick = [this] { showCompareMenu(); };
    addAndMakeVisible(compare_);
    updateCompareButton();

    for (int range : {6, 12, 18, 30})
        range_.addItem(juce::String::fromUTF8("\xC2\xB1") + juce::String(range) + " dB", range);
    range_.onChange = [this] {
        curve_.setRangeDb(range_.getSelectedId());
        storeUiSettings();
    };
    addAndMakeVisible(range_);

    size_.onSelect = [this](int index) {
        const auto size = windowSize(static_cast<SizePreset>(index));
        setSize(size.width, size.height);
    };
    addAndMakeVisible(size_);

    undo_.onClick = [this] { processor_.history().undo(); };
    redo_.onClick = [this] { processor_.history().redo(); };
    undo_.setTooltip("Undo (Ctrl+Z)");
    redo_.setTooltip("Redo (Ctrl+Shift+Z)");
    addAndMakeVisible(presets_);
    addAndMakeVisible(undo_);
    addAndMakeVisible(redo_);

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
    for (juce::Component* c : std::initializer_list<juce::Component*>{&presets_, &undo_, &redo_, &compare_, &analyzer_, &range_, &size_, &output_, &bypass_, &curve_,
                                                                      &panel_})
        c->setExplicitFocusOrder(order++);

    setResizable(true, true);
    setResizeLimits(kMinWindow.width, kMinWindow.height, kMaxWindow.width, kMaxWindow.height);
    setWantsKeyboardFocus(true);

    // Preferencias guardadas con el proyecto: analizador, rango y tamaño.
    applyUiSettings();

    startTimerHz(kRefreshHz);
}

void EqEditor::applyUiSettings()
{
    restoring_ = true;
    stateRevision_ = processor_.stateRevision();
    const auto ui = processor_.uiSettings();

    analyzer_.setSelectedId(ui.analyzerMode + 1, juce::sendNotificationSync);
    range_.setSelectedId(ui.displayRangeDb, juce::sendNotificationSync);

    const auto size = ui.windowWidth > 0 ? clampWindow({ui.windowWidth, ui.windowHeight}) : windowSize(kDefaultSizePreset);
    setSize(size.width, size.height);
    restoring_ = false;
}

void EqEditor::storeUiSettings()
{
    if (restoring_)
        return;
    auto ui = processor_.uiSettings();
    ui.analyzerMode = analyzer_.getSelectedId() - 1;
    ui.displayRangeDb = range_.getSelectedId();
    ui.windowWidth = getWidth();
    ui.windowHeight = getHeight();
    processor_.setUiSettings(ui);
}

bool EqEditor::keyPressed(const juce::KeyPress& key)
{
    // Ctrl/Cmd+Z deshace; Ctrl/Cmd+Mayús+Z y Ctrl/Cmd+Y rehacen. Muchos hosts
    // se quedan con estas teclas: por eso están también los botones.
    const auto mods = key.getModifiers();
    if (!mods.isCommandDown())
        return false;
    const bool z = key.isKeyCode('z') || key.isKeyCode('Z');
    const bool y = key.isKeyCode('y') || key.isKeyCode('Y');
    if (z && !mods.isShiftDown())
    {
        processor_.history().undo();
        return true;
    }
    if ((z && mods.isShiftDown()) || y)
    {
        processor_.history().redo();
        return true;
    }
    return false;
}

EqEditor::~EqEditor()
{
    stopTimer();
    // Los controles cierran sus gestos al destruirse; esto es la red de
    // seguridad para que el historial nunca se quede esperando uno.
    processor_.history().abandonOpenGestures();
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

    presets_.setBounds(toInt(topBar_.presets));
    undo_.setBounds(toInt(topBar_.undo));
    redo_.setBounds(toInt(topBar_.redo).withTrimmedLeft(-1)); // comparten el borde central
    compare_.setBounds(toInt(topBar_.compare));
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

    storeUiSettings();
}

void EqEditor::timerCallback()
{
    // El host ha cargado un estado con la ventana abierta: se aplican sus preferencias.
    if (processor_.stateRevision() != stateRevision_)
        applyUiSettings();

    curve_.refresh();
    meter_.refresh();
    panel_.refresh();
    presets_.refresh();
    updateCompareButton(); // la curva quita de la selección las instancias que se van
    undo_.setEnabled(processor_.history().canUndo());
    redo_.setEnabled(processor_.history().canRedo());
}

void EqEditor::updateCompareButton()
{
    const auto& selection = processor_.compareSelection();
    compare_.setOn(!selection.shown.empty());
    compare_.setBadge(toJuce(compareBadge(selection)));
}

void EqEditor::showCompareMenu()
{
    constexpr int kCollisionsId = 1;
    constexpr int kAllId = 2;
    constexpr int kNoneId = 3;
    constexpr int kFirstInstanceId = 100;

    std::vector<core::InstanceSpectrum> instances;
    processor_.spectrumSession().snapshot(instances);
    auto& selection = processor_.compareSelection();
    selection.retainExisting(instances);
    const auto items = compareMenuItems(instances, processor_.instanceId(), selection);

    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    menu.addSectionHeader("Compare with");
    if (items.empty())
        menu.addItem(-1, "No other esceQ in this project", false);
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        juce::PopupMenu::Item entry(toJuce(item.name) + (item.hasSignal ? "" : "  (silent)"));
        entry.itemID = kFirstInstanceId + static_cast<int>(i);
        entry.isTicked = item.ticked;
        const auto it = std::find_if(instances.begin(), instances.end(), [&](const auto& s) { return s.id == item.id; });
        if (it != instances.end())
            entry.colour = theme::instance(it->number, it->trackColour);
        menu.addItem(std::move(entry));
    }
    menu.addSeparator();
    menu.addItem(kAllId, "Show all", std::any_of(items.begin(), items.end(), [](const auto& i) { return !i.ticked; }));
    menu.addItem(kNoneId, "Hide all", !selection.shown.empty());
    menu.addSeparator();
    menu.addItem(kCollisionsId, "Highlight collisions", true, selection.showCollisions);

    juce::Component::SafePointer<EqEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&compare_), [safe, items](int result) {
        if (safe == nullptr || result <= 0)
            return;
        auto& chosen = safe->processor_.compareSelection();
        if (result == kCollisionsId)
            chosen.showCollisions = !chosen.showCollisions;
        else if (result == kAllId)
        {
            for (const auto& item : items)
                if (!chosen.contains(item.id))
                    chosen.toggle(item.id);
        }
        else if (result == kNoneId)
            chosen.shown.clear();
        else if (const auto index = static_cast<std::size_t>(result - kFirstInstanceId); index < items.size())
            chosen.toggle(items[index].id);
        safe->updateCompareButton();
        safe->curve_.compareChanged();
    });
}

} // namespace esceq::ui
