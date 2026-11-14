# Arquitectura

Este documento describe cómo está organizado esceQ y qué reglas mantienen los cambios bajo control. **Se actualiza en el mismo commit que cambie algo de lo que cuenta.**

## Módulos

```
ui/src  ──►  plugin  ──►  core  ──►  dsp
   │                       ▲
   └──►  ui/logic  ────────┘
```

Las flechas indican "depende de". Nunca al revés.

| Módulo | Qué contiene | Puede depender de | Licencia |
|---|---|---|---|
| `dsp/` | Algoritmos de audio y análisis. C++20 puro | Solo la biblioteca estándar | MIT |
| `core/` | Tabla de parámetros, formato del estado guardado y traducción de parámetros a ajustes del motor. C++20 puro | `dsp/` | AGPLv3 |
| `ui/logic/` | Lógica de la interfaz: geometría de la curva, comportamiento de los controles, medidores y disposición de la ventana. C++20 puro | `core/`, `dsp/` | AGPLv3 |
| `plugin/` | Adaptador con el host (JUCE): buses, parámetros, estado, colas del analizador | `core/`, `dsp/`, JUCE | AGPLv3 |
| `ui/src/` | Capa gráfica (JUCE): tema, dibujo, controles propios, curva, panel de banda, medidor y editor. Ver [ui.md](ui.md) | `plugin/`, `ui/logic/`, `core/`, JUCE | AGPLv3 |
| `ui/assets/` | Fuentes embebidas (Inter) | — | SIL OFL 1.1 |
| `tests/` | Tests de `dsp/`, `core/` y `ui/logic/` | Esos tres, Catch2 | La del código que prueban |
| `tests/plugin/` | Tests de `plugin/` (`processBlock` con buses y sidechain, estado, deshacer, presets). Usan JUCE, sin la interfaz | `plugin/`, Catch2, JUCE | AGPLv3 |
| `tools/` | Herramientas de desarrollo (captura del editor, con `-DESCEQ_BUILD_TOOLS=ON`; notas de la release) | `plugin/`, `ui/src/` | AGPLv3 |
| `packaging/` | Instalador de Windows (Inno Setup) e instrucciones de los paquetes de macOS y Linux | — | AGPLv3 |

`ui/src/` no es una biblioteca: sus fuentes se compilan dentro del objetivo del plugin, porque los módulos de JUCE solo pueden compilarse una vez por binario.

Cómo se hace cumplir:

- `dsp/`, `core/` y `ui/logic/` solo ven sus propias rutas de inclusión y las de lo que tienen debajo, así que un `#include` hacia arriba no compila.
- El job `core` del CI compila y testea los tres con `-DESCEQ_BUILD_PLUGIN=OFF`, es decir, sin JUCE descargado.
- En esos tres módulos los avisos del compilador son errores (`/W4 /WX` en MSVC; `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror` en GCC y Clang).

## Contratos públicos

Son lo que no se puede cambiar sin romper proyectos que los usuarios ya han guardado. Cada uno tiene un test o una nota que lo protege.

| Contrato | Dónde vive | Qué lo protege |
|---|---|---|
| Identificadores, tipos, escalas, rangos, número de opciones y orden de los parámetros | `core/src/parameters.cpp` | `tests/fixtures/parameters/v1.txt` y `tests/core/parameters_test.cpp` · [ADR 0004](decisiones/0004-parametros-congelados.md), [ADR 0005](decisiones/0005-bandas-y-normalizacion.md), [ADR 0006](decisiones/0006-dinamica-por-banda.md), [ADR 0007](decisiones/0007-canal-por-banda.md) |
| Correspondencia valor ↔ posición 0..1 del host (automatización) | `core::toNormalized` / `fromNormalized` | `tests/core/parameters_test.cpp` · [ADR 0005](decisiones/0005-bandas-y-normalizacion.md) |
| Orden de los tipos de filtro (índice guardado en el estado) | `core::kFilterTypeChoices` y `dsp::FilterType` | `static_assert` en `core/src/band_model.cpp` |
| Orden de los canales de banda (índice guardado en el estado) | `core::kChannelChoices` y `dsp::ChannelMode` | `static_assert` en `core/src/band_model.cpp` · [ADR 0007](decisiones/0007-canal-por-banda.md) |
| Formato del estado guardado (versiones 1 y 2) | `core/src/state.cpp` | Fixtures en `tests/fixtures/state/` · [ADR 0003](decisiones/0003-formato-de-estado.md), [ADR 0009](decisiones/0009-preferencias-deshacer-y-presets.md) |
| Nombre, orden y sonido de los presets de fábrica | `core/src/presets.cpp` | `tests/core/presets_test.cpp` · [ADR 0009](decisiones/0009-preferencias-deshacer-y-presets.md) |
| Identidad del plugin ante el host: `PLUGIN_MANUFACTURER_CODE` (`Escq`), `PLUGIN_CODE` (`EscQ`), `BUNDLE_ID`, `CLAP_ID` (`org.esceq.esceq`), nombre | `plugin/CMakeLists.txt` | Revisión manual: no tocar |
| Identidad de la instalación en Windows: `AppId` | `packaging/windows/esceQ.iss` | Revisión manual: si cambia, una versión nueva no actualiza la anterior |
| Lo que publicó cada versión: parámetros, estado, presets y sonido de un proyecto de referencia | `tests/fixtures/*/release_0.1*` | `tests/core/release_contract_test.cpp` · [ADR 0010](decisiones/0010-distribucion.md) |
| Buses: entrada y salida principales, y sidechain opcional como segundo bus de entrada | `plugin/src/PluginProcessor.cpp` | Revisión manual: reordenar los buses rompe el enrutado guardado en los proyectos |
| Sonido de una configuración dada | `dsp/` | `tests/fixtures/dsp/respuesta_v1.txt` y `tests/dsp/filter_golden_test.cpp` · [docs/dsp/filtros.md](dsp/filtros.md). La dinámica: `tests/dsp/dynamics_test.cpp` y [docs/dsp/dinamica.md](dsp/dinamica.md). El canal por banda: `tests/dsp/equalizer_channel_test.cpp` |

