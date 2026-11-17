// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/options_menu.h"

#include "esceq/ui/notes.h"

#include "esceq/core/state.h"

#include <array>
#include <iterator>
#include <locale>
#include <sstream>

namespace esceq::ui {

namespace {

/// Las entradas de los algoritmos van de kFirstAlgorithmId en adelante, en el
/// orden del enum.
constexpr int kFirstAlgorithmId = 100;
/// Y las de los tamaños, de kFirstSizeId en adelante, en el orden de SizePreset.
constexpr int kFirstSizeId = 200;
/// La escala de notas: el teclado, las afinaciones habituales y "Other...".
constexpr int kPianoKeyboardId = 300;
constexpr int kFirstTuningId = 310;
constexpr int kCustomTuningId = 330;
/// El analizador: los valores de cada submenú, en el orden de sus tablas, y
/// los dos interruptores.
constexpr int kFirstFftId = 400;
constexpr int kFirstFallId = 410;
constexpr int kFirstTiltId = 420;
constexpr int kPeakHoldId = 430;
constexpr int kFreezeId = 431;

const char* fallLabel(int fallMs)
{
    switch (fallMs)
    {
    case 1000:
        return "Slow (1 s)";
    case 120:
        return "Fast (0.12 s)";
    default:
        return "Medium (0.35 s)";
    }
}

std::string tiltLabel(double tilt)
{
    // 0, 3, 4,5 o 6: con el decimal solo si lo tiene, y siempre con punto.
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << tilt << " dB/oct";
    if (tilt == 0.0) // NOLINT(clang-diagnostic-float-equal): sin inclinación, exactamente
        text << " (flat)";
    return text.str();
}

/// Índice de `menuId` entre `count` entradas que empiezan en `first`, o -1.
int indexFor(int menuId, int first, std::size_t count) noexcept
{
    const int index = menuId - first;
    return index >= 0 && index < static_cast<int>(count) ? index : -1;
}

struct CommonTuning
{
    double a4Hz;
    const char* label;
};

constexpr std::array<CommonTuning, 6> kCommonTunings{{{415.0, "415 Hz (baroque)"},
                                                      {432.0, "432 Hz"},
                                                      {440.0, "440 Hz (standard)"},
                                                      {442.0, "442 Hz"},
                                                      {443.0, "443 Hz"},
                                                      {444.0, "444 Hz"}}};

const char* sizeName(SizePreset preset)
{
    switch (preset)
    {
    case SizePreset::Small:
        return "Small";
    case SizePreset::Large:
        return "Large";
    case SizePreset::Medium:
        break;
    }
    return "Medium";
}

const char* algorithmName(dsp::FilterAlgorithm algorithm)
{
    switch (algorithm)
    {
    case dsp::FilterAlgorithm::Classic:
        break;
    case dsp::FilterAlgorithm::Matched:
        return "Matched";
    }
    return "Classic";
}

const char* algorithmLabel(dsp::FilterAlgorithm algorithm)
{
    switch (algorithm)
    {
    case dsp::FilterAlgorithm::Classic:
        break;
    case dsp::FilterAlgorithm::Matched:
        return "Matched (analog-accurate highs)";
    }
    return "Classic (as in esceQ 0.1)";
}

} // namespace

std::vector<FilterAlgorithmItem> filterAlgorithmItems(dsp::FilterAlgorithm current)
{
    std::vector<FilterAlgorithmItem> items;
    for (int i = 0; i < dsp::kNumFilterAlgorithms; ++i)
    {
        const auto algorithm = static_cast<dsp::FilterAlgorithm>(i);
        items.push_back({kFirstAlgorithmId + i, algorithm, algorithmLabel(algorithm), algorithm == current});
    }
    return items;
}

std::optional<dsp::FilterAlgorithm> filterAlgorithmForMenuId(int menuId) noexcept
{
    const int index = menuId - kFirstAlgorithmId;
    if (index < 0 || index >= dsp::kNumFilterAlgorithms)
        return std::nullopt;
    return static_cast<dsp::FilterAlgorithm>(index);
}

std::vector<WindowSizeItem> windowSizeItems(std::optional<SizePreset> current)
{
    std::vector<WindowSizeItem> items;
    for (int i = 0; i < kNumSizePresets; ++i)
    {
        const auto preset = static_cast<SizePreset>(i);
        const auto size = windowSize(preset);
        const std::string label = std::string(sizeName(preset)) + " (" + std::to_string(size.width) + " \xC3\x97 " +
                                  std::to_string(size.height) + ")";
        items.push_back({kFirstSizeId + i, preset, label, current == preset});
    }
    return items;
}

std::optional<SizePreset> windowSizeForMenuId(int menuId) noexcept
{
    const int index = menuId - kFirstSizeId;
    if (index < 0 || index >= kNumSizePresets)
        return std::nullopt;
    return static_cast<SizePreset>(index);
}

ToggleItem pianoKeyboardItem(bool shown)
{
    return {kPianoKeyboardId, "Piano keyboard", shown};
}

bool isPianoKeyboardMenuId(int menuId) noexcept
{
    return menuId == kPianoKeyboardId;
}

std::string tuningMenuTitle(double a4Hz)
{
    return "Tuning: A4 = " + tuningText(a4Hz);
}

std::vector<TuningItem> tuningItems(double a4Hz)
{
    std::vector<TuningItem> items;
    bool common = false;
    for (std::size_t i = 0; i < kCommonTunings.size(); ++i)
    {
        const auto& tuning = kCommonTunings[i];
        // NOLINTNEXTLINE(clang-diagnostic-float-equal): las afinaciones van redondeadas a 0,1 Hz
        const bool ticked = tuning.a4Hz == a4Hz;
        common = common || ticked;
        items.push_back({kFirstTuningId + static_cast<int>(i), tuning.a4Hz, tuning.label, ticked});
    }
    items.push_back(
        {kCustomTuningId, std::nullopt, common ? "Other..." : "Other (" + tuningText(a4Hz) + ")...", !common});
    return items;
}

std::optional<double> tuningForMenuId(int menuId) noexcept
{
    const int index = menuId - kFirstTuningId;
    if (index < 0 || index >= static_cast<int>(kCommonTunings.size()))
        return std::nullopt;
    return kCommonTunings[static_cast<std::size_t>(index)].a4Hz;
}

bool isCustomTuningMenuId(int menuId) noexcept
{
    return menuId == kCustomTuningId;
}

std::vector<AnalyzerSubmenu> analyzerSubmenus(const AnalyzerView& view)
{
    std::vector<AnalyzerSubmenu> submenus(3);

    submenus[0].title = "Resolution: " + std::to_string(view.fftSize) + " points";
    for (std::size_t i = 0; i < std::size(core::kAnalyzerFftSizes); ++i)
    {
        const int fft = core::kAnalyzerFftSizes[i];
        submenus[0].items.push_back(
            {kFirstFftId + static_cast<int>(i), std::to_string(fft) + " points", fft == view.fftSize});
    }

    submenus[1].title = std::string("Fall: ") + fallLabel(view.fallMs);
    for (std::size_t i = 0; i < std::size(core::kAnalyzerFallTimesMs); ++i)
    {
        const int fall = core::kAnalyzerFallTimesMs[i];
        submenus[1].items.push_back({kFirstFallId + static_cast<int>(i), fallLabel(fall), fall == view.fallMs});
    }

    submenus[2].title = "Tilt: " + tiltLabel(view.tiltDbPerOctave);
    for (std::size_t i = 0; i < std::size(core::kAnalyzerTilts); ++i)
    {
        const double tilt = core::kAnalyzerTilts[i];
        // NOLINTNEXTLINE(clang-diagnostic-float-equal): la inclinación es uno de esos valores exactos
        const bool ticked = tilt == view.tiltDbPerOctave;
        submenus[2].items.push_back({kFirstTiltId + static_cast<int>(i), tiltLabel(tilt), ticked});
    }
    return submenus;
}

ToggleItem peakHoldItem(bool on)
{
    return {kPeakHoldId, "Peak hold", on};
}

ToggleItem freezeItem(bool on)
{
    return {kFreezeId, "Freeze", on};
}

std::optional<AnalyzerView> analyzerViewForMenuId(AnalyzerView current, int menuId) noexcept
{
    if (const int i = indexFor(menuId, kFirstFftId, std::size(core::kAnalyzerFftSizes)); i >= 0)
        current.fftSize = core::kAnalyzerFftSizes[static_cast<std::size_t>(i)];
    else if (const int j = indexFor(menuId, kFirstFallId, std::size(core::kAnalyzerFallTimesMs)); j >= 0)
        current.fallMs = core::kAnalyzerFallTimesMs[static_cast<std::size_t>(j)];
    else if (const int k = indexFor(menuId, kFirstTiltId, std::size(core::kAnalyzerTilts)); k >= 0)
        current.tiltDbPerOctave = core::kAnalyzerTilts[static_cast<std::size_t>(k)];
    else if (menuId == kPeakHoldId)
        current.peakHold = !current.peakHold;
    else if (menuId == kFreezeId)
        current.frozen = !current.frozen;
    else
        return std::nullopt;
    return current;
}

std::string optionsTooltip(dsp::FilterAlgorithm current)
{
    return std::string("Options \xC2\xB7 ") + kHfAccuracyHeading + ": " + algorithmName(current);
}

} // namespace esceq::ui
