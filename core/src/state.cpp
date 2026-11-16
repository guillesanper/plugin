// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/state.h"

#include "text_utils.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>
#include <vector>

namespace esceq::core {

namespace {

constexpr std::string_view kMagic = "esceQ-state";
constexpr std::string_view kUiPrefix = "ui.";

constexpr std::string_view kUiAnalyzer = "ui.analyzer";
constexpr std::string_view kUiRange = "ui.range";
constexpr std::string_view kUiWidth = "ui.width";
constexpr std::string_view kUiHeight = "ui.height";
constexpr std::string_view kUiPreset = "ui.preset";

constexpr std::string_view kFilterAlgorithm = "filter_algorithm";
/// kLegacyFilterAlgorithm tal y como se escribe en el estado.
constexpr std::string_view kLegacyAlgorithmText = "0";
static_assert(static_cast<int>(kLegacyFilterAlgorithm) == 0);

constexpr int kDisplayRanges[] = {6, 12, 18, 30};
constexpr int kMinStoredWindow = 100;
constexpr int kMaxStoredWindow = 16384;

/// Una línea del estado: `clave valor`, donde el valor es el resto de la línea.
struct Entry
{
    std::string_view key;
    std::string_view value;
};

/// Devuelve la siguiente línea de `text` (sin '\r' final) y la consume.
std::string_view nextLine(std::string_view& text)
{
    const auto end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
    return line;
}

Entry splitLine(std::string_view line)
{
    line = detail::trim(line);
    const auto space = line.find_first_of(" \t");
    if (space == std::string_view::npos)
        return {line, {}};
    return {line.substr(0, space), detail::trim(line.substr(space + 1))};
}

/// Lee el número con el que empieza `text`, con la configuración regional
/// clásica ("C"). Lo que venga detrás se ignora, como en la versión 1.
template <typename T>
bool parseNumber(std::string_view text, T& value)
{
    std::istringstream in{std::string{text}};
    in.imbue(std::locale::classic());
    return static_cast<bool>(in >> value);
}

/// Migración de la versión 1 a la 2.
///
/// La versión 1 solo guardaba parámetros. La 2 añade las preferencias de la
/// interfaz (`ui.*`). Un estado de la versión 1 no las tiene, así que toman
/// sus valores por defecto; y si alguna línea `ui.*` aparece en un estado
/// que dice ser de la versión 1, no la escribió esceQ y se descarta.
void migrate1to2(std::vector<Entry>& entries)
{
    std::erase_if(entries, [](const Entry& entry) { return entry.key.starts_with(kUiPrefix); });
}

/// Migración de la versión 2 a la 3.
///
/// La versión 3 añade el algoritmo de filtro de la instancia. Los estados
/// anteriores se escribieron cuando solo había uno, kLegacyFilterAlgorithm:
/// se cargan con él, que suena bit a bit igual. Va al final porque, si una
/// clave se repite, cuenta la última: una línea `filter_algorithm` en un
/// estado que dice ser anterior no la escribió esceQ y queda anulada.
void migrate2to3(std::vector<Entry>& entries)
{
    entries.push_back({kFilterAlgorithm, kLegacyAlgorithmText});
}

/// Algoritmo a partir de su valor guardado. Lo que no se conozca (un
/// algoritmo de una versión más nueva) toma el de las instancias nuevas.
dsp::FilterAlgorithm filterAlgorithmFromStored(int stored) noexcept
{
    if (stored < 0 || stored >= dsp::kNumFilterAlgorithms)
        return kNewInstanceFilterAlgorithm;
    return static_cast<dsp::FilterAlgorithm>(stored);
}

std::string cleanPresetName(std::string_view name)
{
    std::string out;
    for (const char c : detail::trim(name))
    {
        // Sin caracteres de control: el nombre ocupa una sola línea del estado.
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
            continue;
        out.push_back(c);
    }
    detail::truncateUtf8(out, static_cast<std::size_t>(kMaxPresetNameLength));
    return out;
}

} // namespace

UiSettings sanitize(UiSettings settings)
{
    const UiSettings defaults{};

    if (settings.analyzerMode < 0 || settings.analyzerMode >= kNumAnalyzerModes)
        settings.analyzerMode = defaults.analyzerMode;

    if (std::find(std::begin(kDisplayRanges), std::end(kDisplayRanges), settings.displayRangeDb) == std::end(kDisplayRanges))
        settings.displayRangeDb = defaults.displayRangeDb;

    // El tamaño es un par: si una de las dos medidas no vale, se descartan las dos.
    const auto valid = [](int size) { return size >= kMinStoredWindow && size <= kMaxStoredWindow; };
    if (!valid(settings.windowWidth) || !valid(settings.windowHeight))
    {
        settings.windowWidth = 0;
        settings.windowHeight = 0;
    }

    settings.presetName = cleanPresetName(settings.presetName);
    return settings;
}

std::string serializePluginState(const PluginState& state)
{
    std::string out = serializeState(state.parameters);

    const UiSettings ui = sanitize(state.ui);
    std::ostringstream extra;
    extra.imbue(std::locale::classic());
    extra << kFilterAlgorithm << ' '
          << static_cast<int>(filterAlgorithmFromStored(static_cast<int>(state.filterAlgorithm))) << '\n';
    extra << kUiAnalyzer << ' ' << ui.analyzerMode << '\n';
    extra << kUiRange << ' ' << ui.displayRangeDb << '\n';
    if (ui.windowWidth > 0)
    {
        extra << kUiWidth << ' ' << ui.windowWidth << '\n';
        extra << kUiHeight << ' ' << ui.windowHeight << '\n';
    }
    if (!ui.presetName.empty())
        extra << kUiPreset << ' ' << ui.presetName << '\n';
    return out + extra.str();
}

std::string serializeState(const ParameterValues& values)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(std::numeric_limits<float>::max_digits10);

