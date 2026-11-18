# SPDX-License-Identifier: AGPL-3.0-or-later
#
# esceq_bench: coste del hilo de audio (dsp::PhaseEqualizer) con
# configuraciones fijas. Solo depende de dsp/: se compila también sin JUCE.
# Ver la cabecera de main.cpp y la ADR 0020.
add_executable(esceq_bench main.cpp)
target_link_libraries(esceq_bench PRIVATE esceq_dsp esceq_warnings)

# Que la herramienta funcione en todas las plataformas del CI: una medida
# muy corta de cada escenario y modo, y de la convolución.
if(ESCEQ_BUILD_TESTS)
    add_test(NAME esceq_bench_smoke
             COMMAND esceq_bench --rate 48000 --block 64 --seconds 0.02 --repeat 1)
    add_test(NAME esceq_bench_convolution_smoke
             COMMAND esceq_bench --convolution --rate 48000 --repeat 1)
endif()
