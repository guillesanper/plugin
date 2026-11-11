# SPDX-License-Identifier: AGPL-3.0-or-later
#
# ui/logic: lógica de la interfaz en C++20 puro (escalas, detección de nodos).
# Se testea sin ventana y no depende de JUCE.
#
# ui/src: componentes gráficos con JUCE. No forman una biblioteca propia:
# se compilan dentro del plugin (ver plugin/CMakeLists.txt), porque los
# módulos de JUCE deben compilarse una sola vez por binario.
add_library(esceq_ui_logic STATIC
    logic/src/curve_mapping.cpp)

target_include_directories(esceq_ui_logic PUBLIC logic/include)
target_link_libraries(esceq_ui_logic
    PUBLIC esceq_core
    PRIVATE esceq_warnings)
