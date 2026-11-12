// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include <catch2/catch_session.hpp>

#include <cstdlib>
#include <juce_audio_utils/juce_audio_utils.h>

// El editor no se prueba aquí: así estos tests no dependen de ui/src.
juce::AudioProcessorEditor* esceq::PluginProcessor::createEditor()
{
    return nullptr;
}

int main(int argc, char* argv[])
{
    // Los presets de usuario de las pruebas van a una carpeta temporal, no a la del usuario.
    const auto presets = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("esceq_tests_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
#if JUCE_WINDOWS
    _putenv_s("ESCEQ_PRESETS_DIR", presets.getFullPathName().toRawUTF8());
#else
    setenv("ESCEQ_PRESETS_DIR", presets.getFullPathName().toRawUTF8(), 1);
#endif

    int result = 0;
    {
        const juce::ScopedJuceInitialiser_GUI juce;
        result = Catch::Session().run(argc, argv);
    }
    presets.deleteRecursively();
    return result;
}
