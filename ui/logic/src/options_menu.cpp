// SPDX-License-Identifier: AGPL-3.0-or-later
#include "esceq/ui/options_menu.h"

namespace esceq::ui {

namespace {

/// Las entradas de los algoritmos van de kFirstAlgorithmId en adelante, en el
/// orden del enum.
constexpr int kFirstAlgorithmId = 100;

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

std::string optionsTooltip(dsp::FilterAlgorithm current)
{
    return std::string("Options \xC2\xB7 ") + kHfAccuracyHeading + ": " + algorithmName(current);
}

} // namespace esceq::ui