## Contenido de `dsp/`

| Cabecera | Qué ofrece | Hilo |
|---|---|---|
| `decibels.h` | Conversión entre decibelios y ganancia lineal | Cualquiera |
| `gain_ramp.h` | Ganancia suavizada sin chasquidos | Audio |
| `filter_design.h` | De parámetros de banda a coeficientes, y respuesta en frecuencia exacta para dibujar la curva | Cualquiera |
| `band_filter.h` | Procesado de audio de una banda (cascada de secciones SVF) | Audio |
| `dynamics.h` | Curva de ganancia, seguidor de envolvente y filtro del detector de la dinámica | Audio (la curva y el detector, cualquiera) |
| `equalizer.h` | Las 24 bandas en serie, con suavizado de parámetros, fundidos al cambiar de tipo o activar, bypass con fundido, dinámica por banda con sidechain opcional, y canal por banda (Stereo, Left, Right, Mid, Side) | Audio |
| `spsc_fifo.h` | Cola sin bloqueo de un productor y un consumidor | Audio escribe, interfaz lee |
| `fft.h` | FFT radix-2 | Cualquiera |
| `spectrum_analyzer.h` | Espectro calibrado para dibujar | Interfaz |

El diseño (`designBand`) y el procesado (`BandFilter`) están separados a propósito: la interfaz puede calcular la curva sin procesar audio, y el filtro no sabe nada de tipos ni de parámetros de usuario. Un tipo de filtro nuevo se añade en `designBand` sin tocar `BandFilter`. Detalles en [docs/dsp/filtros.md](dsp/filtros.md), [docs/dsp/ecualizador.md](dsp/ecualizador.md) y [docs/dsp/dinamica.md](dsp/dinamica.md).

## Contenido de `core/`

| Cabecera | Qué ofrece |
|---|---|
| `parameters.h` | La tabla de parámetros, su normalización para el host y su formato como texto |
| `state.h` | Lectura y escritura del estado guardado: parámetros y preferencias de la interfaz (`UiSettings`), con migraciones entre versiones |
| `history.h` | Historial de deshacer y rehacer por instantáneas (`UndoHistory`) |
| `presets.h` | Presets de fábrica, texto de un preset y nombres de preset válidos como nombre de archivo |
| `band_model.h` | De los valores de los parámetros a `dsp::BandSettings`, dinámica incluida. El procesador y la interfaz usan la misma función, así que **la curva que se dibuja es siempre la que suena** (la interfaz le suma la ganancia dinámica que publica el procesador) |

## Contenido de `plugin/`

| Archivo | Qué hace |
|---|---|
| `PluginProcessor` | Buses, parámetros del host generados a partir de la tabla de `core`, `processBlock`, estado, colas del analizador y picos de salida |
| `EditHistory` | Deshacer y rehacer: convierte los gestos de los parámetros en acciones de `core::UndoHistory` |
| `PresetManager` | Presets de fábrica y de usuario (archivos en una carpeta del sistema) |
| `PluginEditorFactory.cpp` | Único punto en el que el procesador conoce a la interfaz (`createEditor`) |

## Contenido de `ui/logic/`

