// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PresetBar.h"

#include "PluginProcessor.h"
#include "Theme.h"

#include "esceq/core/presets.h"

namespace esceq::ui {

using namespace theme;

/// Botón central: el nombre del preset, con una marca si se ha modificado.
class PresetBar::NameButton final : public juce::Button
{
public:
    NameButton() : juce::Button("Presets")
    {
        setTitle("Presets");
        setWantsKeyboardFocus(true);
    }

    void setPreset(const juce::String& name, bool modified)
    {
        if (name == name_ && modified == modified_)
            return;
        name_ = name;
        modified_ = modified;
        setDescription(name.isEmpty() ? juce::String("No preset") : name + (modified ? " (modified)" : ""));
        repaint();
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override
    {
        draw::ControlState state;
        state.hover = highlighted;
        state.pressed = down;
        state.focused = keyboardFocus_ && hasKeyboardFocus(false);
        const auto bounds = getLocalBounds().toFloat();

        g.setColour(down ? colour::controlDown : highlighted ? colour::controlHover : colour::control);
        g.fillRect(bounds);
        g.setColour(colour::lineStrong);
        g.fillRect(bounds.withHeight(1.0f));
        g.fillRect(bounds.withTop(bounds.getBottom() - 1.0f));

        auto content = bounds.reduced(space::x2, 0.0f);
        // Marca de "modificado": un punto del color de acento.
        if (modified_)
        {
            g.setColour(colour::accent);
            g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({content.getRight() - 3.0f, content.getCentreY()}));
            content.removeFromRight(10.0f);
        }
        g.setFont(font(type::body, name_.isEmpty() ? Weight::Regular : Weight::Medium));
        g.setColour(name_.isEmpty() ? colour::textMuted : colour::text);
        g.drawText(name_.isEmpty() ? juce::String("Presets") : name_, content, juce::Justification::centred, true);

        if (state.focused)
            draw::focusRing(g, bounds, 0.0f);
    }

    void focusGained(FocusChangeType cause) override
    {
        keyboardFocus_ = cause == focusChangedByTabKey;
        repaint();
    }

    void focusLost(FocusChangeType) override
    {
        keyboardFocus_ = false;
        repaint();
    }

private:
    juce::String name_;
    bool modified_ = false;
    bool keyboardFocus_ = false;
};

PresetBar::PresetBar(PluginProcessor& processor)
    : processor_(processor),
      name_(std::make_unique<NameButton>())
{
    setTitle("Presets");
    setFocusContainerType(FocusContainerType::focusContainer);

    previous_.onClick = [this] {
        processor_.presets().step(-1);
        refresh();
    };
    next_.onClick = [this] {
        processor_.presets().step(1);
        refresh();
    };
    name_->onClick = [this] { showMenu(); };

    addAndMakeVisible(previous_);
    addAndMakeVisible(*name_);
    addAndMakeVisible(next_);
    refresh();
}

PresetBar::~PresetBar() = default;

void PresetBar::refresh()
{
    auto& presets = processor_.presets();
    const auto current = presets.current();
    name_->setPreset(current ? current->name : juce::String(), presets.isModified());
}

void PresetBar::resized()
{
    auto area = getLocalBounds();
    previous_.setBounds(area.removeFromLeft(24));
    next_.setBounds(area.removeFromRight(24));
    name_->setBounds(area);
}

void PresetBar::showMenu()
{
    constexpr int kSaveId = 1;
    constexpr int kDeleteId = 2;
    constexpr int kDefaultId = 3;
    constexpr int kFolderId = 4;
    constexpr int kFirstPresetId = 100;

    auto& presets = processor_.presets();
    const auto entries = presets.presets();
    const auto current = presets.current();

    juce::PopupMenu menu;
    bool inUser = false;
    menu.addSectionHeader("Factory");
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        if (!entries[i].factory && !inUser)
        {
            inUser = true;
            menu.addSectionHeader("User");
        }
        menu.addItem(kFirstPresetId + static_cast<int>(i), entries[i].name, true, current && *current == entries[i]);
    }
    menu.addSeparator();
    menu.addItem(kSaveId, "Save as...");
    const bool canDelete = current && !current->factory && presets.userPresetExists(current->name);
    menu.addItem(kDeleteId, canDelete ? "Delete \"" + current->name + "\"" : juce::String("Delete"), canDelete);
    menu.addItem(kDefaultId, "Reset to default");
    menu.addItem(kFolderId, "Open presets folder");

    juce::Component::SafePointer<PresetBar> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(name_.get()).withMinimumWidth(name_->getWidth()),
                       [safe, entries](int result) {
                           if (safe == nullptr || result == 0)
                               return;
                           auto& manager = safe->processor_.presets();
                           if (result == kSaveId)
                               safe->askForName();
                           else if (result == kDeleteId)
                           {
                               if (const auto now = manager.current())
                                   manager.deleteUser(now->name);
                           }
                           else if (result == kDefaultId)
                               manager.loadDefault();
                           else if (result == kFolderId)
                           {
                               const auto folder = PresetManager::userDirectory();
                               folder.createDirectory();
                               folder.revealToUser();
                           }
                           else if (result >= kFirstPresetId)
                               manager.load(entries[static_cast<std::size_t>(result - kFirstPresetId)]);
                           safe->refresh();
                       });
}

void PresetBar::askForName()
{
    const auto current = processor_.presets().current();
    auto* window = new juce::AlertWindow("Save preset", "Name of the preset:", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", current && !current->factory ? current->name : juce::String());
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PresetBar> safe(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window](int result) {
                                if (safe == nullptr || result != 1)
                                    return;
                                safe->save(window->getTextEditorContents("name"));
                            }),
                            true);
}

void PresetBar::save(const juce::String& name)
{
    auto& presets = processor_.presets();
    const auto clean = juce::String::fromUTF8(core::sanitizePresetName(name.toStdString()).c_str());

    if (clean.isEmpty())
    {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle("Save preset")
                                         .withMessage("That name cannot be used. Please choose another one.")
                                         .withButton("OK", 1)
                                         .withAssociatedComponent(this),
                                     std::function<void(int)>{});
        return;
    }

    juce::Component::SafePointer<PresetBar> safe(this);
    auto write = [safe, clean] {
        if (safe == nullptr)
            return;
        if (!safe->processor_.presets().saveUser(clean))
            juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                                             .withTitle("Save preset")
                                             .withMessage("The preset could not be written to\n" +
                                                          PresetManager::userDirectory().getFullPathName())
                                             .withButton("OK", 1)
                                             .withAssociatedComponent(safe.getComponent()),
                                         std::function<void(int)>{});
        safe->refresh();
    };

    // No se pisa un preset existente sin preguntar.
    const auto current = presets.current();
    const bool isCurrent = current && !current->factory && current->name == clean;
    if (presets.userPresetExists(clean) && !isCurrent)
    {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                         .withTitle("Save preset")
                                         .withMessage("A preset named \"" + clean + "\" already exists. Replace it?")
                                         .withButton("Replace", 1)
                                         .withButton("Cancel", 0)
                                         .withAssociatedComponent(this),
                                     [write](int result) {
                                         if (result == 1)
                                             write();
                                     });
        return;
    }
    write();
}

} // namespace esceq::ui
