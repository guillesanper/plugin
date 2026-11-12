// SPDX-License-Identifier: MIT
#pragma once

/// ESCEQ_NONBLOCKING marca las funciones que se llaman desde el hilo de
/// audio: no pueden asignar memoria, bloquear ni hacer llamadas al sistema.
///
/// Con un Clang que lo admita (20 o posterior) equivale a [[clang::nonblocking]], y al compilar
/// con -fsanitize=realtime (RealtimeSanitizer, opción ESCEQ_RTSAN de CMake)
/// el programa aborta si una de estas funciones, o cualquier cosa que llame,
/// asigna memoria o bloquea. El CI ejecuta así todos los tests.
///
/// En el resto de compiladores no hace nada: es documentación.
///
/// Se escribe al final de la declaración y de la definición, tras `noexcept`:
///
///     void process(float* const* channels, int numChannels, int numSamples) noexcept ESCEQ_NONBLOCKING;
#if defined(__has_cpp_attribute) && defined(__clang__)
    #if __has_cpp_attribute(clang::nonblocking)
        #define ESCEQ_NONBLOCKING [[clang::nonblocking]]
    #endif
#endif
#ifndef ESCEQ_NONBLOCKING
    #define ESCEQ_NONBLOCKING
#endif
