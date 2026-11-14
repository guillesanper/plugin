# SPDX-License-Identifier: MIT
#
# Biblioteca DSP: C++20 puro. No puede depender de core/, de JUCE ni de
# ningún SDK de plugin. Ver docs/arquitectura.md.
add_library(esceq_dsp STATIC
    src/band_filter.cpp
    src/dynamics.cpp
    src/equalizer.cpp
    src/fft.cpp
    src/spectrum_analyzer.cpp
    src/spsc_fifo.cpp
    src/filter_design.cpp
    src/gain_ramp.cpp)

# Quien use la biblioteca necesita C++20 para sus cabeceras.
target_compile_features(esceq_dsp PUBLIC cxx_std_20)
target_include_directories(esceq_dsp PUBLIC include)
target_link_libraries(esceq_dsp PRIVATE esceq_warnings)
