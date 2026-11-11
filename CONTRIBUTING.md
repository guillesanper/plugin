# Cómo trabajar en esceQ

## Compilar

Requisitos: CMake ≥ 3.24 y un compilador con C++20 (MSVC 2022, Clang 15+, GCC 12+).

En Linux, además:

```
sudo apt-get install libasound2-dev libx11-dev libxext-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxi-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev
```

Plugin completo (descarga JUCE la primera vez):

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

El VST3 queda en `build/plugin/esceQ_artefacts/Release/VST3/esceQ.vst3`. Para probarlo en FL Studio, cópialo a `C:\Program Files\Common Files\VST3` y vuelve a escanear los plugins.

Solo `dsp/` y `core/` con sus tests (rápido, sin JUCE):

```
cmake -S . -B build-core -DESCEQ_BUILD_PLUGIN=OFF
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

## Reglas para cualquier cambio

1. **Respeta la dirección de las dependencias.** `dsp/` y `core/` no incluyen nada de JUCE. Ver [docs/arquitectura.md](docs/arquitectura.md).
2. **No rompas los contratos públicos:** identificadores y rangos de parámetros, formato del estado, identidad del plugin. Si un test de contrato falla, el cambio es incompatible: no edites el test para que pase.
3. **Los fixtures de `tests/fixtures/` no se modifican ni se borran.** Solo se añaden.
4. **Cada bug arreglado deja un test** que lo reproduce.
5. **El hilo de audio no asigna memoria, no bloquea y no hace E/S.**
6. **Documenta en las cabeceras públicas** qué garantiza cada función: si es segura en tiempo real, si es segura entre hilos, unidades y rangos.
7. **Actualiza la documentación en el mismo commit:** `docs/arquitectura.md` si cambia la estructura, una nota nueva en `docs/decisiones/` si tomas una decisión de diseño, y `CHANGELOG.md` si el cambio lo nota el usuario.
8. **El CI debe estar en verde.** Incluye tests y `pluginval` al nivel 10 en Windows, macOS y Linux.

## Estilo

- C++20, formateado con `clang-format` (configuración en la raíz).
- Los avisos del compilador son errores en nuestro código.
- Cada archivo empieza con su `SPDX-License-Identifier`: `MIT` en `dsp/` y sus tests, `AGPL-3.0-or-later` en el resto.

## Git

Una sola rama, `main`. Commits pequeños, con un mensaje que diga qué cambia y por qué.
