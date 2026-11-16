// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/options_menu.h"

namespace esceq::ui {

namespace {

/// Las entradas de los algoritmos van de kFirstAlgorithmId en adelante, en el
/// orden del enum.
constexpr int kFirstAlgorithmId = 100;
/// Y las de los tamaños, de kFirstSizeId en adelante, en el orden de SizePreset.
constexpr int kFirstSizeId = 200;

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

std::string optionsTooltip(dsp::FilterAlgorithm current)
{
    return std::string("Options \xC2\xB7 ") + kHfAccuracyHeading + ": " + algorithmName(current);
}

} // namespace esceq::ui
