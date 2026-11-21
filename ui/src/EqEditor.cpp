// SPDX-License-Identifier: AGPL-3.0-or-later
#include "EqEditor.h"

#include "Draw.h"
#include "JuceConvert.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include "esceq/core/band_model.h"
#include "esceq/core/parameters.h"
#include "esceq/dsp/auto_gain.h"
#include "esceq/ui/control_logic.h"
#include "esceq/ui/notes.h"
#include "esceq/ui/options_menu.h"
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

} // namespace

EqEditor::EqEditor(PluginProcessor& owner)
    : AudioProcessorEditor(owner),
      processor_(owner),
      curve_(owner),
      meter_(owner),
      panel_(owner),
      presets_(owner)
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

    expand_.onClick = [this] { setExpanded(!expanded_); };
    addAndMakeVisible(expand_);
    curve_.onExpandToggled = [this] { setExpanded(!expanded_); };
    updateExpandButton();

    compare_.setTooltip("Compare with other esceQ instances: overlay their spectrum and show where it collides with this one");
    compare_.onClick = [this] { showCompareMenu(); };
    addAndMakeVisible(compare_);
    updateCompareButton();

    options_.onClick = [this] { showOptionsMenu(); };
    addAndMakeVisible(options_);
    updateOptionsTooltip();

    phase_.attach(owner.parameter(core::kPhaseMode), core::parameters()[core::kPhaseMode]);
    addAndMakeVisible(phase_);
    updatePhaseTooltip();

    for (int range : {6, 12, 18, 30})
        range_.addItem(juce::String::fromUTF8("\xC2\xB1") + juce::String(range) + " dB", range);
    range_.onChange = [this] {
        curve_.setRangeDb(range_.getSelectedId());
        storeUiSettings();
    };
    addAndMakeVisible(range_);

    undo_.onClick = [this] { processor_.history().undo(); };
    redo_.onClick = [this] { processor_.history().redo(); };
    undo_.setTooltip("Undo (Ctrl+Z)");
    redo_.setTooltip("Redo (Ctrl+Shift+Z)");
    addAndMakeVisible(presets_);
    addAndMakeVisible(undo_);
    addAndMakeVisible(redo_);

    output_.attach(owner.parameter(core::kOutputGain), core::parameters()[core::kOutputGain]);
    addAndMakeVisible(output_);

    autoGain_.attach(owner.parameter(core::kAutoGain));
    addAndMakeVisible(autoGain_);
    updateAutoGain();

    bypass_.attach(owner.parameter(core::kBypass));
    bypass_.setAccent(colour::warning);
    addAndMakeVisible(bypass_);

    curve_.onSelectionChanged = [this](int band) { panel_.setBand(band); };
    addAndMakeVisible(curve_);
    keyboard_.onHoveredNoteChanged = [this](std::optional<int> midi) { curve_.setMarkedNote(midi); };
    addChildComponent(keyboard_);
    addAndMakeVisible(meter_);
    addAndMakeVisible(panel_);

    // El panel Match va encima de la curva (es una tarjeta sobre ella).
    matchPanel_.onClose = [this] { closeMatch(); };
    matchPanel_.onReference = [this](const core::MatchReference& reference) {
        if (match_ != nullptr)
            match_->selectReference(reference);
        updateMatch(false);
    };
    matchPanel_.onLearn = [this] {
        if (match_ == nullptr)
            return;
        if (processor_.matchState().learning)
            match_->stop();
        else
            match_->learn(matchContext());
        updateMatch(false);
    };
    matchPanel_.onSettings = [this](const core::MatchSettings& settings) {
        if (match_ != nullptr)
            match_->setSettings(settings);
    };
    matchPanel_.onApply = [this] { applyMatch(); };
    addChildComponent(matchPanel_);

    // Orden de tabulación: barra superior, curva y panel.
    int order = 1;
    for (juce::Component* c :
         std::initializer_list<juce::Component*>{&presets_, &undo_, &redo_, &compare_, &options_, &phase_, &analyzer_,
                                                 &expand_, &range_, &output_, &autoGain_, &bypass_, &curve_,
                                                 &matchPanel_, &panel_})
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
    setNoteScale(ui.pianoKeyboard, ui.a4Hz);
    // La congelación no se guarda: se conserva la de la vista.
    curve_.setAnalyzerView({ui.analyzerFftSize, ui.analyzerFallMs, ui.analyzerTiltDbPerOctave, ui.analyzerPeakHold,
                            curve_.analyzerView().frozen});

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
    ui.pianoKeyboard = showKeyboard_;
    ui.a4Hz = a4Hz_;
    const auto& analyzer = curve_.analyzerView();
    ui.analyzerFftSize = analyzer.fftSize;
    ui.analyzerFallMs = analyzer.fallMs;
    ui.analyzerTiltDbPerOctave = analyzer.tiltDbPerOctave;
    ui.analyzerPeakHold = analyzer.peakHold;
    processor_.setUiSettings(ui);
}

