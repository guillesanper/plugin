// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace esceq::ui::palette {

inline const juce::Colour background{0xff15171c};
inline const juce::Colour panel{0xff1d2027};
inline const juce::Colour grid{0xff2a2e37};
inline const juce::Colour gridStrong{0xff3a3f4b};
inline const juce::Colour text{0xffaab0bd};
inline const juce::Colour textDim{0xff6b7280};
inline const juce::Colour curve{0xfff2f4f8};
inline const juce::Colour analyzerPost{0xff4a7fb5};
inline const juce::Colour analyzerPre{0xff7a8392};
inline const juce::Colour warning{0xffe0a050};

/// Color de una banda: tonos repartidos por la proporción áurea para que
/// bandas consecutivas se distingan bien.
inline juce::Colour band(int index)
{
    const float hue = std::fmod(0.58f + 0.618034f * static_cast<float>(index), 1.0f);
    return juce::Colour::fromHSV(hue, 0.55f, 0.95f, 1.0f);
}

} // namespace esceq::ui::palette
