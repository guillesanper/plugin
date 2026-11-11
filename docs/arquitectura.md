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
| `ui/logic/` | Geometría de la interfaz: escalas, posición de nodos, detección de clics. C++20 puro | `core/`, `dsp/` | AGPLv3 |
| `plugin/` | Adaptador con el host (JUCE): buses, parámetros, estado, colas del analizador | `core/`, `dsp/`, JUCE | AGPLv3 |
| `ui/src/` | Componentes gráficos (JUCE): curva, panel de banda, editor | `plugin/`, `ui/logic/`, `core/`, JUCE | AGPLv3 |
| `tests/` | Tests de `dsp/`, `core/` y `ui/logic/` | Esos tres, Catch2 | La del código que prueban |
| `tools/` | Herramientas de desarrollo (captura del editor). Solo con `-DESCEQ_BUILD_TOOLS=ON` | `plugin/`, `ui/src/` | AGPLv3 |

`ui/src/` no es una biblioteca: sus fuentes se compilan dentro del objetivo del plugin, porque los módulos de JUCE solo pueden compilarse una vez por binario.

Cómo se hace cumplir:

- `dsp/`, `core/` y `ui/logic/` solo ven sus propias rutas de inclusión y las de lo que tienen debajo, así que un `#include` hacia arriba no compila.
- El job `core` del CI compila y testea los tres con `-DESCEQ_BUILD_PLUGIN=OFF`, es decir, sin JUCE descargado.

## Contratos públicos

Son lo que no se puede cambiar sin romper proyectos que los usuarios ya han guardado. Cada uno tiene un test o una nota que lo protege.

| Contrato | Dónde vive | Qué lo protege |
|---|---|---|
| Identificadores, tipos, escalas, rangos, número de opciones y orden de los parámetros | `core/src/parameters.cpp` | `tests/fixtures/parameters/v1.txt` y `tests/core/parameters_test.cpp` · [ADR 0004](decisiones/0004-parametros-congelados.md), [ADR 0005](decisiones/0005-bandas-y-normalizacion.md), [ADR 0006](decisiones/0006-dinamica-por-banda.md) |
| Correspondencia valor ↔ posición 0..1 del host (automatización) | `core::toNormalized` / `fromNormalized` | `tests/core/parameters_test.cpp` · [ADR 0005](decisiones/0005-bandas-y-normalizacion.md) |
| Orden de los tipos de filtro (índice guardado en el estado) | `core::kFilterTypeChoices` y `dsp::FilterType` | `static_assert` en `core/src/band_model.cpp` |
| Formato del estado guardado | `core/src/state.cpp` | Fixtures en `tests/fixtures/state/` · [ADR 0003](decisiones/0003-formato-de-estado.md) |
| Identidad del plugin ante el host: `PLUGIN_MANUFACTURER_CODE` (`Escq`), `PLUGIN_CODE` (`EscQ`), `BUNDLE_ID`, nombre | `plugin/CMakeLists.txt` | Revisión manual: no tocar |
| Buses: entrada y salida principales, y sidechain opcional como segundo bus de entrada | `plugin/src/PluginProcessor.cpp` | Revisión manual: reordenar los buses rompe el enrutado guardado en los proyectos |
| Sonido de una configuración dada | `dsp/` | `tests/fixtures/dsp/respuesta_v1.txt` y `tests/dsp/filter_golden_test.cpp` · [docs/dsp/filtros.md](dsp/filtros.md). La dinámica: `tests/dsp/dynamics_test.cpp` y [docs/dsp/dinamica.md](dsp/dinamica.md) |

## Contenido de `dsp/`

| Cabecera | Qué ofrece | Hilo |
|---|---|---|
| `decibels.h` | Conversión entre decibelios y ganancia lineal | Cualquiera |
| `gain_ramp.h` | Ganancia suavizada sin chasquidos | Audio |
| `filter_design.h` | De parámetros de banda a coeficientes, y respuesta en frecuencia exacta para dibujar la curva | Cualquiera |
| `band_filter.h` | Procesado de audio de una banda (cascada de secciones SVF) | Audio |
| `dynamics.h` | Curva de ganancia, seguidor de envolvente y filtro del detector de la dinámica | Audio (la curva y el detector, cualquiera) |
| `equalizer.h` | Las 24 bandas en serie, con suavizado de parámetros, fundidos al cambiar de tipo o activar, bypass con fundido, y dinámica por banda con sidechain opcional | Audio |
| `spsc_fifo.h` | Cola sin bloqueo de un productor y un consumidor | Audio escribe, interfaz lee |
| `fft.h` | FFT radix-2 | Cualquiera |
| `spectrum_analyzer.h` | Espectro calibrado para dibujar | Interfaz |

El diseño (`designBand`) y el procesado (`BandFilter`) están separados a propósito: la interfaz puede calcular la curva sin procesar audio, y el filtro no sabe nada de tipos ni de parámetros de usuario. Un tipo de filtro nuevo se añade en `designBand` sin tocar `BandFilter`. Detalles en [docs/dsp/filtros.md](dsp/filtros.md), [docs/dsp/ecualizador.md](dsp/ecualizador.md) y [docs/dsp/dinamica.md](dsp/dinamica.md).

