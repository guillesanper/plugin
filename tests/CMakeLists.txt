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
    dsp/auto_gain_test.cpp
    dsp/band_filter_test.cpp
    dsp/curve_match_test.cpp
    dsp/decibels_test.cpp
    dsp/dynamics_test.cpp
    dsp/equalizer_channel_test.cpp
    dsp/equalizer_test.cpp
    dsp/filter_algorithm_test.cpp
    dsp/filter_design_test.cpp
    dsp/filter_golden_test.cpp
    dsp/gain_ramp_test.cpp
    dsp/handoff_test.cpp
    dsp/linear_phase_test.cpp
    dsp/phase_equalizer_test.cpp
    dsp/saturation_test.cpp
    dsp/spectral_dynamics_test.cpp
    dsp/spectrum_collision_test.cpp
    core/band_model_test.cpp
    core/control_thread_test.cpp
    core/history_test.cpp
    core/macros_test.cpp
    core/match_test.cpp
    core/parameters_test.cpp
    core/presets_test.cpp
    core/release_contract_test.cpp
    core/spectral_mask_test.cpp
    core/spectrum_session_test.cpp
    core/state_test.cpp
    ui/control_logic_test.cpp
    ui/curve_mapping_test.cpp
    ui/layout_test.cpp
    ui/match_test.cpp
    ui/meter_logic_test.cpp
    ui/notes_test.cpp
    ui/options_menu_test.cpp
    ui/output_stage_test.cpp
    ui/session_overlay_test.cpp
    ui/spectral_mask_test.cpp
    ui/spectrum_peaks_test.cpp)

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

# Con RealtimeSanitizer, un "canario": un programa que asigna memoria dentro
# de una función marcada como de tiempo real. DEBE fallar: si pasa, el
# sanitizador no está actuando y los demás tests no estarían comprobando nada.
if(ESCEQ_RTSAN)
    add_executable(esceq_rtsan_canary rtsan_canary.cpp)
    target_link_libraries(esceq_rtsan_canary PRIVATE esceq_dsp)
    add_test(NAME rtsan_detecta_asignaciones COMMAND esceq_rtsan_canary)
    set_tests_properties(rtsan_detecta_asignaciones PROPERTIES WILL_FAIL TRUE)
endif()
