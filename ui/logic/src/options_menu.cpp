// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/options_menu.h"

#include "esceq/ui/notes.h"

#include <array>

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

std::string optionsTooltip(dsp::FilterAlgorithm current)
{
    return std::string("Options \xC2\xB7 ") + kHfAccuracyHeading + ": " + algorithmName(current);
}

} // namespace esceq::ui
