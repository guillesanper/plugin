// SPDX-License-Identifier: AGPL-3.0-or-later
#include "OutputDrawer.h"

#include "Draw.h"
#include "JuceConvert.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include "esceq/core/band_model.h"
#include "esceq/core/parameters.h"
#include "esceq/ui/macros.h"
#include "esceq/ui/output_stage.h"

#include <cmath>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace esceq::ui {

namespace {

using namespace theme;

juce::Rectangle<int> toInt(const Rect& r)
{
    return toJuce(r).toNearestInt();
}

std::vector<Segmented::Option> modeOptions()
{
    // El orden es el de core::kSaturationModeChoices.
    static_assert(core::kSaturationModeChoices.size() == 3);
    return {{"Off", "Saturation off"}, {"Soft", "Soft saturation"}, {"Warm", "Warm saturation"}};
}

} // namespace

/// La cabecera (abierto) o la pestaña (plegado): un botón con el estado del cajón.
class OutputDrawer::Toggle final : public juce::Button
{
public:
    Toggle() : juce::Button("Output stage")
    {
        setTitle("Output stage");
        setWantsKeyboardFocus(true);
        // Para el lector de pantalla: abierto o plegado. Lo cambia set(), no el clic.
        setToggleable(true);
        setToggleState(open_, juce::dontSendNotification);
    }

    void set(bool open, bool active)
    {
        if (open == open_ && active == active_)
            return;
        open_ = open;
        active_ = active;
        setToggleState(open, juce::dontSendNotification);
        repaint();
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        draw::ControlState state;
        state.hover = highlighted;
        state.pressed = down;
        state.focused = keyboardFocus_ && hasKeyboardFocus(false);
        // Abierto, solo la flecha (las pestañas dicen el título); plegado, el título en vertical.
        draw::drawerToggle(g, getLocalBounds().toFloat(), open_ ? juce::String() : toJuce(kOutputStageTitle), open_,
                           active_, state);
    }

    void focusGained(FocusChangeType cause) override
    {
        keyboardFocus_ = cause != focusChangedByMouseClick;
        repaint();
    }

    void focusLost(FocusChangeType) override
    {
        keyboardFocus_ = false;
        repaint();
    }

private:
    bool open_ = true;
    bool active_ = false;
    bool keyboardFocus_ = false;
};

/// Una pestaña de la cabecera: Output o Macros. Para el lector de pantalla,
/// un botón que se queda elegido, del grupo de las dos.
class OutputDrawer::Tab final : public juce::Button
{
public:
    explicit Tab(std::string_view title) : juce::Button(toJuce(title)), title_(toJuce(title))
    {
        setTitle(title_);
        setWantsKeyboardFocus(true);
        setToggleable(true);
        setRadioGroupId(1, juce::dontSendNotification);
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        draw::ControlState state;
        state.hover = highlighted;
        state.pressed = down;
        state.focused = keyboardFocus_ && hasKeyboardFocus(false);
        draw::tab(g, getLocalBounds().toFloat(), title_, getToggleState(), state);
    }

    void focusGained(FocusChangeType cause) override
    {
        keyboardFocus_ = cause != focusChangedByMouseClick;
        repaint();
    }

    void focusLost(FocusChangeType) override
    {
        keyboardFocus_ = false;
        repaint();
    }

private:
    juce::String title_;
    bool keyboardFocus_ = false;
};

OutputDrawer::OutputDrawer(PluginProcessor& processor)
    : processor_(processor),
      toggle_(std::make_unique<Toggle>()),
      outputTab_(std::make_unique<Tab>(kOutputStageTitle)),
      macrosTab_(std::make_unique<Tab>(kMacrosTitle)),
      mode_(modeOptions(), "Saturation")
{
    const auto& specs = core::parameters();
    mode_.attach(processor.parameter(core::kSaturationMode));
    drive_.attach(processor.parameter(core::kSaturationDrive), specs[core::kSaturationDrive]);
    mix_.attach(processor.parameter(core::kSaturationMix), specs[core::kSaturationMix]);
    gain_.attach(processor.parameter(core::kOutputGain), specs[core::kOutputGain]);
    autoGain_.attach(processor.parameter(core::kAutoGain));
    for (int m = 0; m < core::kNumMacros; ++m)
    {
        const auto index = core::macroParameter(m);
        auto& knob = macros_[static_cast<std::size_t>(m)];
        knob = std::make_unique<ParamControl>(ParamControl::Style::Knob, toJuce(macroName(m)));
        knob->attach(processor.parameter(index), specs[index]);
    }

    toggle_->onClick = [this] {
        if (onToggle)
            onToggle();
    };
    addAndMakeVisible(*toggle_);
    auto choose = [this](bool macros) {
        if (macros == macrosPage_)
            return;
        setMacrosPage(macros);
        if (onPageChanged)
            onPageChanged(macros);
    };
    outputTab_->onClick = [choose] { choose(false); };
    macrosTab_->onClick = [choose] { choose(true); };
    outputTab_->setTooltip("Saturation, output gain and Auto gain");
    macrosTab_->setTooltip("Eight macros: each one moves up to eight settings at once. Right-click a knob or a band "
                           "node and choose a macro to link it; link a host controller to a macro to move them all");

    // Orden de tabulación: la cabecera (pestañas y flecha), luego la pestaña
    // que se ve, en el orden de la señal o de las macros.
    std::vector<juce::Component*> order{outputTab_.get(), macrosTab_.get(), toggle_.get(), &mode_, &drive_, &mix_,
                                        &gain_, &autoGain_};
    for (auto& knob : macros_)
        order.push_back(knob.get());
    int index = 1;
    for (auto* c : order)
    {
        c->setExplicitFocusOrder(index++);
        if (c != toggle_.get())
            addChildComponent(*c);
    }

    setTitle("Output stage");
    setFocusContainerType(FocusContainerType::focusContainer);
    updateVisibility();
    refresh({});
}

