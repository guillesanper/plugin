# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Modelo de parámetros, estado guardado y traducción a ajustes del motor.
# C++20 puro: puede depender de dsp/, nunca de JUCE ni de plugin/.
# Ver docs/arquitectura.md.
add_library(esceq_core STATIC
    src/band_model.cpp
    src/control_thread.cpp
    src/history.cpp
    src/parameters.cpp
    src/presets.cpp
    src/spectrum_session.cpp
    src/state.cpp)

# Quien use la biblioteca necesita C++20 para sus cabeceras.
target_compile_features(esceq_core PUBLIC cxx_std_20)
target_include_directories(esceq_core PUBLIC include)
# spectrum_session y control_thread usan un hilo propio.
find_package(Threads REQUIRED)
target_link_libraries(esceq_core
    PUBLIC esceq_dsp Threads::Threads
    PRIVATE esceq_warnings)
