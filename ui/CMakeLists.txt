# SPDX-License-Identifier: AGPL-3.0-or-later
#
# ui/logic: lógica de la interfaz en C++20 puro (escalas, detección de nodos).
# Se testea sin ventana y no depende de JUCE.
#
# ui/src: componentes gráficos con JUCE. No forman una biblioteca propia:
# se compilan dentro del plugin (ver plugin/CMakeLists.txt), porque los
# módulos de JUCE deben compilarse una sola vez por binario.
add_library(esceq_ui_logic STATIC
    logic/src/control_logic.cpp
    logic/src/curve_mapping.cpp
    logic/src/layout.cpp
    logic/src/meter_logic.cpp)

target_include_directories(esceq_ui_logic PUBLIC logic/include)
target_link_libraries(esceq_ui_logic
    PUBLIC esceq_core
    PRIVATE esceq_warnings)

# Fuentes de ui/src, para el plugin y para tools/snapshot.
set(ESCEQ_UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(ESCEQ_UI_DIR ${ESCEQ_UI_DIR} PARENT_SCOPE)
set(ESCEQ_UI_SOURCES
    ${ESCEQ_UI_DIR}/BandPanel.cpp
    ${ESCEQ_UI_DIR}/BandPanel.h
    ${ESCEQ_UI_DIR}/Controls.cpp
    ${ESCEQ_UI_DIR}/Controls.h
    ${ESCEQ_UI_DIR}/CurveView.cpp
    ${ESCEQ_UI_DIR}/CurveView.h
    ${ESCEQ_UI_DIR}/Draw.cpp
    ${ESCEQ_UI_DIR}/Draw.h
    ${ESCEQ_UI_DIR}/EqEditor.cpp
    ${ESCEQ_UI_DIR}/EqEditor.h
    ${ESCEQ_UI_DIR}/EsceqLookAndFeel.cpp
    ${ESCEQ_UI_DIR}/EsceqLookAndFeel.h
    ${ESCEQ_UI_DIR}/OutputMeter.cpp
    ${ESCEQ_UI_DIR}/OutputMeter.h
    ${ESCEQ_UI_DIR}/Theme.cpp
    ${ESCEQ_UI_DIR}/Theme.h
    PARENT_SCOPE)
