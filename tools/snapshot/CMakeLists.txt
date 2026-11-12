# SPDX-License-Identifier: AGPL-3.0-or-later
#
# esceq_snapshot: abre el editor sin host, con unas bandas de ejemplo y ruido
# rosa pasando por el analizador, y guarda una captura en PNG.
# Sirve para revisar la interfaz sin FL Studio y para la documentación.
juce_add_console_app(esceq_snapshot PRODUCT_NAME "esceq_snapshot")

target_sources(esceq_snapshot PRIVATE
    main.cpp
    ${PROJECT_SOURCE_DIR}/plugin/src/EditHistory.cpp
    ${PROJECT_SOURCE_DIR}/plugin/src/PluginEditorFactory.cpp
    ${PROJECT_SOURCE_DIR}/plugin/src/PluginProcessor.cpp
    ${PROJECT_SOURCE_DIR}/plugin/src/PresetManager.cpp
    ${ESCEQ_UI_SOURCES})

target_include_directories(esceq_snapshot PRIVATE ${PROJECT_SOURCE_DIR}/plugin/src ${ESCEQ_UI_DIR})

target_compile_definitions(esceq_snapshot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(esceq_snapshot PRIVATE
    esceq_core
    esceq_dsp
    esceq_ui_logic
    esceq_assets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
