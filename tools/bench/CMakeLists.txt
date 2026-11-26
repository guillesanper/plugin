# SPDX-License-Identifier: AGPL-3.0-or-later
#
# esceq_bench: coste del hilo de audio (dsp::PhaseEqualizer) con
# configuraciones fijas, del ajuste de la igualación (--match) y de la
# saturación (--saturation), una banda modulada (--modulation) y las macros
# (--macros). Depende de dsp/ y core/: se compila también sin JUCE.
# Ver la cabecera de main.cpp y la ADR 0020.
add_executable(esceq_bench main.cpp)
target_link_libraries(esceq_bench PRIVATE esceq_dsp esceq_core esceq_warnings)

# Que la herramienta funcione en todas las plataformas del CI: una medida
# muy corta de cada escenario y modo, y de la convolución.
if(ESCEQ_BUILD_TESTS)
    add_test(NAME esceq_bench_smoke
             COMMAND esceq_bench --rate 48000 --block 64 --seconds 0.02 --repeat 1)
    add_test(NAME esceq_bench_convolution_smoke
             COMMAND esceq_bench --convolution --rate 48000 --repeat 1)
    add_test(NAME esceq_bench_match_smoke
             COMMAND esceq_bench --match --rate 48000 --repeat 1)
    add_test(NAME esceq_bench_saturation_smoke
             COMMAND esceq_bench --saturation --rate 48000 --block 64 --seconds 0.02 --repeat 1)
    add_test(NAME esceq_bench_modulation_smoke
             COMMAND esceq_bench --modulation --rate 48000 --block 256 --seconds 0.05 --repeat 1)
    add_test(NAME esceq_bench_macros_smoke
             COMMAND esceq_bench --macros --repeat 1)
endif()
