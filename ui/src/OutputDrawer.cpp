// SPDX-License-Identifier: AGPL-3.0-or-later
#include "OutputDrawer.h"

#include "Draw.h"
#include "JuceConvert.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include "esceq/core/band_model.h"
#include "esceq/core/parameters.h"
#include "esceq/ui/output_stage.h"

#include <cmath>
#include <initializer_list>

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
        draw::drawerToggle(g, getLocalBounds().toFloat(), toJuce(std::string(kOutputStageTitle)), open_, active_,
                           state);
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

OutputDrawer::OutputDrawer(PluginProcessor& processor)
    : processor_(processor),
      toggle_(std::make_unique<Toggle>()),
      mode_(modeOptions(), "Saturation")
{
    const auto& specs = core::parameters();
    mode_.attach(processor.parameter(core::kSaturationMode));
    drive_.attach(processor.parameter(core::kSaturationDrive), specs[core::kSaturationDrive]);
    mix_.attach(processor.parameter(core::kSaturationMix), specs[core::kSaturationMix]);
    gain_.attach(processor.parameter(core::kOutputGain), specs[core::kOutputGain]);
    autoGain_.attach(processor.parameter(core::kAutoGain));

    toggle_->onClick = [this] {
        if (onToggle)
            onToggle();
    };
    addAndMakeVisible(*toggle_);

    // Orden de tabulación: la cabecera y luego en el orden de la señal.
    int order = 1;
    for (juce::Component* c :
         std::initializer_list<juce::Component*>{toggle_.get(), &mode_, &drive_, &mix_, &gain_, &autoGain_})
    {
        c->setExplicitFocusOrder(order++);
        if (c != toggle_.get())
            addAndMakeVisible(*c);
    }

    setTitle("Output stage");
    setFocusContainerType(FocusContainerType::focusContainer);
    refresh();
}

OutputDrawer::~OutputDrawer() = default;

void OutputDrawer::setOpen(bool open)
{
    if (open == open_)
        return;
    open_ = open;
    for (juce::Component* c : std::initializer_list<juce::Component*>{&mode_, &drive_, &mix_, &gain_, &autoGain_})
        c->setVisible(open);
    refresh();
    resized();
    repaint();
}

void OutputDrawer::refresh()
{
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
    toggle_->set(open_, outputStageActive(values));
    toggle_->setTooltip(toJuce(outputStageTooltip(values, open_)));
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
