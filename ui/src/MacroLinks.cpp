// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MacroLinks.h"

#include "JuceConvert.h"
#include "PluginProcessor.h"

#include "esceq/ui/macros.h"

#include <algorithm>

namespace esceq::ui {

MacroLinks::MacroLinks(PluginProcessor& processor, juce::Component& owner)
    : processor_(processor),
      owner_(owner)
{
}

MacroLinks::~MacroLinks()
{
    // El diálogo usa el aspecto del editor, que está a punto de desaparecer.
    if (depthDialog_ != nullptr)
        delete depthDialog_.getComponent();
}

void MacroLinks::add(ParamControl& control)
{
    controls_.push_back(&control);
    control.onPopupMenu = [this](ParamControl& c) { showLinkMenu(c); };
}

void MacroLinks::addMacro(ParamControl& control, int macro)
{
    if (macro < 0 || macro >= core::kNumMacros)
        return;
    macroControls_[static_cast<std::size_t>(macro)] = &control;
    control.onPopupMenu = [this, macro](ParamControl& c) { showMacroMenu(c, macro); };
}

void MacroLinks::refresh()
{
    macros_ = processor_.macroAssignments();
    sounding_ = processor_.currentValues();
    core::applyMacros(macros_, sounding_);
    updateRings();
}

void MacroLinks::updateRings()
{
    for (auto* control : controls_)
    {
        const auto index = control->parameterIndex();
        if (index && isLinked(macros_, *index))
            control->setModulation(sounding_[*index]);
        else
            control->setModulation(std::nullopt);
    }
}

void MacroLinks::apply(const core::MacroAssignments& macros)
{
    // Una acción de deshacer por elección (si cambia algo).
    processor_.changeMacroAssignments(macros);
    refresh();
}

juce::PopupMenu MacroLinks::depthMenu(int macro, std::size_t parameter, std::optional<float> depth)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&owner_.getLookAndFeel());
    juce::Component::SafePointer<juce::Component> safe(&owner_);
    for (const float choice : kMacroDepthChoices)
    {
        // Igualdad exacta a propósito: marca la profundidad elegida en el menú.
        const bool ticked = depth && juce::exactlyEqual(*depth, choice);
        menu.addItem(toJuce(depthText(choice)), true, ticked, [this, safe, macro, parameter, choice] {
            if (safe == nullptr)
                return;
            if (const auto next = withLink(processor_.macroAssignments(), macro, parameter, choice))
                apply(*next);
        });
    }
    const bool other = depth && std::none_of(kMacroDepthChoices.begin(), kMacroDepthChoices.end(),
                                             [&](float choice) { return juce::exactlyEqual(*depth, choice); });
    menu.addItem(other ? "Other (" + toJuce(depthText(*depth)) + ")..." : juce::String("Other..."), true, other,
                 [this, safe, macro, parameter, depth] {
                     if (safe != nullptr)
                         askDepth(macro, parameter, depth);
                 });
    if (depth)
    {
        menu.addSeparator();
        menu.addItem("Remove link", [this, safe, macro, parameter] {
            if (safe != nullptr)
                apply(withoutLink(processor_.macroAssignments(), macro, parameter));
        });
    }
    return menu;
}

void MacroLinks::addLinkMenu(juce::PopupMenu& menu, std::size_t parameter)
{
    for (const auto& entry : macroLinkEntries(macros_, parameter))
        menu.addSubMenu(toJuce(entry.text), depthMenu(entry.macro, parameter, entry.depth), !entry.full, nullptr,
                        entry.depth.has_value());
}

void MacroLinks::addNodeMenu(juce::PopupMenu& menu, const core::ParameterValues& values, int band)
{
    menu.addSectionHeader(toJuce(std::string(kLinkToMacroTitle)));
    for (const auto index : nodeMacroTargets(values, band))
    {
        const char* label = index == core::bandParameter(band, core::BandParam::Frequency) ? "Frequency"
                            : index == core::bandParameter(band, core::BandParam::Gain)    ? "Gain"
                                                                                             : "Q";
        juce::PopupMenu links;
        links.setLookAndFeel(&owner_.getLookAndFeel());
        addLinkMenu(links, index);
        menu.addSubMenu(toJuce(linkedTargetText(macros_, index, label)), links, true, nullptr,
                        isLinked(macros_, index));
    }
}

void MacroLinks::showLinkMenu(ParamControl& control)
{
    const auto index = control.parameterIndex();
    if (!index || !core::isMacroTarget(*index))
        return; // los interruptores y las listas no ofrecen Link to Macro (y no son ParamControl)

    refresh(); // con las asignaciones de ahora, aunque el temporizador no haya pasado
    juce::PopupMenu menu;
    menu.setLookAndFeel(&owner_.getLookAndFeel());
    menu.addSectionHeader(toJuce(std::string(kLinkToMacroTitle)) + ": " +
                          toJuce(std::string(core::parameters()[*index].name)));
    addLinkMenu(menu, *index);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&control).withMousePosition());
}

void MacroLinks::showMacroMenu(ParamControl& control, int macro)
{
    refresh();
    juce::PopupMenu menu;
    menu.setLookAndFeel(&owner_.getLookAndFeel());
    menu.addSectionHeader(toJuce(macroName(macro)));
    juce::Component::SafePointer<juce::Component> safe(&owner_);
    if (!macroHasTargets(macros_, macro))
    {
        menu.addItem("No links: right-click a knob or a band node and choose a macro", false, false,
                     std::function<void()>{});
    }
    else
    {
        for (const auto& target : macros_.targets(macro))
            if (target.used())
                menu.addSubMenu(toJuce(std::string(core::parameters()[target.parameter].name)) + " (" +
                                    toJuce(depthText(target.depth)) + ")",
                                depthMenu(macro, target.parameter, target.depth));
        menu.addSeparator();
        menu.addItem("Remove all links", [this, safe, macro] {
            if (safe != nullptr)
                apply(clearMacro(processor_.macroAssignments(), macro));
        });
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&control).withMousePosition());
}

void MacroLinks::askDepth(int macro, std::size_t parameter, std::optional<float> depth)
{
    // Como el diálogo de la afinación: una ventana aparte con el aspecto del editor.
    if (depthDialog_ != nullptr)
        delete depthDialog_.getComponent();
    auto* window = new juce::AlertWindow(toJuce(macroName(macro)),
                                         "Depth for " + toJuce(std::string(core::parameters()[parameter].name)) +
                                             ", from -100% to +100% of its knob's travel:",
                                         juce::MessageBoxIconType::NoIcon, &owner_);
    window->setLookAndFeel(&owner_.getLookAndFeel());
    depthDialog_ = window;
    window->addTextEditor("depth", toJuce(depthText(depth.value_or(25.0f))));
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<juce::Component> safe(&owner_);
    auto done = [this, safe, window, macro, parameter](int result) {
        if (safe == nullptr || result != 1)
            return;
        // Lo que no se entiende no cambia nada; lo que se pasa, se limita a ±100 %.
        const auto typed = parseDepth(window->getTextEditorContents("depth").toStdString());
        if (!typed)
            return;
        if (const auto next = withLink(processor_.macroAssignments(), macro, parameter, *typed))
            apply(*next);
    };
    window->enterModalState(true, juce::ModalCallbackFunction::create(done), true);
}

} // namespace esceq::ui
