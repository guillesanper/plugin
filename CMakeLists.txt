# SPDX-License-Identifier: AGPL-3.0-or-later
cmake_minimum_required(VERSION 3.24)

project(esceQ VERSION 0.0.1 LANGUAGES C CXX)

# ESCEQ_BUILD_PLUGIN=OFF compila solo dsp/ y core/ (sin descargar JUCE).
# El CI lo usa para garantizar que esos módulos no dependen del framework.
option(ESCEQ_BUILD_PLUGIN "Compila el plugin (descarga JUCE)" ON)
option(ESCEQ_BUILD_TESTS "Compila los tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

if(MSVC)
    # Los fuentes están en UTF-8.
    add_compile_options(/utf-8)
endif()

# Avisos estrictos para nuestro código (no para dependencias).
add_library(esceq_warnings INTERFACE)
if(MSVC)
    target_compile_options(esceq_warnings INTERFACE /W4 /WX /permissive-)
else()
    target_compile_options(esceq_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

include(FetchContent)

add_subdirectory(dsp)
add_subdirectory(core)

if(ESCEQ_BUILD_PLUGIN)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(plugin)
endif()

if(ESCEQ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
