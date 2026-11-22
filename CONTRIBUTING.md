# Cómo trabajar en esceQ

## Compilar

Requisitos: CMake ≥ 3.24 y un compilador con C++20 (Visual Studio 2022 o 2026, Clang 15+, GCC 12+).

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

El VST3 queda en `build/plugin/esceQ_artefacts/Release/VST3/esceQ.vst3` y el CLAP en `.../Release/CLAP/esceQ.clap`. Para probarlo en FL Studio, copia el VST3 a `C:\Program Files\Common Files\VST3` y vuelve a escanear los plugins.

La compilación completa incluye dos grupos de tests: `esceq_tests` (`dsp/`, `core/` y `ui/logic`, sin JUCE) y `esceq_plugin_tests` (la capa `plugin/`: `processBlock` con buses y sidechain, estado, deshacer y presets, con JUCE). `ctest --test-dir build` ejecuta los dos.

Solo `dsp/`, `core/` y `ui/logic` con sus tests (rápido, sin JUCE):

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
5. **El hilo de audio no asigna memoria, no bloquea y no hace E/S.** Las funciones de `dsp/` que se llaman desde él llevan la marca `ESCEQ_NONBLOCKING`, y el CI las comprueba con RealtimeSanitizer.
6. **Documenta en las cabeceras públicas** qué garantiza cada función: si es segura en tiempo real, si es segura entre hilos, unidades y rangos.
7. **Actualiza la documentación en el mismo commit:** `docs/arquitectura.md` si cambia la estructura, una nota nueva en `docs/decisiones/` si tomas una decisión de diseño, y `CHANGELOG.md` si el cambio lo nota el usuario.
8. **El CI debe estar en verde.** Incluye tests y `pluginval` al nivel 10 en Windows, macOS y Linux, y los tests con RealtimeSanitizer.

## Revisar la interfaz sin host

`tools/snapshot` abre el editor con unas bandas de ejemplo y ruido rosa, y guarda una captura en PNG:

```
cmake -S . -B build -DESCEQ_BUILD_TOOLS=ON
cmake --build build --target esceq_snapshot
./build/tools/snapshot/esceq_snapshot_artefacts/Release/esceq_snapshot captura.png
```

En Linux sin pantalla, anteponer `xvfb-run -a`. Opciones: `ancho alto`, `--scale F` (1, 1.25, 1.5, 2), `--select N`, `--hover N` (etiqueta flotante de esa banda), `--overflow`, `--bypass`, `--auto-gain`, `--auto-timing` (tiempos automáticos en la banda dinámica), `--notes` (con el teclado de la escala de notas), `--key N` (con la tecla de la nota N, en MIDI, señalada), `--expanded` (en la vista ampliada, como al pulsar F en la curva), `--peaks` (con los picos del analizador mantenidos), `--peak HZ` (con una resonancia en HZ en el audio y el ratón junto a ella: el pico marcado de las bandas desde el espectro), `--gallery` (estados de los controles), `--compare` (dos instancias más en la sesión, "Bass" y "Keys", superpuestas y con las colisiones marcadas) y `--match` (el panel Match abierto, tras aprender 10 s de otra instancia, "Reference", con la propuesta calculada; imprime las bandas propuestas).

Para medir el coste de pintar la curva, `--bench N` pone las 24 bandas dinámicas y pinta N fotogramas de arrastre (por ejemplo `esceq_snapshot x.png 1480 860 --scale 1 --bench 60`). Imprime la media y la mediana, sin audio y con audio, y una huella de los píxeles de un fotograma fijo: si una optimización no debe cambiar lo que se ve, la huella tiene que salir igual antes y después.

Cuando cambie el aspecto de la interfaz, regenera las imágenes de la documentación y míralas:

