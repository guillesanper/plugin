// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Controls.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace esceq {
class PluginProcessor;
}

namespace esceq::ui {

/// Navegador de presets de la barra superior: anterior, nombre (abre el
/// menú) y siguiente. El menú lista los presets de fábrica y los de usuario,
/// y permite guardar, borrar, volver a los valores por defecto y abrir la
/// carpeta de presets. La gestión está en plugin/src/PresetManager.h.
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar(PluginProcessor& processor);
    ~PresetBar() override;

    /// Actualiza el nombre mostrado. La llama el temporizador del editor.
    void refresh();

    void resized() override;

private:
    class NameButton;

    void showMenu();
    void askForName();
    void save(const juce::String& name);

    PluginProcessor& processor_;
    IconButton previous_{draw::Icon::Previous, "Previous preset", draw::Corners::Left};
    IconButton next_{draw::Icon::Next, "Next preset", draw::Corners::Right};
    std::unique_ptr<NameButton> name_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBar)
};

} // namespace esceq::ui
