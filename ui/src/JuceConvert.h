// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "JuceText.h"

#include "esceq/ui/curve_mapping.h"

#include <juce_graphics/juce_graphics.h>

/// De los tipos de ui/logic y core a los de JUCE. Las dos sobrecargas de
/// toJuce viven juntas en esceq::ui para que una no oculte a la otra.
namespace esceq::ui {

using esceq::toJuce;

inline juce::Rectangle<float> toJuce(const Rect& r)
{
    return {r.x, r.y, r.width, r.height};
}

} // namespace esceq::ui