bool EqEditor::keyPressed(const juce::KeyPress& key)
{
    // Esc vuelve de la vista ampliada tenga el foco quien lo tenga (la curva lo atiende antes).
    if (expanded_ && key.isKeyCode(juce::KeyPress::escapeKey))
    {
        setExpanded(false);
        return true;
    }

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
    // Cerrar la ventana es Stop; lo aprendido se queda en el procesador.
    curve_.setMatch(nullptr);
    match_.reset();
    // Los controles cierran sus gestos al destruirse; esto es la red de
    // seguridad para que el historial nunca se quede esperando uno.
    processor_.history().abandonOpenGestures();
    // El diálogo de la afinación usa el aspecto del editor, que está a punto de desaparecer.
    if (tuningDialog_ != nullptr)
        delete tuningDialog_.getComponent();
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
    layout_ = editorLayout(static_cast<float>(getWidth()), static_cast<float>(getHeight()), showKeyboard_, expanded_);
    topBar_ = topBarLayout(layout_.topBar);

    presets_.setBounds(toInt(topBar_.presets));
    undo_.setBounds(toInt(topBar_.undo));
    redo_.setBounds(toInt(topBar_.redo).withTrimmedLeft(-1)); // comparten el borde central
    compare_.setBounds(toInt(topBar_.compare));
    options_.setBounds(toInt(topBar_.options));
    phase_.setBounds(toInt(topBar_.phase));
    analyzer_.setBounds(toInt(topBar_.analyzer));
    expand_.setBounds(toInt(topBar_.expand));
    range_.setBounds(toInt(topBar_.range));
    // La barra y su valor son un solo control; a su izquierda va la etiqueta "OUT".
    output_.setBounds(toInt(topBar_.output).withTrimmedLeft(26).getUnion(toInt(topBar_.outputValue)));
    autoGain_.setBounds(toInt(topBar_.autoGain));
    bypass_.setBounds(toInt(topBar_.bypass));

    curve_.setBounds(toInt(layout_.curve));
    keyboard_.setBounds(toInt(layout_.keyboard));
    meter_.setBounds(toInt(layout_.meter));
    panel_.setBounds(toInt(layout_.panel));
    matchPanel_.setPanelLayout(matchPanelLayout(layout_.curve));

    storeUiSettings();
}

void EqEditor::timerCallback()
{
    // El host ha cargado un estado con la ventana abierta: se aplican sus preferencias.
    if (processor_.stateRevision() != stateRevision_)
        applyUiSettings();

    curve_.refresh(); // vacía la cola "pre": con el panel Match abierto, también para aprender
    updateMatch(true);
    meter_.refresh();
    panel_.refresh();
    presets_.refresh();
    updateCompareButton(); // la curva quita de la selección las instancias que se van
    updatePhaseTooltip();
    updateOptionsTooltip(); // el algoritmo cambia también al deshacer o al cargar un proyecto
    updateAutoGain();
    undo_.setEnabled(processor_.history().canUndo());
    redo_.setEnabled(processor_.history().canRedo());
}

void EqEditor::updatePhaseTooltip()
{
    // La latencia del modo que suena: cambia un momento después de elegir otro.
    const int latency = processor_.engineLatencySamples();
    if (latency == shownLatency_)
        return;
    shownLatency_ = latency;
    phase_.setTooltip(toJuce(phaseModeTooltip(latency, processor_.currentSampleRate())));
}

void EqEditor::updateOptionsTooltip()
{
    const auto algorithm = processor_.filterAlgorithm();
    if (algorithm == shownAlgorithm_)
        return;
    shownAlgorithm_ = algorithm;
    options_.setTooltip(toJuce(optionsTooltip(algorithm)));
}

void EqEditor::updateAutoGain()
{
    const AutoGainInputs inputs{processor_.currentValues(), processor_.filterAlgorithm(),
                                processor_.currentSampleRate(), processor_.outputChannelCount()};
    if (inputs == autoGainInputs_)
        return;
    autoGainInputs_ = inputs;
    // La misma función y los mismos ajustes que el procesador: el valor es el
    // que suena, y se ve aunque el host no esté procesando audio.
    const bool enabled = inputs.values[core::kAutoGain] >= 0.5f;
    const double db = enabled ? dsp::autoGainDb(core::allBandSettings(inputs.values, inputs.algorithm),
                                                inputs.sampleRate, inputs.channels)
                              : 0.0;
    autoGain_.setButtonText(toJuce(autoGainLabel(enabled, db)));
    autoGain_.setTooltip(toJuce(autoGainTooltip(enabled, db)));
}

