# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Modelo de parámetros, estado guardado y traducción a ajustes del motor.
# C++20 puro: puede depender de dsp/, nunca de JUCE ni de plugin/.
# Ver docs/arquitectura.md.
add_library(esceq_core STATIC
    src/band_model.cpp
    src/parameters.cpp
    src/state.cpp)

target_include_directories(esceq_core PUBLIC include)
target_link_libraries(esceq_core
    PUBLIC esceq_dsp
    PRIVATE esceq_warnings)
