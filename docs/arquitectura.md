# Arquitectura

Este documento describe cómo está organizado esceQ y qué reglas mantienen los cambios bajo control. **Se actualiza en el mismo commit que cambie algo de lo que cuenta.**

## Módulos

```
ui  ──►  plugin  ──►  core
              └────►  dsp
```

Las flechas indican "depende de". Nunca al revés.

| Módulo | Qué contiene | Puede depender de | Licencia |
|---|---|---|---|
| `dsp/` | Algoritmos de audio. C++20 puro | Solo la biblioteca estándar | MIT |
| `core/` | Tabla de parámetros y formato del estado guardado. C++20 puro | Biblioteca estándar y `dsp/` | AGPLv3 |
| `plugin/` | Adaptador con el host (JUCE): buses, parámetros, estado | `core/`, `dsp/`, JUCE | AGPLv3 |
| `ui/` | Interfaz gráfica. **Aún no existe** (fase 2); de momento se usa el editor genérico de JUCE | `plugin/`, `core/`, JUCE | AGPLv3 |
| `tests/` | Tests de `dsp/` y `core/` | `dsp/`, `core/`, Catch2 | La del código que prueban |

Cómo se hace cumplir:

- `dsp/` y `core/` solo ven sus propias rutas de inclusión, así que un `#include` hacia arriba no compila.
- El job `core` del CI compila y testea ambos con `-DESCEQ_BUILD_PLUGIN=OFF`, es decir, sin JUCE descargado.

## Contratos públicos

Son lo que no se puede cambiar sin romper proyectos que los usuarios ya han guardado. Cada uno tiene un test o una nota que lo protege.

| Contrato | Dónde vive | Qué lo protege |
|---|---|---|
| Identificadores, tipos, rangos y orden de los parámetros | `core/include/esceq/core/parameters.h` | `tests/core/parameters_test.cpp` · [ADR 0004](decisiones/0004-parametros-congelados.md) |
| Formato del estado guardado | `core/src/state.cpp` | Fixtures en `tests/fixtures/state/` · [ADR 0003](decisiones/0003-formato-de-estado.md) |
| Identidad del plugin ante el host: `PLUGIN_MANUFACTURER_CODE` (`Escq`), `PLUGIN_CODE` (`EscQ`), `BUNDLE_ID`, nombre | `plugin/CMakeLists.txt` | Revisión manual: no tocar |
| Sonido de una configuración dada | `dsp/` | `tests/fixtures/dsp/respuesta_v1.txt` y `tests/dsp/filter_golden_test.cpp` · [docs/dsp/filtros.md](dsp/filtros.md) |

## Contenido de `dsp/`

| Cabecera | Qué ofrece |
|---|---|
| `decibels.h` | Conversión entre decibelios y ganancia lineal |
| `gain_ramp.h` | Ganancia suavizada sin chasquidos |
| `filter_design.h` | De parámetros de banda a coeficientes, y respuesta en frecuencia exacta para dibujar la curva |
| `band_filter.h` | Procesado de audio de una banda (cascada de secciones SVF) |

El diseño (`designBand`) y el procesado (`BandFilter`) están separados a propósito: la interfaz puede calcular la curva sin procesar audio, y el filtro no sabe nada de tipos ni de parámetros de usuario. Un tipo de filtro nuevo se añade en `designBand` sin tocar `BandFilter`. Detalles en [docs/dsp/filtros.md](dsp/filtros.md).

Los filtros todavía no están conectados al plugin: eso es la fase 2.

## Parámetros: una sola fuente de verdad

`core::kParameters` es la única lista de parámetros. `plugin/` genera los parámetros del host recorriéndola, y el estado se serializa recorriéndola. Para añadir un parámetro:

1. Añadir la fila **al final** de `kParameters` y su entrada en `ParameterIndex`.
2. Añadir la fila al contrato congelado de `tests/core/parameters_test.cpp`.
3. Usarlo en `plugin/` o `dsp/`.

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
| Mensajes / interfaz | Editor, carga y guardado de estado | Ninguna especial |

Comunicación:

- **Interfaz → audio:** solo mediante parámetros del host. El hilo de audio lee sus valores atómicos una vez por bloque.
- **Audio → interfaz:** todavía no hay. Cuando llegue el analizador será una cola sin bloqueo de un productor y un consumidor.

Toda función de `dsp/` documenta en su cabecera si es segura en tiempo real y si es segura entre hilos.

## Flujo de audio actual (fase 0)

Entrada → `dsp::GainRamp` (ganancia de salida suavizada) → salida. Con bypass activo la ganancia objetivo es 1. Mono y estéreo, con el mismo número de canales de entrada y salida.

## Dependencias externas

Se fijan por versión en CMake y se descargan con `FetchContent`.

| Dependencia | Versión | Uso | Licencia |
|---|---|---|---|
| JUCE | 9.0.5 | `plugin/` | AGPLv3 |
| Catch2 | 3.16.1 | `tests/` | BSL-1.0 |