void EqEditor::showOptionsMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    menu.addSectionHeader(kHfAccuracyHeading);
    for (const auto& item : filterAlgorithmItems(processor_.filterAlgorithm()))
        menu.addItem(item.menuId, toJuce(item.label), true, item.ticked);
    menu.addSectionHeader(kWindowSizeHeading);
    for (const auto& item : windowSizeItems(matchingPreset({getWidth(), getHeight()})))
        menu.addItem(item.menuId, toJuce(item.label), true, item.ticked);
    menu.addSectionHeader(kNotesHeading);
    const auto keyboard = pianoKeyboardItem(showKeyboard_);
    menu.addItem(keyboard.menuId, toJuce(keyboard.label), true, keyboard.ticked);
    juce::PopupMenu tuning;
    for (const auto& item : tuningItems(a4Hz_))
        tuning.addItem(item.menuId, toJuce(item.label), true, item.ticked);
    menu.addSubMenu(toJuce(tuningMenuTitle(a4Hz_)), tuning);
    menu.addSectionHeader(kAnalyzerHeading);
    const auto analyzer = curve_.analyzerView();
    for (const auto& submenu : analyzerSubmenus(analyzer))
    {
        juce::PopupMenu sub;
        for (const auto& item : submenu.items)
            sub.addItem(item.menuId, toJuce(item.label), true, item.ticked);
        menu.addSubMenu(toJuce(submenu.title), sub);
    }
    for (const auto& item : {peakHoldItem(analyzer.peakHold), freezeItem(analyzer.frozen)})
        menu.addItem(item.menuId, toJuce(item.label), true, item.ticked);

    juce::Component::SafePointer<EqEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&options_), [safe](int result) {
        if (safe == nullptr)
            return;
        // Una acción que se puede deshacer, como cualquier cambio de la interfaz.
        if (const auto algorithm = filterAlgorithmForMenuId(result))
            safe->processor_.changeFilterAlgorithm(*algorithm);
        if (const auto preset = windowSizeForMenuId(result))
        {
            const auto size = windowSize(*preset);
            safe->setSize(size.width, size.height);
        }
        // Preferencias de la interfaz: no son acciones que se deshagan (ADR 0009).
        if (isPianoKeyboardMenuId(result))
            safe->setNoteScale(!safe->showKeyboard_, safe->a4Hz_);
        if (const auto a4 = tuningForMenuId(result))
            safe->setNoteScale(safe->showKeyboard_, *a4);
        if (isCustomTuningMenuId(result))
            safe->showTuningDialog();
        if (const auto view = analyzerViewForMenuId(safe->curve_.analyzerView(), result))
        {
            safe->curve_.setAnalyzerView(*view);
            safe->storeUiSettings();
        }
        safe->updateOptionsTooltip();
    });
}

void EqEditor::setExpanded(bool expanded)
{
    if (expanded == expanded_)
        return;
    expanded_ = expanded;
    // El medidor y el panel se ocultan; la curva se queda con su sitio.
    meter_.setVisible(!expanded);
    panel_.setVisible(!expanded);
    curve_.setExpanded(expanded);
    updateExpandButton();
    resized();
}

void EqEditor::updateExpandButton()
{
    expand_.setOn(expanded_);
    expand_.setTooltip(expanded_ ? "Back to the normal view (F or Esc)" : "Expand the curve and analyzer (F)");
}

void EqEditor::setNoteScale(bool keyboard, double a4Hz)
{
    const bool resize = keyboard != showKeyboard_;
    showKeyboard_ = keyboard;
    a4Hz_ = a4Hz;
    if (!keyboard)
        keyboard_.setHoveredNote(std::nullopt); // y la curva deja de marcarla
    keyboard_.setVisible(keyboard);
    keyboard_.setA4(a4Hz);
    matchPanel_.setA4(a4Hz);
    curve_.setNoteScale(keyboard, a4Hz);
    panel_.setNoteScale(keyboard, a4Hz);
    if (resize)
        resized(); // también guarda las preferencias
    else
        storeUiSettings();
}

