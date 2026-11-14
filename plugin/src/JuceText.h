// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_core/juce_core.h>

#include <string_view>

namespace esceq {

/// Texto UTF-8 de core (nombres, opciones, valores formateados) como juce::String.
inline juce::String toJuce(std::string_view text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

} // namespace esceq
