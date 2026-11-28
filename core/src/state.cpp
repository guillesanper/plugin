// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/core/state.h"

#include "text_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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
constexpr std::string_view kUiNotes = "ui.notes";
constexpr std::string_view kUiA4 = "ui.a4";
constexpr std::string_view kUiAnalyzerFft = "ui.analyzer_fft";
constexpr std::string_view kUiAnalyzerFall = "ui.analyzer_fall";
constexpr std::string_view kUiAnalyzerTilt = "ui.analyzer_tilt";
constexpr std::string_view kUiAnalyzerPeaks = "ui.analyzer_peaks";
constexpr std::string_view kUiOutputStage = "ui.output_stage";
constexpr std::string_view kUiOutputTab = "ui.output_tab";

constexpr std::string_view kFilterAlgorithm = "filter_algorithm";
/// Las asignaciones de las macros: `macro.N.M <destino> <profundidad>`.
constexpr std::string_view kMacroPrefix = "macro.";
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

/// Migración de la versión 3 a la 4.
///
/// La versión 4 añade las asignaciones de las macros. Los estados anteriores
/// no tenían macros: cargan sin asignaciones, y suenan igual. Una línea
/// `macro.*` en un estado que dice ser anterior no la escribió esceQ y se
/// descarta.
void migrate3to4(std::vector<Entry>& entries)
{
    std::erase_if(entries, [](const Entry& entry) { return entry.key.starts_with(kMacroPrefix); });
}

/// Un número de 1 a 9 escrito con una sola cifra ("1".."9"), o 0.
int singleDigit(std::string_view text) noexcept
{
    return text.size() == 1 && text[0] >= '1' && text[0] <= '9' ? text[0] - '0' : 0;
}

/// Lee una línea `macro.N.M <destino> <profundidad>` en `macros`. Lo que no se
/// entienda se descarta (ver deserializePluginState()).
void readMacroAssignment(const Entry& entry, MacroAssignments& macros)
{
    const std::string_view position = entry.key.substr(kMacroPrefix.size());
    const auto dot = position.find('.');
    if (dot == std::string_view::npos)
        return;
    const int macro = singleDigit(position.substr(0, dot));
    const int slot = singleDigit(position.substr(dot + 1));
    if (macro < 1 || macro > kNumMacros || slot < 1 || slot > kMaxMacroTargets)
        return;

    const Entry target = splitLine(entry.value);
    const auto index = findParameter(target.key);
    float depth = 0.0f;
    if (!index || !parseNumber(target.value, depth))
    {
        macros.clearSlot(macro - 1, slot - 1);
        return;
    }
    // Si la línea repite un hueco, sustituye a la anterior.
    macros.clearSlot(macro - 1, slot - 1);
    macros.setSlot(macro - 1, slot - 1, *index, depth);
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
    settings.a4Hz = validA4(settings.a4Hz).value_or(defaults.a4Hz);

    const auto oneOf = [](const auto& allowed, auto value) {
        return std::find(std::begin(allowed), std::end(allowed), value) != std::end(allowed);
    };
    if (!oneOf(kAnalyzerFftSizes, settings.analyzerFftSize))
        settings.analyzerFftSize = defaults.analyzerFftSize;
    if (!oneOf(kAnalyzerFallTimesMs, settings.analyzerFallMs))
        settings.analyzerFallMs = defaults.analyzerFallMs;
    // Comparación exacta a propósito: solo valen esos valores.
    if (!oneOf(kAnalyzerTilts, settings.analyzerTiltDbPerOctave))
        settings.analyzerTiltDbPerOctave = defaults.analyzerTiltDbPerOctave;
    return settings;
}

std::optional<double> validA4(double a4Hz) noexcept
{
    if (!std::isfinite(a4Hz))
        return std::nullopt;
    const double rounded = std::round(a4Hz * 10.0) / 10.0;
    if (rounded < kMinA4Hz || rounded > kMaxA4Hz)
        return std::nullopt;
    return rounded;
}