| Cabecera | Qué ofrece |
|---|---|
| `curve_mapping.h` | Frecuencia y ganancia ↔ píxeles, posición y selección de nodos, rejillas, etiqueta flotante, indicador de ganancia fuera de rango |
| `control_logic.h` | De arrastre, rueda y teclas a valor; ajuste fino; arco de valor; edición del valor como texto |
| `meter_logic.h` | Escala y balística del medidor de salida; fracción del medidor de dinámica |
| `layout.h` | Tamaños de ventana (S, M, L y límites) y posición de cada zona y control |

Nada de esto depende de JUCE ni de una ventana: se testea en `tests/ui/`. `ui/src` solo pinta y traduce gestos ([ADR 0008](decisiones/0008-sistema-visual.md), [ui.md](ui.md)).

## Parámetros: una sola fuente de verdad

`core::parameters()` es la única lista de parámetros, 338 en total, en este orden:

1. 2 globales: `output_gain`, `bypass`.
2. 6 por cada una de las 24 bandas (`bNN_on`, `bNN_type`, `bNN_freq`, `bNN_gain`, `bNN_q`, `bNN_slope`), con índice `core::bandParameter()`.
3. 7 de dinámica por cada banda (`bNN_dyn`, `bNN_thr`, `bNN_range`, `bNN_att`, `bNN_rel`, `bNN_knee`, `bNN_sc`), con índice `core::dynamicParameter()`. Van detrás de todas las bandas porque se añadieron en la fase 3 ([ADR 0006](decisiones/0006-dinamica-por-banda.md)).
4. 1 de canal por cada banda (`bNN_chan`), con índice `core::channelParameter()`. Añadido en la fase 4 ([ADR 0007](decisiones/0007-canal-por-banda.md)).

 `plugin/` genera los parámetros del host recorriéndola (incluidos el texto que muestran y la escala logarítmica de frecuencia y Q), y el estado se serializa recorriéndola. Para añadir un parámetro:

1. Añadirlo **al final** de la tabla en `core/src/parameters.cpp` (y su índice en `parameters.h`).
2. Ejecutar `ESCEQ_APPEND_PARAMETERS=1 ./esceq_tests "Parametros: contrato*"`, que añade **solo las líneas nuevas** al final de `tests/fixtures/parameters/v1.txt`. Las líneas existentes no se editan nunca.
3. Usarlo en `core/band_model` o `plugin/`.

No hace falta tocar el código de estado ni registrar nada en JUCE.

## Estado guardado

Texto UTF-8 con formato propio (no el de JUCE), versionado. Lleva los parámetros y, desde la versión 2, las preferencias de la interfaz (claves `ui.*`, que no son parámetros):

```
esceQ-state 2
output_gain 0
bypass 0
...
ui.analyzer 1
ui.range 18
ui.width 1180
ui.height 700
ui.preset Vocal
```

Reglas de lectura: lo ausente toma el valor por defecto, lo desconocido se ignora, lo inválido se sanea, y una cabecera incorrecta hace que se conserve el estado actual. Los estados de versiones anteriores se migran paso a paso (`migrate1to2`...). Al subir `kStateVersion` se añade una migración en `core/src/state.cpp` y un fixture nuevo; los fixtures antiguos no se tocan. Un preset es este mismo formato con solo parámetros. Ver [ADR 0003](decisiones/0003-formato-de-estado.md) y [ADR 0009](decisiones/0009-preferencias-deshacer-y-presets.md).

## Deshacer, rehacer y presets

- `core::UndoHistory` guarda instantáneas de todos los parámetros. `plugin/src/EditHistory` decide cuándo hay una acción nueva: al cerrarse el último gesto abierto de la interfaz, tras 300 ms sin más gestos. Un arrastre, una banda nueva o una ráfaga de rueda son una acción. La automatización del host no crea acciones. Los gestos se cuentan por parámetro; los que abre el propio deshacer no cuentan, y si la ventana se cierra con un gesto abierto, los controles lo cierran y el editor avisa al historial (`abandonOpenGestures`).
- Cargar un proyecto borra el historial; cargar un preset es una acción más.
- `plugin/src/PresetManager` lista los presets de fábrica (`core::factoryPresets()`) y los de usuario (archivos `.esceq`), y los carga con `PluginProcessor::applyValues`, que avisa al host de cada parámetro que cambia.

## Hilos

| Hilo | Qué hace | Restricciones |
|---|---|---|
| Audio | `processBlock` | No asigna memoria, no bloquea, no hace E/S |
| Mensajes / interfaz | Editor (refresco a 30 Hz), analizador, carga y guardado de estado, historial de deshacer, presets (lectura y escritura de archivos) | Ninguna especial |

Comunicación:

