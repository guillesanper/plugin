# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Capa JUCE: traduce entre el host y core/dsp. Es el único módulo (junto
# con ui/, cuando exista) que puede incluir cabeceras de JUCE.

set(ESCEQ_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND ESCEQ_FORMATS AU)
endif()

# CONTRATO: PLUGIN_MANUFACTURER_CODE, PLUGIN_CODE y BUNDLE_ID identifican al
# plugin ante el host. Cambiarlos hace que los proyectos guardados dejen de
# encontrarlo. Ver docs/arquitectura.md.
juce_add_plugin(esceQ
    PRODUCT_NAME "esceQ"
    COMPANY_NAME "esceQ"
    BUNDLE_ID "org.esceq.esceq"
    PLUGIN_MANUFACTURER_CODE Escq
    PLUGIN_CODE EscQ
    FORMATS ${ESCEQ_FORMATS}
    VST3_CATEGORIES Fx EQ
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(esceQ PRIVATE
    src/PluginProcessor.cpp
    src/PluginProcessor.h)

target_compile_definitions(esceQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(esceQ
    PRIVATE
        esceq_core
        esceq_dsp
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
