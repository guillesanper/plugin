# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)

project(esceQ VERSION 0.1.0 LANGUAGES C CXX)

# ESCEQ_BUILD_PLUGIN=OFF compila solo dsp/ y core/ (sin descargar JUCE).
# El CI lo usa para garantizar que esos módulos no dependen del framework.
option(ESCEQ_BUILD_PLUGIN "Compila el plugin (descarga JUCE)" ON)
option(ESCEQ_BUILD_TESTS "Compila los tests" ON)
option(ESCEQ_BUILD_TOOLS "Compila herramientas de desarrollo (tools/)" OFF)
# RealtimeSanitizer: los tests abortan si una función marcada ESCEQ_NONBLOCKING
# (las del hilo de audio) asigna memoria o bloquea. Exige Clang 20 o posterior.
option(ESCEQ_RTSAN "Compila con RealtimeSanitizer (Clang >= 20)" OFF)
# Los avisos son errores en nuestro código. Se puede desactivar para probar un
# compilador nuevo que traiga avisos que aún no hemos visto (ver DEV.md).
option(ESCEQ_WARNINGS_AS_ERRORS "Trata los avisos del compilador como errores en dsp/, core/ y ui/logic" ON)

# En Windows las rutas de más de 260 caracteres hacen fallar la configuración
# o la compilación con errores poco claros, y JUCE genera rutas muy largas
# dentro de la carpeta de compilación. Mejor avisar aquí. Ver DEV.md.
if(WIN32)
    string(LENGTH "${CMAKE_BINARY_DIR}" ESCEQ_BINARY_DIR_LENGTH)
    if(ESCEQ_BINARY_DIR_LENGTH GREATER 45)
        message(WARNING
            "La carpeta de compilacion tiene una ruta larga (${ESCEQ_BINARY_DIR_LENGTH} caracteres):\n"
            "  ${CMAKE_BINARY_DIR}\n"
            "Windows limita las rutas a 260 caracteres y JUCE genera rutas muy largas dentro de ella. "
            "Si la configuracion o la compilacion fallan, clona el repositorio en una ruta corta "
            "(por ejemplo C:\\dev\\esceQ) o compila en otra carpeta: cmake -S . -B C:\\b\\esceq. "
            "Ver \"Rutas largas en Windows\" en DEV.md.")
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

if(MSVC)
    # Los fuentes están en UTF-8. /MP compila en paralelo dentro de cada proyecto.
    add_compile_options(/utf-8 /MP)
endif()

# Biblioteca de ejecución de MSVC enlazada de forma estática (/MT): el plugin no
# depende de las DLL del redistribuible de Visual C++, que no todos los
# equipos tienen y que el instalador no incluye. Afecta a todo lo que se
# compila (también JUCE y Catch2), para que nada mezcle las dos. El CI
# comprueba con dumpbin que el VST3 y el CLAP no las importan.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(ESCEQ_RTSAN)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20)
        message(FATAL_ERROR "ESCEQ_RTSAN necesita Clang 20 o posterior (compilador actual: "
                            "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION})")
    endif()
    add_compile_options(-fsanitize=realtime -fno-omit-frame-pointer)
    add_link_options(-fsanitize=realtime)
endif()

# Avisos estrictos para nuestro código (no para dependencias).
add_library(esceq_warnings INTERFACE)
if(MSVC)
    target_compile_options(esceq_warnings INTERFACE /W4 /permissive-)
    if(ESCEQ_WARNINGS_AS_ERRORS)
        target_compile_options(esceq_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(esceq_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(ESCEQ_WARNINGS_AS_ERRORS)
        target_compile_options(esceq_warnings INTERFACE -Werror)
    endif()
endif()

include(FetchContent)

add_subdirectory(dsp)
add_subdirectory(core)
add_subdirectory(ui)

if(ESCEQ_BUILD_PLUGIN)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    # Formato CLAP: JUCE no lo trae, lo añade clap-juce-extensions (MIT).
    # No publica etiquetas recientes, así que se fija por commit.
    option(ESCEQ_BUILD_CLAP "Compila también el formato CLAP" ON)
    if(ESCEQ_BUILD_CLAP)
        FetchContent_Declare(clap-juce-extensions
            GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
            GIT_TAG 6bde108329ff9fc24404b79b8b397261a75cf6a8)
        FetchContent_MakeAvailable(clap-juce-extensions)
    endif()

    add_subdirectory(plugin)
    if(ESCEQ_BUILD_TOOLS)
        add_subdirectory(tools/snapshot)
    endif()
endif()

if(ESCEQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    if(ESCEQ_BUILD_PLUGIN)
        add_subdirectory(tests/plugin)
    endif()
endif()

# Después de los tests: registra su prueba de humo con ctest.
if(ESCEQ_BUILD_TOOLS)
    add_subdirectory(tools/bench)
endif()
