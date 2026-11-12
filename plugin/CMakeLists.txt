# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Capa JUCE: traduce entre el host y core/dsp. Junto con ui/src, es lo
# único que puede incluir cabeceras de JUCE.

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

# CLAP. CONTRATO: CLAP_ID identifica al plugin ante el host, como los códigos
# de arriba para VST3 y AU. No cambiarlo.
if(ESCEQ_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET esceQ
        CLAP_ID "org.esceq.esceq"
        CLAP_FEATURES audio-effect equalizer stereo mono)
endif()

# Recursos embebidos en el binario: la fuente de la interfaz (Inter, licencia
# OFL, ver ui/assets/fonts/OFL.txt). Así se ve igual en todos los sistemas.
juce_add_binary_data(esceq_assets
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        ${PROJECT_SOURCE_DIR}/ui/assets/fonts/Inter-Regular.ttf
        ${PROJECT_SOURCE_DIR}/ui/assets/fonts/Inter-Medium.ttf
        ${PROJECT_SOURCE_DIR}/ui/assets/fonts/Inter-SemiBold.ttf)
set_target_properties(esceq_assets PROPERTIES POSITION_INDEPENDENT_CODE ON)

# La interfaz (ui/src) se compila aquí: los módulos de JUCE solo pueden
# compilarse una vez por binario. La lista de fuentes está en ui/CMakeLists.txt.
target_sources(esceQ PRIVATE
    src/EditHistory.cpp
    src/EditHistory.h
    src/PluginEditorFactory.cpp
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PresetManager.cpp
    src/PresetManager.h
    ${ESCEQ_UI_SOURCES})

target_include_directories(esceQ PRIVATE src ${ESCEQ_UI_DIR})

target_compile_definitions(esceQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(esceQ
    PRIVATE
        esceq_core
        esceq_dsp
        esceq_ui_logic
        esceq_assets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Con GCC, la optimización en el enlazado (LTO) que activa JUCE se hace en
# serie y cada formato tarda minutos en enlazar. "-flto=auto" reparte el
# trabajo entre los núcleos disponibles; el resultado es el mismo.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(esceQ PUBLIC -flto=auto)
endif()