```
S=./build/tools/snapshot/esceq_snapshot_artefacts/Release/esceq_snapshot
$S docs/img/esceq.png --hover 6 --auto-gain --auto-timing
$S docs/img/ui/notas.png 1120 580 --select 2 --hover 2 --key 57
$S docs/img/ui/ampliada.png 1480 860 --expanded --peaks --hover 6
$S docs/img/ui/controles.png --gallery
$S docs/img/ui/fuera-de-rango.png 1120 580 --overflow --hover 3 --select 3
$S docs/img/ui/sin-seleccion.png 1120 580 --select 0
$S docs/img/ui/pico.png 1120 580 --select 0 --peak 1300 --notes
$S docs/img/ui/comparar.png 1180 700 --compare --select 0
$S docs/img/ui/igualar.png 1180 700 --match --select 0
```

Los menús y los diálogos son ventanas aparte y no salen en esas capturas. Para verlos, `--desktop presets|edit|save SEGUNDOS` deja el editor en pantalla con eso abierto mientras otra herramienta captura la pantalla (el ejemplo está en la cabecera de `tools/snapshot/main.cpp`).

## Medir el coste del hilo de audio

`tools/bench` mide cuánto tarda `dsp::PhaseEqualizer` con seis escenarios fijos (24 campanas, 24 cortes de 96 dB/oct, 24 campanas dinámicas comprimiendo, una mezcla de 8 bandas, 24 campanas espectrales y la mezcla con sus 2 bandas dinámicas espectrales) en los cuatro modos de fase, en % de un núcleo para una instancia estéreo, y lo que tarda la llamada más lenta frente a lo que dura el bloque (con ruido del sistema: repite y da rangos). Solo depende de `dsp/`, así que también se compila sin JUCE:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DESCEQ_BUILD_TOOLS=ON
cmake --build build --config Release --target esceq_bench
./build/tools/bench/esceq_bench --rate 44100,48000,96000,192000 --block 32,256,1024
./build/tools/bench/esceq_bench --convolution
./build/tools/bench/esceq_bench --match
./build/tools/bench/esceq_bench --spectral
```

Opciones: `--scenario`, `--mode`, `--rate` y `--block` (listas separadas por comas), `--algorithm classic|matched`, `--seconds`, `--repeat`; `--convolution` mide en vez del ecualizador el pico de la fase lineal, la llamada que completa un bloque de la convolución, `--match`, el ajuste de la igualación con 24 bandas y con 4 ([docs/dsp/igualacion.md](docs/dsp/igualacion.md)), y `--spectral`, el motor de la dinámica espectral solo, con una banda, con 24 y con 24 en reposo, en % de un núcleo y en ms por trama ([docs/dsp/dinamica-espectral.md](docs/dsp/dinamica-espectral.md)). Las medidas solo valen en Release y comparadas en la misma máquina; antes de optimizar algo del hilo de audio, mídelo con esto antes y después y deja las cifras en la ADR. Las de la decisión sobre SIMD están en la [ADR 0020](docs/decisiones/0020-sin-simd.md).

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

## Publicar una versión

1. Comprobar que el CI de `main` está en verde y probar el plugin en FL Studio.
2. Poner la versión en `project(esceQ VERSION X.Y.Z ...)` de `CMakeLists.txt`.
3. En `CHANGELOG.md`, convertir "Sin publicar" en `## [X.Y.Z] - AAAA-MM-DD` y dejar una sección "Sin publicar" vacía encima. Ese texto es el de la release: `tools/release-notes.sh X.Y.Z` lo imprime.
4. Si la versión cambia parámetros, estado, presets o sonido, añadir sus archivos `release_X.Y*` en `tests/fixtures/` y sus comprobaciones en `tests/core/release_contract_test.cpp`. Los de versiones anteriores no se tocan.
5. Hacer commit, push y esperar al CI.
6. Subir la etiqueta: `git tag vX.Y.Z && git push origin vX.Y.Z`. El workflow `Release` compila, valida, empaqueta y publica. Si la etiqueta no coincide con la versión de `CMakeLists.txt`, o el CHANGELOG no tiene esa sección, falla antes de compilar.

Para ensayar el proceso sin publicar: pestaña **Actions** de GitHub → **Release** → **Run workflow**.

## Git

Una sola rama, `main`. Commits pequeños, con un mensaje que diga qué cambia y por qué.
