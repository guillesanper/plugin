// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Único punto en el que el procesador conoce a la interfaz. Está en un
// archivo aparte para que los tests del procesador (tests/plugin) puedan
// compilarse sin ui/src.
#include "EqEditor.h"
#include "PluginProcessor.h"

namespace esceq {

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new ui::EqEditor(*this);
}

} // namespace esceq
