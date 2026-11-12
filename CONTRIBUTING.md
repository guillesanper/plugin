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
3. **Los fixtures de `tests/fixtures/` no se modifican ni se borran.** Solo se añaden. Para añadir parámetros al contrato usa `ESCEQ_APPEND_PARAMETERS=1` (ver [docs/arquitectura.md](docs/arquitectura.md)), que solo escribe líneas nuevas al final.
4. **Cada bug arreglado deja un test** que lo reproduce.
5. **El hilo de audio no asigna memoria, no bloquea y no hace E/S.**
6. **Documenta en las cabeceras públicas** qué garantiza cada función: si es segura en tiempo real, si es segura entre hilos, unidades y rangos.
7. **Actualiza la documentación en el mismo commit:** `docs/arquitectura.md` si cambia la estructura, una nota nueva en `docs/decisiones/` si tomas una decisión de diseño, y `CHANGELOG.md` si el cambio lo nota el usuario.
8. **El CI debe estar en verde.** Incluye tests y `pluginval` al nivel 10 en Windows, macOS y Linux.

## Revisar la interfaz sin host

`tools/snapshot` abre el editor con unas bandas de ejemplo y ruido rosa, y guarda una captura en PNG:

```
cmake -S . -B build -DESCEQ_BUILD_TOOLS=ON
cmake --build build --target esceq_snapshot
./build/tools/snapshot/esceq_snapshot_artefacts/Release/esceq_snapshot captura.png
```

En Linux sin pantalla, anteponer `xvfb-run -a`. Opciones: `ancho alto`, `--scale F` (1, 1.25, 1.5, 2), `--select N`, `--hover N` (etiqueta flotante de esa banda), `--overflow`, `--bypass` y `--gallery` (estados de los controles).

Cuando cambie el aspecto de la interfaz, regenera las imágenes de la documentación y míralas:

```
S=./build/tools/snapshot/esceq_snapshot_artefacts/Release/esceq_snapshot
$S docs/img/esceq.png --hover 6
$S docs/img/ui/controles.png --gallery
$S docs/img/ui/fuera-de-rango.png 960 580 --overflow --hover 3 --select 3
$S docs/img/ui/sin-seleccion.png 960 580 --select 0
```

## Reglas de la interfaz

Ver [docs/ui.md](docs/ui.md). En corto:

- Colores, letra y medidas de estilo salen de `ui/src/Theme.h`. No se escriben a mano en los componentes.
- Lo que se pueda calcular sin ventana (de gesto a valor, escalas, posiciones) va en `ui/logic` con su test.
- Un control nuevo tiene nombre accesible, se maneja con el teclado y dibuja sus seis estados.
- Los textos visibles van en inglés. Nada de nombres, logos ni aspecto de productos comerciales.

## Estilo

- C++20, formateado con `clang-format` (configuración en la raíz).
- Los avisos del compilador son errores en nuestro código.
- Los nombres de los tests (`TEST_CASE`) van en ASCII, sin tildes ni eñes: en Windows CTest no encuentra los que las llevan.
- Cada archivo empieza con su `SPDX-License-Identifier`: `MIT` en `dsp/` y sus tests, `AGPL-3.0-or-later` en el resto. Las fuentes de `ui/assets/fonts/` son de terceros (SIL OFL 1.1) y no se modifican.

## Git

Una sola rama, `main`. Commits pequeños, con un mensaje que diga qué cambia y por qué.