    out << kMagic << ' ' << kStateVersion << '\n';
    for (std::size_t i = 0; i < kNumParameters; ++i)
        out << parameters()[i].id << ' ' << sanitize(parameters()[i], values[i]) << '\n';
    return out.str();
}

std::optional<PluginState> deserializePluginState(std::string_view text)
{
    const Entry header = splitLine(nextLine(text));
    int version = 0;
    if (header.key != kMagic || !parseNumber(header.value, version) || version < 1)
        return std::nullopt;

    std::vector<Entry> entries;
    while (!text.empty())
    {
        const Entry entry = splitLine(nextLine(text));
        if (!entry.key.empty())
            entries.push_back(entry);
    }

    // Migraciones: cada una lleva el contenido de la versión N a la N+1.
    // Un estado de una versión más nueva que esta no se migra: se lee lo
    // que se conozca.
    if (version < 2)
        migrate1to2(entries);
    if (version < 3)
        migrate2to3(entries);

    PluginState state;
    for (const Entry& entry : entries)
    {
        if (const auto index = findParameter(entry.key))
        {
            float value = 0.0f;
            if (parseNumber(entry.value, value))
                state.parameters[*index] = sanitize(parameters()[*index], value);
            continue;
        }

        int number = 0;
        if (entry.key == kUiPreset)
            state.ui.presetName = std::string(entry.value);
        else if (!parseNumber(entry.value, number))
            continue;
        else if (entry.key == kFilterAlgorithm)
            state.filterAlgorithm = filterAlgorithmFromStored(number);
        else if (entry.key == kUiAnalyzer)
            state.ui.analyzerMode = number;
        else if (entry.key == kUiRange)
            state.ui.displayRangeDb = number;
        else if (entry.key == kUiWidth)
            state.ui.windowWidth = number;
        else if (entry.key == kUiHeight)
            state.ui.windowHeight = number;
    }

    state.ui = sanitize(std::move(state.ui));
    return state;
}

std::optional<ParameterValues> deserializeState(std::string_view text)
{
    if (auto state = deserializePluginState(text))
        return state->parameters;
    return std::nullopt;
}

} // namespace esceq::core