- **Interfaz → audio:** solo mediante parámetros del host. El hilo de audio lee sus valores atómicos una vez por bloque. Los gestos de la curva (arrastrar, rueda, doble clic) van envueltos en `beginChangeGesture`/`endChangeGesture` para que el host los registre como una sola acción de deshacer y de automatización.
- **Preferencias de la interfaz:** viven en el procesador (`uiSettings()`), protegidas con un cerrojo que el hilo de audio nunca toma. El editor las lee al abrirse y cuando el host carga un estado (`stateRevision()`), y las escribe cuando cambian.
- **Audio → interfaz:** dos `dsp::SpscFifo` (audio mono antes y después del EQ). El hilo de audio escribe; si la cola está llena, descarta. El editor abierto es el **único** lector. Si algún host abriera dos editores a la vez del mismo plugin, habría dos lectores: no está soportado.
- **Ganancia dinámica → interfaz:** 24 `std::atomic<float>` que el hilo de audio escribe al final de cada bloque y la interfaz lee a 30 Hz (sin orden entre bandas: basta para dibujar). Igual con un `std::atomic<bool>` que indica si el host tiene conectado el sidechain.
- **Nivel de salida → interfaz:** un `std::atomic<float>` por canal con el pico de salida. El hilo de audio solo lo sube; el medidor lo lee y lo pone a cero 30 veces por segundo. Si coinciden puede perderse el pico de un bloque: es un medidor, no una medida.

Toda función de `dsp/` documenta en su cabecera si es segura en tiempo real y si es segura entre hilos. Las que se llaman desde el hilo de audio llevan además la marca `ESCEQ_NONBLOCKING` (`esceq/dsp/realtime.h`): el job `RealtimeSanitizer` del CI ejecuta todos los tests con Clang y `-fsanitize=realtime`, y aborta si alguna de ellas, o algo que llamen, asigna memoria o bloquea. Un test "canario" comprueba que el sanitizador está actuando.

## Flujo de audio actual (fase 4)

```
entrada ─► cola "pre" ─► dsp::Equalizer (24 bandas en serie) ─► dsp::GainRamp (salida) ─► cola "post" + picos ─► salida
                              ▲ detectores
sidechain (bus opcional) ─────┘
```

Cada bloque, el procesador lee los 338 parámetros, los traduce con `core::bandSettings` y se los pasa al ecualizador, que decide qué suavizar y qué fundir. Las bandas desactivadas no consumen CPU. Cada banda procesa los dos canales, uno solo, la suma (Mid) o la diferencia (Side). Los detectores de las bandas dinámicas escuchan la entrada del ecualizador o, si así se elige, el bus de sidechain, por el mismo canal que la banda. Con bypass, el ecualizador hace un fundido a la señal seca y la ganancia de salida vuelve a 0 dB. Mono y estéreo, con el mismo número de canales de entrada y salida; el sidechain puede estar desconectado, ser mono o estéreo. Latencia cero.

El búfer que entrega el host contiene los canales de todos los buses: el procesador toma el bus principal con `getBusBuffer` y el sidechain como solo lectura.

## Formatos, CI y publicación

| Formato | Plataformas | Lo genera | Lo valida el CI |
|---|---|---|---|
| VST3 | Windows, macOS, Linux | JUCE | pluginval, nivel 10 |
| AU | macOS | JUCE | — |
| CLAP | Windows, macOS, Linux | clap-juce-extensions | clap-validator |
| Standalone | Todas | JUCE | — (para depurar; no se distribuye) |

El workflow `CI` (`.github/workflows/ci.yml`) tiene tres jobs: `core` (los módulos sin JUCE, en las tres plataformas), `rtsan` (los tests con RealtimeSanitizer) y `plugin` (compila todo, tests, validadores y paquetes; en Windows prueba el instalador). El workflow `Release` (`release.yml`) lo reutiliza al subir una etiqueta `vX.Y.Z` y publica los paquetes. Ver [ADR 0010](decisiones/0010-distribucion.md).

## Dependencias externas

Se fijan por versión en CMake y se descargan con `FetchContent`.

| Dependencia | Versión | Uso | Licencia |
|---|---|---|---|
| JUCE | 9.0.5 | `plugin/`, `ui/src/`, `tools/` | AGPLv3 |
| clap-juce-extensions | commit `6bde108` (incluye CLAP 1.2.7) | Formato CLAP del plugin | MIT |
| Catch2 | 3.16.1 | `tests/` | BSL-1.0 |

En el CI, además: pluginval (última versión) y clap-validator 0.3.2 para validar los formatos, e Inno Setup 6 para el instalador de Windows.

Además, la tipografía Inter 4.1 (SIL OFL 1.1) va copiada en `ui/assets/fonts/`, sin modificar y con su licencia, y se embebe en el binario.
