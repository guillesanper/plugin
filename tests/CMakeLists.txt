# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests de dsp/ y core/. No dependen de JUCE.
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(esceq_tests
    dsp/decibels_test.cpp
    dsp/gain_ramp_test.cpp
    core/parameters_test.cpp
    core/state_test.cpp)

target_compile_definitions(esceq_tests PRIVATE
    ESCEQ_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

target_link_libraries(esceq_tests PRIVATE
    esceq_dsp
    esceq_core
    Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(esceq_tests)