## Contenido de `core/`

| Cabecera | Qué ofrece |
|---|---|
| `parameters.h` | La tabla de parámetros, su normalización para el host y su formato como texto |
| `state.h` | Lectura y escritura del estado guardado |
| `band_model.h` | De los valores de los parámetros a `dsp::BandSettings`, dinámica incluida. El procesador y la interfaz usan la misma función, así que **la curva que se dibuja es siempre la que suena** (la interfaz le suma la ganancia dinámica que publica el procesador) |

## Parámetros: una sola fuente de verdad

`core::parameters()` es la única lista de parámetros, 314 en total, en este orden:

1. 2 globales: `output_gain`, `bypass`.
2. 6 por cada una de las 24 bandas (`bNN_on`, `bNN_type`, `bNN_freq`, `bNN_gain`, `bNN_q`, `bNN_slope`), con índice `core::bandParameter()`.
3. 7 de dinámica por cada banda (`bNN_dyn`, `bNN_thr`, `bNN_range`, `bNN_att`, `bNN_rel`, `bNN_knee`, `bNN_sc`), con índice `core::dynamicParameter()`. Van detrás de todas las bandas porque se añadieron en la fase 3 ([ADR 0006](decisiones/0006-dinamica-por-banda.md)).

 `plugin/` genera los parámetros del host recorriéndola (incluidos el texto que muestran y la escala logarítmica de frecuencia y Q), y el estado se serializa recorriéndola. Para añadir un parámetro:

1. Añadirlo **al final** de la tabla en `core/src/parameters.cpp` (y su índice en `parameters.h`).
2. Ejecutar `ESCEQ_APPEND_PARAMETERS=1 ./esceq_tests "Parametros: contrato*"`, que añade **solo las líneas nuevas** al final de `tests/fixtures/parameters/v1.txt`. Las líneas existentes no se editan nunca.
3. Usarlo en `core/band_model` o `plugin/`.

No hace falta tocar el código de estado ni registrar nada en JUCE.

## Estado guardado

Texto UTF-8 con formato propio (no el de JUCE), versionado:

```
esceQ-state 1
output_gain 0
bypass 0
```

Reglas de lectura: lo ausente toma el valor por defecto, lo desconocido se ignora, lo inválido se sanea, y una cabecera incorrecta hace que se conserve el estado actual. Al subir `kStateVersion` se añade una migración en `deserializeState` y un fixture nuevo; los fixtures antiguos no se tocan.

## Hilos

| Hilo | Qué hace | Restricciones |
|---|---|---|
| Audio | `processBlock` | No asigna memoria, no bloquea, no hace E/S |
| Mensajes / interfaz | Editor (refresco a 30 Hz), analizador, carga y guardado de estado | Ninguna especial |

Comunicación:

- **Interfaz → audio:** solo mediante parámetros del host. El hilo de audio lee sus valores atómicos una vez por bloque. Los gestos de la curva (arrastrar, rueda, doble clic) van envueltos en `beginChangeGesture`/`endChangeGesture` para que el host los registre como una sola acción de deshacer y de automatización.
- **Audio → interfaz:** dos `dsp::SpscFifo` (audio mono antes y después del EQ). El hilo de audio escribe; si la cola está llena, descarta. El editor abierto es el **único** lector. Si algún host abriera dos editores a la vez del mismo plugin, habría dos lectores: no está soportado.
- **Ganancia dinámica → interfaz:** 24 `std::atomic<float>` que el hilo de audio escribe al final de cada bloque y la interfaz lee a 30 Hz (sin orden entre bandas: basta para dibujar). Igual con un `std::atomic<bool>` que indica si el host tiene conectado el sidechain.

Toda función de `dsp/` documenta en su cabecera si es segura en tiempo real y si es segura entre hilos.

## Flujo de audio actual (fase 3)

```
entrada ─► cola "pre" ─► dsp::Equalizer (24 bandas en serie) ─► dsp::GainRamp (salida) ─► cola "post" ─► salida
                              ▲ detectores
sidechain (bus opcional) ─────┘
```

Cada bloque, el procesador lee los 314 parámetros, los traduce con `core::bandSettings` y se los pasa al ecualizador, que decide qué suavizar y qué fundir. Las bandas desactivadas no consumen CPU. Los detectores de las bandas dinámicas escuchan la entrada del ecualizador o, si así se elige, el bus de sidechain. Con bypass, el ecualizador hace un fundido a la señal seca y la ganancia de salida vuelve a 0 dB. Mono y estéreo, con el mismo número de canales de entrada y salida; el sidechain puede estar desconectado, ser mono o estéreo. Latencia cero.

El búfer que entrega el host contiene los canales de todos los buses: el procesador toma el bus principal con `getBusBuffer` y el sidechain como solo lectura.

## Dependencias externas

Se fijan por versión en CMake y se descargan con `FetchContent`.

| Dependencia | Versión | Uso | Licencia |
|---|---|---|---|
| JUCE | 9.0.5 | `plugin/`, `ui/src/`, `tools/` | AGPLv3 |
| Catch2 | 3.16.1 | `tests/` | BSL-1.0 |