OutputDrawer::~OutputDrawer() = default;

void OutputDrawer::updateVisibility()
{
    const bool output = open_ && !macrosPage_;
    for (juce::Component* c : std::initializer_list<juce::Component*>{&mode_, &drive_, &mix_, &gain_, &autoGain_})
        c->setVisible(output);
    for (auto& knob : macros_)
        knob->setVisible(open_ && macrosPage_);
    outputTab_->setVisible(open_);
    macrosTab_->setVisible(open_);
    outputTab_->setToggleState(!macrosPage_, juce::dontSendNotification);
    macrosTab_->setToggleState(macrosPage_, juce::dontSendNotification);
}

void OutputDrawer::setOpen(bool open)
{
    if (open == open_)
        return;
    open_ = open;
    updateVisibility();
    refresh(macroAssignments_);
    resized();
    repaint();
}

void OutputDrawer::setMacrosPage(bool macros)
{
    if (macros == macrosPage_)
        return;
    macrosPage_ = macros;
    updateVisibility();
    repaint();
}

void OutputDrawer::refresh(const core::MacroAssignments& macros)
{
    macroAssignments_ = macros;
    const auto values = processor_.currentValues();
    const auto mode = core::saturationModeFromChoice(values[core::kSaturationMode]);
    const bool active = saturationAmountActive(mode);
    // Con Off, drive y mezcla no influyen: se atenúan, pero se pueden editar.
    drive_.setDimmed(!active);
    mix_.setDimmed(!active);
    drive_.setTooltip(toJuce(saturationDriveTooltip(mode)));
    mix_.setTooltip(toJuce(saturationMixTooltip(mode)));
    // La latencia, la del modo de fase elegido a la frecuencia de muestreo actual.
    mode_.setTooltip(toJuce(saturationTooltip(core::phaseModeFromChoice(values[core::kPhaseMode]),
                                              processor_.currentSampleRate())));
    for (int m = 0; m < core::kNumMacros; ++m)
    {
        // Sin destinos, una macro no influye: se atenúa, pero se puede mover (y automatizar).
        auto& knob = *macros_[static_cast<std::size_t>(m)];
        knob.setDimmed(!macroHasTargets(macros, m));
        knob.setTooltip(toJuce(macroTooltip(macros, m)));
    }

    // Plegado, el punto y la ayuda dicen también si las macros están moviendo algo.
    toggle_->set(open_, outputStageActive(values, macros));
    toggle_->setTooltip(toJuce(outputStageTooltip(values, open_, macros)));
}

void OutputDrawer::setAutoGainText(const juce::String& label, const juce::String& tooltip)
{
    autoGain_.setButtonText(label);
    autoGain_.setTooltip(tooltip);
}

void OutputDrawer::resized()
{
    const auto bounds = getLocalBounds().toFloat();
    layout_ = outputDrawerLayout({bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight()}, open_);
    toggle_->setBounds(toInt(layout_.toggle));
    outputTab_->setBounds(toInt(layout_.outputTab));
    macrosTab_->setBounds(toInt(layout_.macrosTab));
    for (std::size_t m = 0; m < macros_.size(); ++m)
        macros_[m]->setBounds(toInt(layout_.macros[m]));
    mode_.setBounds(toInt(layout_.saturationMode));
    drive_.setBounds(toInt(layout_.drive));
    mix_.setBounds(toInt(layout_.mix));
    gain_.setBounds(toInt(layout_.gain));
    autoGain_.setBounds(toInt(layout_.autoGain));
}

void OutputDrawer::paint(juce::Graphics& g)
{
    if (!open_)
        return; // la pestaña se pinta sola

    draw::card(g, toJuce(layout_.card));
    if (macrosPage_)
        return;
    draw::caption(g, toJuce(std::string(kSaturationTitle)), toJuce(layout_.saturationTitle),
                  juce::Justification::centredLeft);

    // Una línea entre la saturación y la salida, a medio camino.
    const auto drive = toJuce(layout_.drive);
    const auto gain = toJuce(layout_.gain);
    const auto row = toJuce(layout_.saturationMode);
    g.setColour(colour::line);
    g.fillRect(row.getX(), std::round(0.5f * (drive.getBottom() + gain.getY())), row.getWidth(), stroke::hairline);
}

} // namespace esceq::ui