void EqEditor::showTuningDialog()
{
    // Como los diálogos de los presets: una ventana aparte con el aspecto del editor.
    if (tuningDialog_ != nullptr)
        delete tuningDialog_.getComponent();
    auto* window =
        new juce::AlertWindow("Tuning", "Frequency of A4, from 400 to 480 Hz:", juce::MessageBoxIconType::NoIcon, this);
    window->setLookAndFeel(&getLookAndFeel());
    tuningDialog_ = window;
    window->addTextEditor("a4", toJuce(tuningText(a4Hz_)));
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<EqEditor> safe(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window](int result) {
                                if (safe == nullptr || result != 1)
                                    return;
                                // Lo que no se entiende, o se sale del rango, no cambia la afinación.
                                if (const auto a4 = parseTuning(window->getTextEditorContents("a4").toStdString()))
                                    safe->setNoteScale(safe->showKeyboard_, *a4);
                            }),
                            true);
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
    constexpr int kMatchId = 4;
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
    menu.addItem(kMatchId, toJuce(std::string(kMatchMenuText)));
    menu.addSeparator();
    menu.addItem(kAllId, "Show all", std::any_of(items.begin(), items.end(), [](const auto& i) { return !i.ticked; }));
    menu.addItem(kNoneId, "Hide all", !selection.shown.empty());
    menu.addSeparator();
    menu.addItem(kCollisionsId, "Highlight collisions", true, selection.showCollisions);

    juce::Component::SafePointer<EqEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&compare_), [safe, items](int result) {
        if (safe == nullptr || result <= 0)
            return;
        if (result == kMatchId)
        {
            safe->openMatch();
            return;
        }
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

// ---------------------------------------------------------------------------
// Igualar a una referencia

void EqEditor::openMatch()
{
    if (match_ == nullptr)
    {
        match_ = std::make_unique<MatchController>(processor_.matchState(), processor_.matchWorker(),
                                                   processor_.spectrumSession(), processor_.matchSidechain());
        curve_.setMatch(match_.get());
        matchPanel_.setVisible(true);
        updateMatch(false);
    }
    // Con el foco en el panel, Esc lo cierra.
    matchPanel_.grabKeyboardFocus();
}

void EqEditor::closeMatch()
{
    if (match_ == nullptr)
        return;
    const bool hadFocus = matchPanel_.hasKeyboardFocus(true);
    matchPanel_.setVisible(false);
    curve_.setMatch(nullptr);
    match_.reset(); // Stop: lo aprendido se queda en el procesador
    if (hadFocus)
        curve_.grabKeyboardFocus();
}

MatchContext EqEditor::matchContext()
{
    matchValues_ = processor_.currentValues();
    MatchContext context;
    context.instances = matchInstances_;
    context.self = processor_.instanceId();
    context.sidechainConnected = processor_.sidechainConnected();
    context.sampleRate = processor_.currentSampleRate();
    context.numChannels = processor_.outputChannelCount();
    context.algorithm = processor_.filterAlgorithm();
    context.values = &matchValues_;
    return context;
}

void EqEditor::updateMatch(bool tick)
{
    if (match_ == nullptr)
        return;

    processor_.spectrumSession().snapshot(matchInstances_);
    const auto context = matchContext();
    if (tick)
        tickMatch(context);

    // Sin nada elegido ni aprendido, la referencia por defecto: la primera instancia con señal.
    auto& state = processor_.matchState();
    if (state.reference.kind == core::MatchReference::Kind::None && !state.learning && state.learner.empty())
    {
        const auto options = matchReferenceOptions(context.instances, context.self, context.sidechainConnected);
        const auto fallback = defaultMatchReference(options);
        if (canLearnFrom(fallback, options))
            match_->selectReference(fallback);
    }

    const auto status = match_->status(context);
    const auto proposal = match_->proposal();
    const int proposed = proposal != nullptr ? static_cast<int>(proposal->result.bands.size()) : 0;
    matchPanel_.update(status, state.settings, freeBandCount(matchValues_), proposed);

    // La referencia se dibuja con el color de su pista; el sidechain, con el del texto secundario.
    juce::Colour referenceColour = theme::colour::textMuted;
    if (state.reference.kind == core::MatchReference::Kind::Instance)
        for (const auto& instance : matchInstances_)
            if (instance.id == state.reference.instance)
                referenceColour = theme::instance(instance.number, instance.trackColour);
    curve_.refreshMatch(referenceColour);
}

void EqEditor::tickMatch(const MatchContext& context)
{
    // La cola del sidechain solo tiene audio mientras se aprende de él; su único lector es esto.
    sidechainBuffer_.resize(4096);
    auto& tap = processor_.matchSidechain();
    for (int n; (n = tap.read(sidechainBuffer_.data(), static_cast<int>(sidechainBuffer_.size()))) > 0;)
        match_->pushSidechain(sidechainBuffer_.data(), n);
    match_->tick(1.0 / kRefreshHz, context);
}

void EqEditor::applyMatch()
{
    if (match_ == nullptr)
        return;
    const auto context = matchContext();
    const auto apply = match_->applyChanges(context);
    if (!apply)
        return;
    // Una sola acción de deshacer para todas las bandas creadas.
    processor_.applyAction(withChanges(matchValues_, apply->changes));
    curve_.refresh();
    if (apply->firstBand)
        curve_.setSelectedBand(*apply->firstBand);
    updateMatch(false);
}

} // namespace esceq::ui
