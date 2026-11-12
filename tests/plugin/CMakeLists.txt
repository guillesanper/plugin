# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests de la capa del plugin (plugin/src): los que necesitan JUCE. Comprueban
# lo que une core con el host: el estado que entra y sale por
# get/setStateInformation, el deshacer agrupado por gestos y los presets.
# No incluyen la interfaz: el editor se sustituye por uno vacío.
juce_add_console_app(esceq_plugin_tests PRODUCT_NAME "esceq_plugin_tests")

target_sources(esceq_plugin_tests PRIVATE
    main.cpp
    history_test.cpp
    presets_test.cpp
    state_test.cpp
    ${PROJECT_SOURCE_DIR}/plugin/src/EditHistory.cpp
    ${PROJECT_SOURCE_DIR}/plugin/src/PluginProcessor.cpp
    ${PROJECT_SOURCE_DIR}/plugin/src/PresetManager.cpp)

target_include_directories(esceq_plugin_tests PRIVATE ${PROJECT_SOURCE_DIR}/plugin/src)

target_compile_definitions(esceq_plugin_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    ESCEQ_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures")

target_link_libraries(esceq_plugin_tests PRIVATE
    esceq_core
    esceq_dsp
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    Catch2::Catch2)

add_test(NAME plugin_tests COMMAND esceq_plugin_tests)
