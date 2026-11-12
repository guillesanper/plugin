// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PresetManager.h"

#include "PluginProcessor.h"

#include "esceq/core/presets.h"
#include "esceq/core/state.h"

#include <algorithm>

namespace esceq {

namespace {

juce::String toJuce(std::string_view text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

juce::String extension()
{
    return toJuce(core::kPresetExtension);
}

} // namespace

PresetManager::PresetManager(PluginProcessor& processor)
    : processor_(processor)
{
}

juce::File PresetManager::userDirectory()
{
    // Para pruebas y para quien quiera sus presets en otro sitio (ver DEV.md).
    const auto custom = juce::SystemStats::getEnvironmentVariable("ESCEQ_PRESETS_DIR", {});
    if (custom.isNotEmpty() && juce::File::isAbsolutePath(custom))
        return juce::File(custom);

    auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    base = base.getChildFile("Application Support");
#endif
    return base.getChildFile("esceQ").getChildFile("Presets");
}

juce::File PresetManager::fileFor(const juce::String& name)
{
    return userDirectory().getChildFile(name + extension());
}

std::vector<PresetManager::Entry> PresetManager::presets() const
{
    std::vector<Entry> entries;
    for (const auto& preset : core::factoryPresets())
        entries.push_back({toJuce(preset.name), true});

    juce::StringArray names;
    for (const auto& file : userDirectory().findChildFiles(juce::File::findFiles, false, "*" + extension()))
        names.add(file.getFileNameWithoutExtension());
    names.sortNatural();
    for (const auto& name : names)
        entries.push_back({name, false});
    return entries;
}

std::optional<core::ParameterValues> PresetManager::read(const Entry& entry) const
{
    if (entry.factory)
        return core::loadFactoryPreset(entry.name.toStdString());

    const auto file = fileFor(entry.name);
    if (!file.existsAsFile())
        return std::nullopt;
    return core::deserializeState(file.loadFileAsString().toStdString());
}

void PresetManager::setCurrent(const juce::String& name)
{
    auto ui = processor_.uiSettings();
    ui.presetName = name.toStdString();
    processor_.setUiSettings(ui);
}

bool PresetManager::load(const Entry& entry)
{
    const auto values = read(entry);
    if (!values)
        return false;

    processor_.applyValues(*values);
    cachedName_ = entry.name;
    cachedValues_ = processor_.currentValues();
    setCurrent(entry.name);
    processor_.history().commitNow();
    return true;
}

void PresetManager::step(int direction)
{
    const auto entries = presets();
    if (entries.empty())
        return;

    const auto now = current();
    const auto it = now ? std::find(entries.begin(), entries.end(), *now) : entries.end();
    const int count = static_cast<int>(entries.size());
    int index = direction > 0 ? 0 : count - 1;
    if (it != entries.end())
        index = (static_cast<int>(it - entries.begin()) + (direction > 0 ? 1 : -1) + count) % count;
    load(entries[static_cast<std::size_t>(index)]);
}

void PresetManager::loadDefault()
{
    processor_.applyValues(core::defaultValues());
    cachedName_.clear();
    cachedValues_.reset();
    setCurrent({});
    processor_.history().commitNow();
}

bool PresetManager::saveUser(const juce::String& name)
{
    const auto clean = toJuce(core::sanitizePresetName(name.toStdString()));
    if (clean.isEmpty())
        return false;

    const auto directory = userDirectory();
    if (!directory.isDirectory() && directory.createDirectory().failed())
        return false;

    const auto values = processor_.currentValues();
    if (!fileFor(clean).replaceWithText(toJuce(core::serializePreset(values)), false, false, "\n"))
        return false;

    cachedName_ = clean;
    cachedValues_ = values;
    setCurrent(clean);
    return true;
}

bool PresetManager::userPresetExists(const juce::String& name) const
{
    const auto clean = toJuce(core::sanitizePresetName(name.toStdString()));
    return clean.isNotEmpty() && fileFor(clean).existsAsFile();
}

bool PresetManager::deleteUser(const juce::String& name)
{
    const auto file = fileFor(name);
    if (!file.existsAsFile() || !file.deleteFile())
        return false;
    if (processor_.uiSettings().presetName == name.toStdString())
    {
        cachedName_.clear();
        cachedValues_.reset();
        setCurrent({});
    }
    return true;
}

std::optional<PresetManager::Entry> PresetManager::current() const
{
    const auto name = toJuce(processor_.uiSettings().presetName);
    if (name.isEmpty())
        return std::nullopt;

    // Un preset de usuario con el nombre de uno de fábrica tiene preferencia:
    // es el que el usuario guardó a propósito.
    if (fileFor(name).existsAsFile())
        return Entry{name, false};
    if (core::loadFactoryPreset(name.toStdString()))
        return Entry{name, true};
    return Entry{name, false}; // el archivo ya no está: se conserva el nombre
}

bool PresetManager::isModified() const
{
    const auto now = current();
    if (!now)
        return false;

    if (cachedName_ != now->name || !cachedValues_)
    {
        cachedName_ = now->name;
        cachedValues_ = read(*now);
    }
    if (!cachedValues_)
        return false; // no se puede comparar: el archivo ya no existe

    // El bypass y la ganancia de salida no forman parte del "sonido" del
    // preset a estos efectos: tocarlos no lo marca como modificado.
    auto a = processor_.currentValues();
    auto b = *cachedValues_;
    for (const auto index : {core::kBypass, core::kOutputGain})
        a[index] = b[index];
    return a != b;
}

} // namespace esceq
