// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PresetManager.h"

#include "JuceText.h"
#include "PluginProcessor.h"

#include "esceq/core/presets.h"
#include "esceq/core/state.h"

#include <algorithm>

namespace esceq {

namespace {

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

std::optional<juce::File> PresetManager::fileFor(const juce::String& name)
{
    const auto text = name.toStdString();
    if (text.empty() || core::sanitizePresetName(text) != text)
        return std::nullopt;
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

std::optional<core::PresetContents> PresetManager::read(const Entry& entry) const
{
    if (entry.factory)
    {
        for (const auto& preset : core::factoryPresets())
            if (preset.name == entry.name.toStdString())
                return core::deserializePreset(preset.state);
        return std::nullopt;
    }

    const auto file = fileFor(entry.name);
    if (!file || !file->existsAsFile())
        return std::nullopt;
    return core::deserializePreset(file->loadFileAsString().toStdString());
}

void PresetManager::apply(const core::PresetContents& contents)
{
    // Lo que el usuario acaba de hacer (un gesto que aún espera su registro)
    // es una acción aparte: deshacer la carga no lo deshace.
    processor_.history().flushPending();
    processor_.history().touchMacroAssignments();
    // El modo de fase y la compensación no son parte del preset
    // (core::kParametersOutsidePresets). Las asignaciones, sí, siempre: un
    // preset sin ninguna las quita (ADR 0028).
    processor_.applyValues(core::applyPreset(contents.parameters, processor_.currentValues()));
    processor_.setMacroAssignments(contents.macros);
    processor_.history().commitNow();
}

void PresetManager::setCurrent(const juce::String& name)
{
    auto ui = processor_.uiSettings();
    ui.presetName = name.toStdString();
    processor_.setUiSettings(ui);
}

bool PresetManager::load(const Entry& entry)
{
    const auto contents = read(entry);
    if (!contents)
        return false;

    setCurrent(entry.name);
    apply(*contents);
    cachedName_ = entry.name;
    cachedValues_ = core::PresetContents{processor_.currentValues(), processor_.macroAssignments()};
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
    setCurrent({});
    apply(core::PresetContents{});
    cachedName_.clear();
    cachedValues_.reset();
}

bool PresetManager::saveUser(const juce::String& name)
{
    const auto clean = toJuce(core::sanitizePresetName(name.toStdString()));
    if (clean.isEmpty())
        return false;

    const auto directory = userDirectory();
    if (!directory.isDirectory() && directory.createDirectory().failed())
        return false;

    const core::PresetContents contents{processor_.currentValues(), processor_.macroAssignments()};
    const auto file = fileFor(clean);
    if (!file ||
        !file->replaceWithText(toJuce(core::serializePreset(contents.parameters, contents.macros)), false, false, "\n"))
        return false;

    cachedName_ = clean;
    cachedValues_ = contents;
    setCurrent(clean);
    return true;
}

bool PresetManager::userPresetExists(const juce::String& name) const
{
    const auto clean = toJuce(core::sanitizePresetName(name.toStdString()));
    const auto file = fileFor(clean);
    return file && file->existsAsFile();
}

bool PresetManager::deleteUser(const juce::String& name)
{
    const auto file = fileFor(name);
    if (!file || !file->existsAsFile() || !file->deleteFile())
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
    if (const auto file = fileFor(name); file && file->existsAsFile())
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
    // preset a estos efectos, ni el modo de fase y la compensación, que ni
    // siquiera se guardan en él: tocarlos no lo marca como modificado.
    auto a = processor_.currentValues();
    auto b = cachedValues_->parameters;
    for (const auto index : {core::kBypass, core::kOutputGain})
        a[index] = b[index];
    for (const auto index : core::kParametersOutsidePresets)
        a[index] = b[index];
    return a != b || !(processor_.macroAssignments() == cachedValues_->macros);
}

} // namespace esceq
