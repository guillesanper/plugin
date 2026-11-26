// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "esceq/core/parameters.h"
#include "esceq/core/presets.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace esceq {

class PluginProcessor;

/// Presets de fábrica (los de core::factoryPresets()) y de usuario (archivos
/// en una carpeta del sistema). Un preset es un estado de esceQ con solo
/// parámetros y las asignaciones de las macros (los de fábrica no llevan
/// ninguna); cargarlo sustituye las asignaciones por las suyas, y es una
/// acción que se puede deshacer.
///
/// Carpeta de los presets de usuario:
///  - Windows: %APPDATA%\esceQ\Presets
///  - macOS:   ~/Library/Application Support/esceQ/Presets
///  - Linux:   ~/.config/esceQ/Presets
/// La variable de entorno ESCEQ_PRESETS_DIR (ruta absoluta) la sustituye.
///
/// Se usa desde el hilo de mensajes. Lee y escribe archivos: nunca desde el hilo de audio.
class PresetManager final
{
public:
    struct Entry
    {
        juce::String name;
        bool factory = false;

        bool operator==(const Entry& other) const { return name == other.name && factory == other.factory; }
    };

    explicit PresetManager(PluginProcessor& processor);

    static juce::File userDirectory();

    /// Los de fábrica, en su orden, y después los de usuario por orden alfabético.
    std::vector<Entry> presets() const;

    /// Carga un preset y lo registra como una acción que se puede deshacer.
    bool load(const Entry& entry);

    /// Carga el anterior (-1) o el siguiente (+1) de la lista, dando la vuelta.
    void step(int direction);

    /// Vuelve todos los parámetros a su valor por defecto y quita las
    /// asignaciones de las macros.
    void loadDefault();

    /// Guarda los parámetros actuales y las asignaciones de las macros como
    /// preset de usuario. Devuelve false si el nombre no sirve o no se puede
    /// escribir el archivo.
    bool saveUser(const juce::String& name);
    bool userPresetExists(const juce::String& name) const;
    bool deleteUser(const juce::String& name);

    /// Preset cargado (según el nombre guardado en el estado), si lo hay.
    std::optional<Entry> current() const;

    /// true si los parámetros o las asignaciones ya no son los del preset cargado.
    bool isModified() const;

private:
    /// Archivo de un preset de usuario, o nullopt si `name` no es un nombre de
    /// preset válido. El nombre puede venir de un estado guardado (ui.preset):
    /// uno con rutas ("../x", "/x") o caracteres reservados nunca llega al disco.
    static std::optional<juce::File> fileFor(const juce::String& name);
    std::optional<core::PresetContents> read(const Entry& entry) const;
    /// Carga `contents` como una acción: los parámetros (salvo
    /// core::kParametersOutsidePresets) y las asignaciones.
    void apply(const core::PresetContents& contents);
    void setCurrent(const juce::String& name);

    PluginProcessor& processor_;

    // Lo que tiene el preset cargado, para saber si se ha modificado. Se lee
    // del archivo la primera vez que hace falta (tras cargar un proyecto).
    mutable juce::String cachedName_;
    mutable std::optional<core::PresetContents> cachedValues_;
};

} // namespace esceq
