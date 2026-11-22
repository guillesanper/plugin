# SPDX-License-Identifier: MIT
#
# Biblioteca DSP: C++20 puro. No puede depender de core/, de JUCE ni de
# ningún SDK de plugin. Ver docs/arquitectura.md.
add_library(esceq_dsp STATIC
    src/auto_gain.cpp
    src/band_filter.cpp
    src/curve_match.cpp
    src/dynamics.cpp
    src/equalizer.cpp
    src/fft.cpp
    src/linear_phase.cpp
    src/partitioned_convolver.cpp
    src/phase_equalizer.cpp
    src/saturation.cpp
    src/spectral_dynamics.cpp
    src/spectrum_analyzer.cpp
    src/spectrum_collision.cpp
    src/spsc_fifo.cpp
    src/filter_design.cpp
    src/gain_ramp.cpp)

# Quien use la biblioteca necesita C++20 para sus cabeceras.
target_compile_features(esceq_dsp PUBLIC cxx_std_20)
target_include_directories(esceq_dsp PUBLIC include)
target_link_libraries(esceq_dsp PRIVATE esceq_warnings)

# La curva de la saturación (fastTanh) solo se vectoriza en GCC si puede
# calcular las dos ramas de un `?:` de coma flotante sin miedo a una
# excepción. No cambia ningún resultado: sin reasociar ni contraer, cada
# operación da lo mismo. Clang y MSVC no lo necesitan.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(src/saturation.cpp PROPERTIES COMPILE_OPTIONS -fno-trapping-math)
endif()