std::string serializePluginState(const PluginState& state)
{
    std::string out = serializeState(state.parameters);

    const UiSettings ui = sanitize(state.ui);
    std::ostringstream extra;
    extra.imbue(std::locale::classic());
    extra << kFilterAlgorithm << ' '
          << static_cast<int>(filterAlgorithmFromStored(static_cast<int>(state.filterAlgorithm))) << '\n';
    extra << serializeMacroAssignments(state.macros);
    extra << kUiAnalyzer << ' ' << ui.analyzerMode << '\n';
    extra << kUiRange << ' ' << ui.displayRangeDb << '\n';
    if (ui.windowWidth > 0)
    {
        extra << kUiWidth << ' ' << ui.windowWidth << '\n';
        extra << kUiHeight << ' ' << ui.windowHeight << '\n';
    }
    if (!ui.presetName.empty())
        extra << kUiPreset << ' ' << ui.presetName << '\n';
    // Las de la escala de notas, solo si no tienen su valor por defecto: un
    // estado que no las usa se escribe igual que antes de existir.
    if (ui.pianoKeyboard)
        extra << kUiNotes << " 1\n";
    if (ui.a4Hz != kDefaultA4Hz)
        extra << kUiA4 << ' ' << ui.a4Hz << '\n';
    // Las del analizador ampliado, igual.
    const UiSettings defaults{};
    if (ui.analyzerFftSize != defaults.analyzerFftSize)
        extra << kUiAnalyzerFft << ' ' << ui.analyzerFftSize << '\n';
    if (ui.analyzerFallMs != defaults.analyzerFallMs)
        extra << kUiAnalyzerFall << ' ' << ui.analyzerFallMs << '\n';
    if (ui.analyzerTiltDbPerOctave != defaults.analyzerTiltDbPerOctave)
        extra << kUiAnalyzerTilt << ' ' << ui.analyzerTiltDbPerOctave << '\n';
    if (ui.analyzerPeakHold)
        extra << kUiAnalyzerPeaks << " 1\n";
    // El cajón de la etapa de salida, solo plegado: abierto es el de siempre.
    if (!ui.outputStageOpen)
        extra << kUiOutputStage << " 0\n";
    if (ui.outputStageMacros)
        extra << kUiOutputTab << " 1\n";
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

std::string serializeMacroAssignments(const MacroAssignments& macros)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(std::numeric_limits<float>::max_digits10);
    for (int macro = 0; macro < kNumMacros; ++macro)
    {
        const auto& targets = macros.targets(macro);
        for (std::size_t slot = 0; slot < targets.size(); ++slot)
        {
            const MacroTarget& target = targets[slot];
            // Solo lo que se puede volver a leer (un MacroAssignments solo
            // contiene eso, pero el formato no debe depender de ello).
            if (!target.used() || !isMacroTarget(target.parameter) || !std::isfinite(target.depth))
                continue;
            out << kMacroPrefix << macro + 1 << '.' << slot + 1 << ' ' << parameters()[target.parameter].id << ' '
                << std::clamp(target.depth, -kMaxMacroDepth, kMaxMacroDepth) << '\n';
        }
    }
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
    if (version < 4)
        migrate3to4(entries);

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

        if (entry.key.starts_with(kMacroPrefix))
        {
            readMacroAssignment(entry, state.macros);
            continue;
        }

        int number = 0;
        if (entry.key == kUiPreset)
            state.ui.presetName = std::string(entry.value);
        else if (entry.key == kUiA4)
        {
            double a4 = 0.0;
            state.ui.a4Hz = parseNumber(entry.value, a4) ? a4 : kDefaultA4Hz;
        }
        else if (entry.key == kUiAnalyzerTilt)
        {
            double tilt = 0.0;
            if (parseNumber(entry.value, tilt))
                state.ui.analyzerTiltDbPerOctave = tilt;
        }
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
        else if (entry.key == kUiNotes && (number == 0 || number == 1))
            state.ui.pianoKeyboard = number == 1;
        else if (entry.key == kUiAnalyzerFft)
            state.ui.analyzerFftSize = number;
        else if (entry.key == kUiAnalyzerFall)
            state.ui.analyzerFallMs = number;
        else if (entry.key == kUiAnalyzerPeaks && (number == 0 || number == 1))
            state.ui.analyzerPeakHold = number == 1;
        else if (entry.key == kUiOutputStage && (number == 0 || number == 1))
            state.ui.outputStageOpen = number == 1;
        else if (entry.key == kUiOutputTab && (number == 0 || number == 1))
            state.ui.outputStageMacros = number == 1;
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
