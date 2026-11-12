// SPDX-License-Identifier: MIT
//
// Canario de RealtimeSanitizer (ver tests/CMakeLists.txt): asigna memoria
// dentro de una función de tiempo real. Con el sanitizador activo, el
// programa aborta; CTest espera ese fallo.
#include "esceq/dsp/realtime.h"

#include <cstdio>
#include <vector>

namespace {

int allocateInRealtime(int size) noexcept ESCEQ_NONBLOCKING
{
    std::vector<int> data(static_cast<std::size_t>(size), 1); // asigna memoria: prohibido aquí
    return data[0];
}

} // namespace

int main(int argc, char**)
{
    const int value = allocateInRealtime(64 + argc);
    std::printf("RealtimeSanitizer no ha detectado la asignacion (valor %d)\n", value);
    return 0;
}
