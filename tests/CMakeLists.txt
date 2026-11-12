# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests de dsp/, core/ y ui/logic. No dependen de JUCE.
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(esceq_tests
    dsp/analysis_test.cpp
    dsp/band_filter_test.cpp
    dsp/decibels_test.cpp
    dsp/dynamics_test.cpp
    dsp/equalizer_channel_test.cpp
    dsp/equalizer_test.cpp
    dsp/filter_design_test.cpp
    dsp/filter_golden_test.cpp
    dsp/gain_ramp_test.cpp
    core/band_model_test.cpp
    core/history_test.cpp
    core/parameters_test.cpp
    core/presets_test.cpp
    core/state_test.cpp
    ui/control_logic_test.cpp
    ui/curve_mapping_test.cpp
    ui/layout_test.cpp
    ui/meter_logic_test.cpp)

target_compile_definitions(esceq_tests PRIVATE
    ESCEQ_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

find_package(Threads REQUIRED)

target_link_libraries(esceq_tests PRIVATE
    esceq_dsp
    esceq_core
    esceq_ui_logic
    Threads::Threads
    Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(esceq_tests)
