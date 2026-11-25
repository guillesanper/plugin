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
| `ui/src/` | Capa gráfica (JUCE): tema, dibujo, controles propios, curva, panel de banda, cajón de la etapa de salida, medidor y editor. Ver [ui.md](ui.md) | `plugin/`, `ui/logic/`, `core/`, JUCE | AGPLv3 |
| `ui/assets/` | Fuentes embebidas (Inter) | — | SIL OFL 1.1 |
| `tests/` | Tests de `dsp/`, `core/` y `ui/logic/` | Esos tres, Catch2 | La del código que prueban |
| `tests/plugin/` | Tests de `plugin/` (`processBlock` con buses y sidechain, estado, deshacer, presets, igualación de extremo a extremo). Usan JUCE, sin la interfaz | `plugin/`, Catch2, JUCE | AGPLv3 |
| `tools/` | Herramientas de desarrollo, con `-DESCEQ_BUILD_TOOLS=ON`: captura del editor (`snapshot`) y coste del hilo de audio (`bench`, [ADR 0020](decisiones/0020-sin-simd.md)); notas de la release | `snapshot`: `plugin/`, `ui/src/`; `bench`: solo `dsp/` | AGPLv3 |
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
| Orden de los modos de fase (índice guardado en el estado) | `core::kPhaseModeChoices` y `dsp::PhaseMode` | `static_assert` en `core/src/band_model.cpp` · [ADR 0012](decisiones/0012-modos-de-fase.md) |
| Formato del estado guardado (versiones 1, 2 y 3) | `core/src/state.cpp` | Fixtures en `tests/fixtures/state/` · [ADR 0003](decisiones/0003-formato-de-estado.md), [ADR 0009](decisiones/0009-preferencias-deshacer-y-presets.md), [ADR 0014](decisiones/0014-estado-v3-algoritmo-de-filtro.md) |
| Valores de los algoritmos de filtro (guardados en el estado) | `dsp::FilterAlgorithm` | `tests/core/state_test.cpp`, `tests/dsp/filter_algorithm_test.cpp` · [ADR 0013](decisiones/0013-correccion-en-agudos.md) |
| Nombre, orden y sonido de los presets de fábrica | `core/src/presets.cpp` | `tests/core/presets_test.cpp` · [ADR 0009](decisiones/0009-preferencias-deshacer-y-presets.md) |
| Identidad del plugin ante el host: `PLUGIN_MANUFACTURER_CODE` (`Escq`), `PLUGIN_CODE` (`EscQ`), `BUNDLE_ID`, `CLAP_ID` (`org.esceq.esceq`), nombre | `plugin/CMakeLists.txt` | Revisión manual: no tocar |
| Identidad de la instalación en Windows: `AppId` | `packaging/windows/esceQ.iss` | Revisión manual: si cambia, una versión nueva no actualiza la anterior |
| Lo que publicó cada versión: parámetros, estado, presets y sonido de un proyecto de referencia | `tests/fixtures/*/release_0.1*` y `release_0.5*` | `tests/core/release_contract_test.cpp` · [ADR 0010](decisiones/0010-distribucion.md) |
| Buses: entrada y salida principales, y sidechain opcional como segundo bus de entrada | `plugin/src/PluginProcessor.cpp` | Revisión manual: reordenar los buses rompe el enrutado guardado en los proyectos |
| Sonido de una configuración dada | `dsp/` | `tests/fixtures/dsp/respuesta_v1.txt` (Classic), `respuesta_matched.txt` (Matched) y `tests/dsp/filter_golden_test.cpp`; Classic, además, bit a bit con el código de la 0.1 (`tests/dsp/filter_algorithm_test.cpp`) · [docs/dsp/filtros.md](dsp/filtros.md). La dinámica: `tests/dsp/dynamics_test.cpp` y [docs/dsp/dinamica.md](dsp/dinamica.md). El canal por banda: `tests/dsp/equalizer_channel_test.cpp` |

## Contenido de `dsp/`

| Cabecera | Qué ofrece | Hilo |
|---|---|---|
| `decibels.h` | Conversión entre decibelios y ganancia lineal | Cualquiera |
| `gain_ramp.h` | Ganancia suavizada sin chasquidos | Audio |
| `auto_gain.h` | Compensación automática de ganancia: la ganancia que deja igual la sonoridad de ruido rosa (ponderación K) por la curva estática (`autoGainDb`), esa curva punto a punto con la misma regla de canales (`staticCurveDb`), y su entrega del hilo de control al de audio (`AutoGain`). Ver [docs/dsp/compensacion-de-ganancia.md](dsp/compensacion-de-ganancia.md) | El cálculo, fuera del de audio; la entrega, uno de audio y uno de control |
| `curve_match.h` | Igualar a una referencia: de los espectros medios de la pista y de la referencia a las campanas y shelves que faltan sobre la curva actual (`matchCurve`: objetivo, pesos, inicio voraz y Levenberg-Marquardt), determinista y cancelable ([ADR 0023](decisiones/0023-igualar-a-una-referencia.md), [docs/dsp/igualacion.md](dsp/igualacion.md)) | Fuera del de audio (hasta 1 s con 24 bandas) |
| `filter_design.h` | De parámetros de banda a coeficientes, respuesta en frecuencia exacta para dibujar la curva, y el Q de un ancho en octavas (`qForBandwidth`) | Cualquiera |
| `band_filter.h` | Procesado de audio de una banda (cascada de secciones SVF) | Audio |
| `dynamics.h` | Curva de ganancia, seguidor de envolvente y filtro del detector de la dinámica | Audio (la curva y el detector, cualquiera) |
| `equalizer.h` | Las 24 bandas en serie (IIR), con suavizado de parámetros, fundidos al cambiar de tipo o activar, bypass con fundido, dinámica por banda con sidechain opcional, y canal por banda (Stereo, Left, Right, Mid, Side). En modo "solo dinámica", solo el cambio que añade la dinámica (para la fase lineal) | Audio |
| `linear_phase.h` | Modos de fase (`PhaseMode`), tamaños y latencias (con su escalado con la frecuencia de muestreo, `sampleRateFactor`, que usa también la dinámica espectral), y diseño de FIR de fase lineal a partir de las bandas (`LinearPhaseDesigner`). Ver [docs/dsp/fase-lineal.md](dsp/fase-lineal.md) | Diseño: no tiempo real |
| `partitioned_convolver.h` | Convolución estéreo particionada por FFT (`FirKernel`, `PartitionedConvolver`), con fundido entre núcleos | Audio (el núcleo se construye fuera) |
| `phase_equalizer.h` | El ecualizador completo con sus cuatro modos de fase: IIR (el `Equalizer`) o FIR de fase lineal más la dinámica en IIR, y tras ellos, mientras alguna banda la pida, la dinámica espectral, que sustituye a la dinámica de esas bandas ([ADR 0025](decisiones/0025-dinamica-espectral-en-el-procesador.md)), y al final, fuera de Off, la saturación, con los filtros del modo ([ADR 0026](decisiones/0026-saturacion.md)); bypass con latencia constante y cambio de modo (o de dinámica espectral, o de saturación en fase lineal) con fundido. `serviceDesigns()` es la parte del hilo de control | Audio, y el de control |
| `spectral_dynamics.h` | Dinámica espectral (`SpectralDynamics`, [ADR 0024](decisiones/0024-dinamica-espectral.md), [docs/dsp/dinamica-espectral.md](dsp/dinamica-espectral.md)): una STFT de 2048 puntos a 48 kHz para todas las bandas, detector de 1/6 de octava por bin, envolventes y máscaras L/R y M/S, con el cálculo de cada trama repartido en el salto siguiente y N + N/4 muestras de latencia ([ADR 0025](decisiones/0025-dinamica-espectral-en-el-procesador.md)). La usa `PhaseEqualizer` | Audio (la memoria se reserva en `prepare`) |
| `saturation.h` | Saturación (`Saturation`, [ADR 0026](decisiones/0026-saturacion.md), [docs/dsp/saturacion.md](dsp/saturacion.md)): curvas Soft y Warm, compensación del drive, bloqueador de continua y mezcla con la seca, a unos 384 kHz; y el sobremuestreo (`Oversampler`), en etapas ×2 de media banda IIR (sin latencia) o de fase lineal. La usa `PhaseEqualizer`, con sus ajustes en `SaturationSettings` | Audio (no asigna memoria) |
| `handoff.h` | `TripleBuffer` (el último valor, sin bloqueos) y `Handoff` (entregar objetos al hilo de audio y recogerlos para destruirlos fuera de él) | Uno de audio y uno que no es de tiempo real |
| `spsc_fifo.h` | Cola sin bloqueo de un productor y un consumidor | Audio escribe, interfaz lee |
| `fft.h` | FFT radix-2 en `float` y `double`, directa e inversa | Cualquiera |
| `spectrum_analyzer.h` | Espectro calibrado para dibujar | Interfaz |
| `spectrum_collision.h` | Colisiones (enmascaramiento) entre dos espectros: suavizado por octavas (con un ancho mínimo en Hz opcional, para la igualación), presencia, severidad, tramos y promedio temporal. Ver [docs/dsp/colisiones.md](dsp/colisiones.md) | Cualquiera |

`dsp` no crea hilos: el de control vive en `core` (`ControlThread`) y solo llama a `PhaseEqualizer::serviceDesigns()` y a `AutoGain::service()`. `matchCurve` lo llamará el hilo del ajuste de `core` (bloque 6.2).

El diseño (`designBand`) y el procesado (`BandFilter`) están separados a propósito: la interfaz puede calcular la curva sin procesar audio, y el filtro no sabe nada de tipos ni de parámetros de usuario. Un tipo de filtro nuevo se añade en `designBand` sin tocar `BandFilter`. Detalles en [docs/dsp/filtros.md](dsp/filtros.md), [docs/dsp/ecualizador.md](dsp/ecualizador.md) y [docs/dsp/dinamica.md](dsp/dinamica.md).

## Contenido de `core/`

| Cabecera | Qué ofrece |
|---|---|
| `parameters.h` | La tabla de parámetros, su normalización para el host y su formato como texto |
| `state.h` | Lectura y escritura del estado guardado: parámetros, algoritmo de filtro de la instancia y preferencias de la interfaz (`UiSettings`), con migraciones entre versiones |
| `history.h` | Historial de deshacer y rehacer por diferencias: de cada acción, los parámetros que tocó y el algoritmo de filtro, con su valor de antes y de después (`EditAction`, `PendingAction`, `UndoHistory`) |
| `presets.h` | Presets de fábrica, texto de un preset, parámetros que un preset no cambia (el modo de fase y la compensación automática de ganancia) y nombres de preset válidos como nombre de archivo |
| `spectrum_session.h` | Espectros compartidos entre las instancias del proceso: registro, hilo trabajador que solo calcula si alguien mira, y la selección de instancias a comparar ([ADR 0011](decisiones/0011-espectros-compartidos.md)). Cada instancia publica también su frecuencia de muestreo y un reloj de audio, que solo avanza cuando le llega audio (para la igualación) |
| `spectral_mask.h` | La forma de la máscara de la dinámica espectral, del hilo de audio a la interfaz (bloque 6.6 de PLAN.md 5.7): la rejilla fija de 192 puntos de 20 Hz a 20 kHz, la reducción de los bins a ella (`reduceSpectralMask`: cada punto, el bin de su tramo que más cambia; en graves, interpolada) y el publicador (`SpectralMaskPublisher`: unas 30 veces por segundo de audio, por un `dsp::TripleBuffer`) | Audio (publicar), y un lector (la curva) |
| `match.h` | Igualar a una referencia, lo que guarda el procesador mientras está abierto ([ADR 0023](decisiones/0023-igualar-a-una-referencia.md), [docs/dsp/igualacion.md](dsp/igualacion.md)): la referencia (`MatchReference`), los ajustes del panel (`MatchSettings`), lo aprendido (`MatchLearner`: suma en potencia, ponderada por el tiempo, solo con señal en las dos, y los segundos útiles), el hilo del ajuste (`MatchWorker`: se crea con la primera petición, una nueva cancela la anterior y publica la última propuesta; con un modo manual para los tests) y la cola del sidechain (`MatchSidechainTap`) |
| `control_thread.h` | El hilo de control de una instancia: diseña los FIR de fase lineal de un `dsp::PhaseEqualizer` y calcula la compensación de ganancia de un `dsp::AutoGain`, y `flush()` para hacerlo en el momento (al preparar el audio y al renderizar) ([ADR 0012](decisiones/0012-modos-de-fase.md), [ADR 0015](decisiones/0015-compensacion-automatica-de-ganancia.md)) |
| `band_model.h` | De los valores de los parámetros a `dsp::BandSettings` (de una banda o de las 24), dinámica incluida, y al modo de fase (`phaseModeFromChoice`). El procesador y la interfaz usan la misma función, así que **la curva que se dibuja es siempre la que suena** (la interfaz le suma la ganancia dinámica que publica el procesador) |

## Contenido de `plugin/`

| Archivo | Qué hace |
|---|---|
| `PluginProcessor` | Buses, parámetros del host generados a partir de la tabla de `core`, `processBlock`, latencia y cola según el modo de fase, compensación automática de ganancia (y el hilo de control que calcula las dos cosas), estado, colas del analizador, picos de salida, su lugar en la sesión de espectros compartidos (salida y nombre y color de la pista), y la igualación: lo aprendido, los ajustes y el hilo del ajuste mientras está abierto (`core::MatchState`, `core::MatchWorker`), la cola del sidechain y *Apply* como una sola acción de deshacer (`applyAction`) |
| `EditHistory` | Deshacer y rehacer: convierte los gestos de los parámetros en acciones de `core::UndoHistory` |
| `PresetManager` | Presets de fábrica y de usuario (archivos en una carpeta del sistema) |
| `PluginEditorFactory.cpp` | Único punto en el que el procesador conoce a la interfaz (`createEditor`) |
| `JuceText.h` | `toJuce`: texto UTF-8 de `core` como `juce::String` (`ui/src/JuceConvert.h` añade la conversión de los rectángulos de `ui/logic`) |

## Contenido de `ui/logic/`

| Cabecera | Qué ofrece |
|---|---|
| `notes.h` | Escala de notas: frecuencia ↔ nota temperada con una afinación dada, desviación en cents, ajuste a semitonos, notas y afinación escritas como texto, y las teclas del teclado de piano en el eje de la curva ([ADR 0017](decisiones/0017-escala-de-notas.md)) |
| `curve_mapping.h` | Frecuencia y ganancia ↔ píxeles, posición y selección de nodos, rejillas, etiqueta flotante (con la nota, si se ve la escala de notas), indicador de ganancia fuera de rango, valores de una banda nueva (`newBandChanges`, de cualquier tipo), navegación con el teclado y Q con la rueda |
| `control_logic.h` | De arrastre, rueda y teclas a valor; ajuste fino; arco de valor; edición del valor como texto (en Hz, también una nota); textos de ayuda del modo de fase y del botón de la compensación automática de ganancia; texto y ayuda de los tiempos automáticos de la dinámica |
| `meter_logic.h` | Escala y balística del medidor de salida; balística del analizador (con la caída elegida), picos mantenidos, promedio del espectro en el que se buscan picos (`SpectrumAverage`) y ajustes del analizador (`AnalyzerView`); fracción del medidor de dinámica |
| `spectrum_peaks.h` | Bandas desde el espectro: picos del analizador (`findPeaks`), el más cercano al ratón, su Q del ancho a −3 dB, el texto del marcador y los cambios que crean la banda y, con Alt, la hacen dinámica ([ADR 0019](decisiones/0019-bandas-desde-el-espectro.md), [dsp/picos.md](dsp/picos.md)) |
| `spectral_mask.h` | Dinámica espectral: la máscara publicada (192 puntos) en las columnas de la curva, interpolada en escala logarítmica (`spectralMaskColumns`) |
| `layout.h` | Tamaños de ventana (S, M, L y límites) y posición de cada zona y control, con el teclado de notas o sin él, en la vista ampliada y con el cajón de la etapa de salida abierto o plegado |
| `output_stage.h` | Textos del cajón de la etapa de salida ([ADR 0027](decisiones/0027-cajon-de-la-etapa-de-salida.md)): la ayuda de la saturación con su latencia en fase lineal, cuándo no influyen el drive y la mezcla, y lo que marca la pestaña plegada |
| `options_menu.h` | Menú de opciones: entradas de la precisión en agudos, del tamaño de la ventana y de la escala de notas (teclado y afinación) y del analizador, qué marca y qué hace cada una |
| `session_overlay.h` | Comparación con otras instancias: entradas del menú, colisiones con varias a la vez y colocación de sus etiquetas |
| `match.h` | Igualar a una referencia, sin ventana: las referencias del desplegable y la de por defecto, el aprendizaje (`MatchController`: mide la pista y el sidechain con las piezas de la sesión, suma lo aprendido y se para si la referencia desaparece), cuándo se pide la propuesta, el estado del panel con los motivos de *Apply* desactivado, y los cambios que crean las bandas en las libres (`matchApplyChanges`); y, para el panel, sus textos, el rango escrito como la frecuencia y lo que se dibuja (los espectros con la referencia desplazada a la mediana de su diferencia, los tramos del objetivo y las bandas en las que caería la propuesta) ([ADR 0023](decisiones/0023-igualar-a-una-referencia.md)) |

Nada de esto depende de JUCE ni de una ventana: se testea en `tests/ui/`. `ui/src` solo pinta y traduce gestos ([ADR 0008](decisiones/0008-sistema-visual.md), [ui.md](ui.md)).

## Parámetros: una sola fuente de verdad

`core::parameters()` es la única lista de parámetros, 391 en total, en este orden:

1. 2 globales: `output_gain`, `bypass`.
2. 6 por cada una de las 24 bandas (`bNN_on`, `bNN_type`, `bNN_freq`, `bNN_gain`, `bNN_q`, `bNN_slope`), con índice `core::bandParameter()`.
3. 7 de dinámica por cada banda (`bNN_dyn`, `bNN_thr`, `bNN_range`, `bNN_att`, `bNN_rel`, `bNN_knee`, `bNN_sc`), con índice `core::dynamicParameter()`. Van detrás de todas las bandas porque se añadieron en la fase 3 ([ADR 0006](decisiones/0006-dinamica-por-banda.md)).
4. 1 de canal por cada banda (`bNN_chan`), con índice `core::channelParameter()`. Añadido en la fase 4 ([ADR 0007](decisiones/0007-canal-por-banda.md)).
5. 1 global de modo de fase (`phase_mode`), con índice `core::kPhaseMode`. Añadido en la fase 5 ([ADR 0012](decisiones/0012-modos-de-fase.md)).
6. 1 global de compensación automática de ganancia (`auto_gain`), con índice `core::kAutoGain`. Añadido en la fase 5 ([ADR 0015](decisiones/0015-compensacion-automatica-de-ganancia.md)).
7. 1 de tiempos automáticos de la dinámica por cada banda (`bNN_autotime`), con índice `core::autoTimingParameter()`. Añadido en la fase 5 ([ADR 0016](decisiones/0016-tiempos-automaticos-en-la-dinamica.md)).
8. 1 de dinámica espectral por cada banda (`bNN_spectral`, índices 364 a 387), con índice `core::spectralParameter()`. Añadido en la fase 6 ([ADR 0024](decisiones/0024-dinamica-espectral.md) y [0025](decisiones/0025-dinamica-espectral-en-el-procesador.md)).
9. 3 globales de saturación (índices 388 a 390): `sat_mode` ("Saturation": Off, Soft o Warm; Off por defecto), `sat_drive` ("Saturation Drive", de 0 a 24 dB, 6 por defecto) y `sat_mix` ("Saturation Mix", de 0 a 100 %, 100 por defecto), con índices `core::kSaturationMode`, `core::kSaturationDrive` y `core::kSaturationMix`, en los presets. `core::saturationSettings()` los traduce a `dsp::SaturationSettings`. Añadidos en la fase 6 ([ADR 0026](decisiones/0026-saturacion.md)).

 `plugin/` genera los parámetros del host recorriéndola (incluidos el texto que muestran y la escala logarítmica de frecuencia y Q), y el estado se serializa recorriéndola. Para añadir un parámetro:

1. Añadirlo **al final** de la tabla en `core/src/parameters.cpp` (y su índice en `parameters.h`).
2. Ejecutar `ESCEQ_APPEND_PARAMETERS=1 ./esceq_tests "Parametros: contrato*"`, que añade **solo las líneas nuevas** al final de `tests/fixtures/parameters/v1.txt`. Las líneas existentes no se editan nunca.
3. Usarlo en `core/band_model` o `plugin/`.

No hace falta tocar el código de estado ni registrar nada en JUCE.

## Estado guardado

Texto UTF-8 con formato propio (no el de JUCE), versionado. Lleva los parámetros; desde la versión 2, las preferencias de la interfaz (claves `ui.*`, que no son parámetros), y desde la 3, el algoritmo de filtro de la instancia (`filter_algorithm`, el valor de `dsp::FilterAlgorithm`, que tampoco es un parámetro):

```
esceQ-state 3
output_gain 0
bypass 0
...
filter_algorithm 1
ui.analyzer 1
ui.range 18
ui.width 1180
ui.height 700
ui.preset Vocal
ui.notes 1
ui.a4 432
ui.analyzer_fft 8192
```

`ui.notes` (teclado de la escala de notas) y `ui.a4` (afinación, en Hz) solo se escriben si no tienen su valor por defecto (oculto, 440 Hz), así que un proyecto que no los usa se guarda igual que antes ([ADR 0017](decisiones/0017-escala-de-notas.md)); igual los ajustes del analizador, `ui.analyzer_fft`, `ui.analyzer_fall`, `ui.analyzer_tilt` y `ui.analyzer_peaks` ([ADR 0018](decisiones/0018-analizador-ampliado.md)), y el cajón de la etapa de salida, `ui.output_stage`, que solo se escribe plegado ([ADR 0027](decisiones/0027-cajon-de-la-etapa-de-salida.md)). Reglas de lectura: lo ausente toma el valor por defecto, lo desconocido se ignora, lo inválido se sanea, y una cabecera incorrecta hace que se conserve el estado actual. Los estados de versiones anteriores se migran paso a paso (`migrate1to2`, `migrate2to3`): los anteriores a la 3 cargan en Classic, el algoritmo de la 0.1, y suenan bit a bit igual; una instancia nueva usa Matched. Al subir `kStateVersion` se añade una migración en `core/src/state.cpp` y un fixture nuevo; los fixtures antiguos no se tocan. Un preset es este mismo formato con solo parámetros: no lleva el algoritmo, y cargarlo no lo cambia. Ver [ADR 0003](decisiones/0003-formato-de-estado.md), [ADR 0009](decisiones/0009-preferencias-deshacer-y-presets.md) y [ADR 0014](decisiones/0014-estado-v3-algoritmo-de-filtro.md).

## Deshacer, rehacer y presets

- `core::UndoHistory` guarda acciones por diferencias (`core::EditAction`): de cada parámetro que tocaron los gestos de la acción, el valor de antes y el de después, y el algoritmo de filtro si cambió ([ADR 0028](decisiones/0028-automatizacion-macros-y-sidechain.md)). `plugin/src/EditHistory` anota el valor de cada parámetro al empezar su primer gesto de la acción (`core::PendingAction`) y decide cuándo hay una acción nueva: al cerrarse el último gesto abierto de la interfaz, tras 300 ms sin más gestos. Un arrastre, una banda nueva o una ráfaga de rueda son una acción. La automatización del host no abre gestos: no entra en ninguna acción, y deshacer y rehacer, que solo cambian los parámetros de la acción (`PluginProcessor::applyEdit`), no la tocan. Los gestos se cuentan por parámetro; los que abre el propio deshacer no cuentan, y si la ventana se cierra con un gesto abierto, los controles lo cierran y el editor avisa al historial (`abandonOpenGestures`).
- Cargar un proyecto borra el historial; cargar un preset es una acción más, y el *Apply* de la igualación también (`PluginProcessor::applyAction`: `applyValues` y la acción registrada en el momento). Cambiar el algoritmo de filtro (`PluginProcessor::changeFilterAlgorithm`) es una acción propia: lo que estuviera esperando su registro se registra antes, por separado.
- `plugin/src/PresetManager` lista los presets de fábrica (`core::factoryPresets()`) y los de usuario (archivos `.esceq`), y los carga con `PluginProcessor::applyValues`, que avisa al host de cada parámetro que cambia.

## Hilos

| Hilo | Qué hace | Restricciones |
|---|---|---|
| Audio | `processBlock` | No asigna memoria, no bloquea, no hace E/S |
| Mensajes / interfaz | Editor (refresco a 30 Hz), analizador, carga y guardado de estado, historial de deshacer, presets (lectura y escritura de archivos) | Ninguna especial |
| Control (uno por instancia) | Mira cada 10 ms si hay que diseñar un FIR, preparar un modo de fase o calcular la compensación de ganancia, lo hace y se lo entrega al hilo de audio ([ADR 0012](decisiones/0012-modos-de-fase.md), [ADR 0015](decisiones/0015-compensacion-automatica-de-ganancia.md)) | No lo espera nunca el hilo de audio. Se crea y se para con la instancia. Al preparar el audio y al renderizar sin tiempo real, el trabajo se hace en el hilo que llama (`flush()`) |
| Sesión de espectros (uno por proceso) | Lee la cola de cada instancia y calcula su espectro, 20 veces por segundo, solo mientras alguna interfaz compara o aprende de otra instancia ([ADR 0011](decisiones/0011-espectros-compartidos.md)) | No lo usa el hilo de audio; se crea con la primera instancia y se para con la última, nunca al descargar el binario |
| Ajuste de la igualación (uno por instancia) | Calcula la propuesta de la igualación (`dsp::matchCurve`, hasta 1 s con 24 bandas) en `core::MatchWorker` ([ADR 0023](decisiones/0023-igualar-a-una-referencia.md)) | No lo usa el hilo de audio, ni la interfaz lo espera. Se crea con la primera petición y se para con la instancia (tras la iteración en curso) |

**Carga y guardado del estado.** VST3 (`IComponent::setState`) y CLAP (`state.load`) exigen que el host llame a `setStateInformation` desde el hilo de mensajes, y el envoltorio VST3 de JUCE lo comprueba con una aserción en depuración; AU no dice nada. Si algún host lo hiciera desde otro hilo, no hay carreras de datos: los parámetros son atómicos, las preferencias tienen su cerrojo, el historial (`EditHistory`) protege lo suyo con otro, y en JUCE 9 `Timer::stopTimer` se puede llamar desde cualquier hilo (lo dice su documentación) y `AsyncUpdater::cancelPendingUpdate` solo pone a cero un atómico. Un test de `tests/plugin/state_test.cpp` carga estados desde un hilo mientras el de mensajes deshace y el de audio procesa; ejecutado con ThreadSanitizer no da ningún aviso (el CI no lo ejecuta así; cómo hacerlo, en [DEV.md](../DEV.md)). Lo único que no se garantiza en ese caso es el orden: una carga simultánea a un deshacer puede dejar parámetros de los dos. `getStateInformation` solo lee, y se puede llamar desde cualquier hilo.

Comunicación:

- **Interfaz → audio:** mediante parámetros del host y el algoritmo de filtro (abajo). El hilo de audio lee sus valores atómicos una vez por bloque. Los gestos de la curva (arrastrar, rueda, doble clic) van envueltos en `beginChangeGesture`/`endChangeGesture` para que el host los registre como una sola acción de deshacer y de automatización.
- **Algoritmo de filtro:** un `std::atomic<dsp::FilterAlgorithm>` en el procesador. El hilo de mensajes lo escribe (el menú, deshacer, cargar un estado) y el de audio lo lee una vez por bloque, junto a los parámetros; la curva lo lee en cada refresco. Como no es un parámetro, al cambiarlo el procesador avisa al host con `updateHostDisplay` (estado modificado) para que el proyecto se marque como cambiado.
- **Preferencias de la interfaz:** viven en el procesador (`uiSettings()`), protegidas con un cerrojo que el hilo de audio nunca toma. El editor las lee al abrirse y cuando el host carga un estado (`stateRevision()`), y las escribe cuando cambian.
- **Audio → interfaz:** dos `dsp::SpscFifo` (audio mono antes y después del EQ). El hilo de audio escribe; si la cola está llena, descarta. El editor abierto es el **único** lector. Si algún host abriera dos editores a la vez del mismo plugin, habría dos lectores: no está soportado.
- **Audio → interfaz, dinámica espectral:** la ganancia dinámica de cada banda va, como siempre, en 24 atómicos (en una banda espectral, la del bin que más cambia de su máscara). La forma de las máscaras la publica el hilo de audio tras cada bloque con `core::SpectralMaskPublisher`: cada 1/30 de segundo de audio, reduce los bins de cada banda (de 1025 a 4097, según la frecuencia de muestreo) a 192 puntos y los copia en un `dsp::TripleBuffer` (unos 18 KB, dentro del objeto: no asigna ni espera); mientras no suena la dinámica espectral no publica nada, salvo una vez al dejar de sonar, sin máscaras. El único lector es la curva del editor abierto, en su refresco. Solo lee del ecualizador: `processBlock` da lo mismo, bit a bit, se lea o no (`tests/plugin/spectral_test.cpp`).
- **Ganancia dinámica → interfaz:** 24 `std::atomic<float>` que el hilo de audio escribe al final de cada bloque y la interfaz lee a 30 Hz (sin orden entre bandas: basta para dibujar). En una banda espectral, la del bin de su máscara que más cambia. Igual con un `std::atomic<bool>` que indica si el host tiene conectado el sidechain.
- **Audio ↔ control, compensación de ganancia:** con `auto_gain` encendido, cuando cambian los parámetros el hilo de audio publica las bandas en el `dsp::TripleBuffer` de `dsp::AutoGain`; el de control calcula la compensación y la publica con el número de la petición en un atómico de 64 bits. El audio aplica la última que haya, y al encenderla espera a la de los ajustes de ese momento ([docs/dsp/compensacion-de-ganancia.md](dsp/compensacion-de-ganancia.md)).
- **Audio ↔ control, fase lineal:** el hilo de audio publica lo que hay que diseñar en un `dsp::TripleBuffer`; el de control entrega los FIR y los caminos de cada modo por un `dsp::Handoff`, por el que también recoge lo que el audio ya no usa para destruirlo. El hilo de audio no asigna ni libera memoria ([docs/dsp/fase-lineal.md](dsp/fase-lineal.md)).
- **Latencia → host:** el procesador la comunica en `prepareToPlay`. Si cambia de modo con el audio en marcha (o se enciende la primera banda espectral, o se apaga la última, o se enciende o apaga la saturación en fase lineal), la latencia cambia en el hilo de audio; el hilo de control lo ve en su vuelta siguiente (`core::ControlThread`, cada 10 ms) y pide un `juce::AsyncUpdater` (`LatencyReporter`), que en el hilo de mensajes llama a `setLatencySamples`. Sin cambios no hay mensajes ([ADR 0021](decisiones/0021-aviso-de-latencia-sin-temporizador.md)).
- **Audio → otras instancias:** una tercera `dsp::SpscFifo` por instancia, en `core::SpectrumSession`. El hilo de audio solo escribe si alguna interfaz del proceso está comparando (un atómico); la lee el hilo de la sesión, que publica el espectro bajo un cerrojo que el hilo de audio nunca toma. Las interfaces leen copias (`snapshot`).
- **Sidechain → interfaz, para la igualación:** una cuarta `dsp::SpscFifo` (`core::MatchSidechainTap`), que el hilo de audio solo escribe, con la mezcla mono del sidechain, mientras se aprende de él: la interfaz la enciende con un atómico, que el audio lee una vez por bloque. Apagada, `processBlock` no cambia (bit a bit, `tests/plugin/match_test.cpp`). La lee la interfaz que aprende; la pista se mide con lo que lee de la cola "pre", que sigue teniendo un solo lector.
- **Interfaz ↔ ajuste de la igualación:** la interfaz copia lo aprendido, las bandas y los ajustes en una petición (`core::MatchRequest`) y la entrega a `core::MatchWorker` bajo un cerrojo; el hilo del ajuste la calcula con su propia cancelación (un atómico que mira en cada iteración) y publica la propuesta como un `std::shared_ptr` inmutable bajo el mismo cerrojo, que la interfaz lee en cada refresco. Lo aprendido y los ajustes (`core::MatchState`) solo los toca el hilo de mensajes.
- **Nivel de salida → interfaz:** un `std::atomic<float>` por canal con el pico de salida. El hilo de audio solo lo sube; el medidor lo lee y lo pone a cero 30 veces por segundo. Si coinciden puede perderse el pico de un bloque: es un medidor, no una medida.

Toda función de `dsp/` documenta en su cabecera si es segura en tiempo real y si es segura entre hilos. Las que se llaman desde el hilo de audio llevan además la marca `ESCEQ_NONBLOCKING` (`esceq/dsp/realtime.h`): el job `RealtimeSanitizer` del CI ejecuta todos los tests con Clang y `-fsanitize=realtime`, y aborta si alguna de ellas, o algo que llamen, asigna memoria o bloquea. Un test "canario" comprueba que el sanitizador está actuando.

## Flujo de audio actual (fase 6)

```
                         ┌─ IIR: dsp::Equalizer (24 bandas IIR en serie) ──────────────┐
entrada ─► cola "pre" ─► ┤                                                                 ├─► [dsp::SpectralDynamics] ─► dsp::GainRamp (salida + compensación) ─► cola "post" + cola de la sesión + picos ─► salida
                         └─ Linear: FIR (partes estáticas) ─► dsp::Equalizer (solo dinámica) ┘           ▲
                                                                   ▲ detectores (en Linear, retrasados como el audio)
sidechain (bus opcional) ──────────────────────────────────────────┴───────────────────────────┘
```

Cada bloque, el procesador lee los 391 parámetros y, si alguno ha cambiado, los traduce con `core::allBandSettings`, `core::phaseModeFromChoice` y `core::saturationSettings` y se los pasa a `dsp::PhaseEqualizer`, que decide qué suavizar, qué fundir y qué FIR pedir al hilo de control; con `auto_gain` encendido, pide también la compensación de las bandas a `dsp::AutoGain`. En IIR, suena el `Equalizer` IIR, igual que en la 0.1: las bandas desactivadas no consumen CPU, cada banda procesa los dos canales, uno solo, la suma (Mid) o la diferencia (Side), y los detectores de las bandas dinámicas escuchan la entrada del ecualizador o, si así se elige, el bus de sidechain, por el mismo canal que la banda. En fase lineal, la parte estática de las bandas va en un FIR simétrico y la dinámica, en el `Equalizer` en modo solo dinámica, con los detectores y el sidechain retrasados como el audio ([docs/dsp/fase-lineal.md](dsp/fase-lineal.md)). La rampa de salida (20 ms) aplica `output_gain` más la compensación, si está encendida; apagada, o con la curva plana, la compensación suma exactamente 0 dB. Mientras alguna banda tenga `bNN_spectral` encendido, tras el IIR o el FIR suena `dsp::SpectralDynamics`: en las bandas espectrales, su máscara sustituye a la parte dinámica del `Equalizer`, con sus detectores escuchando la entrada del ecualizador (o el sidechain) retrasada como el audio ([docs/dsp/dinamica-espectral.md](dsp/dinamica-espectral.md)). Después, con `sat_mode` fuera de Off, suena `dsp::Saturation`, antes de la rampa de salida y sin que la compensación la tenga en cuenta (compensa la curva; la saturación compensa su drive), con filtros de sobremuestreo IIR en IIR y de fase lineal en los lineales ([docs/dsp/saturacion.md](dsp/saturacion.md)); con Off no se ejecuta, y todo es bit a bit como sin ella. Así la cadena es: IIR o FIR (con la dinámica), dinámica espectral, saturación, `output_gain` más la compensación. Con bypass, el ecualizador hace un fundido a la señal seca (retrasada en fase lineal, con dinámica espectral o con saturación, para que la latencia no cambie) y la ganancia de salida, compensación incluida, vuelve a 0 dB. Mono y estéreo, con el mismo número de canales de entrada y salida; el sidechain puede estar desconectado, ser mono o estéreo. Latencia: 0 en IIR; 1280, 4608 o 17408 muestras a 48 kHz en Low, Medium y High (26,7, 96 y 362,7 ms, los mismos a 96 y 192 kHz); con alguna banda espectral, 2560 muestras más (53,3 ms a 48 kHz, 58 ms a 44,1, los mismos a 96 y 192 kHz). Encender la primera banda espectral, o apagar la última, cambia la latencia como un cambio de modo ([ADR 0025](decisiones/0025-dinamica-espectral-en-el-procesador.md)). Con la saturación fuera de Off, en fase lineal, 47 muestras más a 44,1 y 48 kHz (0,98 ms a 48 kHz), 43 a 96 kHz y 35 a 192 kHz; en IIR, ninguna. Encenderla o apagarla en fase lineal cambia la latencia como un cambio de modo; en IIR es un fundido de 20 ms, sin silencio.

El búfer que entrega el host contiene los canales de todos los buses: el procesador toma el bus principal con `getBusBuffer` y el sidechain como solo lectura. Mientras la igualación aprende del sidechain, su mezcla mono va además a la cola de la igualación (`core::MatchSidechainTap`).

## Formatos, CI y publicación

| Formato | Plataformas | Lo genera | Lo valida el CI |
|---|---|---|---|
| VST3 | Windows, macOS, Linux | JUCE | pluginval, nivel 10 |
| AU | macOS | JUCE | auval |
| CLAP | Windows, macOS, Linux | clap-juce-extensions | clap-validator |
| Standalone | Todas | JUCE | — (para depurar; no se distribuye) |

El workflow `CI` (`.github/workflows/ci.yml`) tiene cuatro jobs: `core` (los módulos sin JUCE y `esceq_bench`, en las tres plataformas), `rtsan` (los tests con RealtimeSanitizer), `sanitizers` (todos los tests, también los del plugin, con AddressSanitizer y UndefinedBehaviorSanitizer) y `plugin` (compila todo, tests, validadores y paquetes; en macOS valida el AU con `auval`; en Windows comprueba que no depende del redistribuible de Visual C++ y prueba el instalador). El workflow `Release` (`release.yml`) lo reutiliza al subir una etiqueta `vX.Y.Z` y publica los paquetes. Ver [ADR 0010](decisiones/0010-distribucion.md).

## Dependencias externas

Se fijan por versión en CMake y se descargan con `FetchContent`.

| Dependencia | Versión | Uso | Licencia |
|---|---|---|---|
| JUCE | 9.0.5 | `plugin/`, `ui/src/`, `tools/snapshot` | AGPLv3 |
| clap-juce-extensions | commit `6bde108` (incluye CLAP 1.2.7) | Formato CLAP del plugin | MIT |
| Catch2 | 3.16.1 | `tests/` | BSL-1.0 |

En el CI, además: pluginval 1.0.4 y clap-validator 0.3.2 para validar los formatos (los dos fijados por versión; en Linux, pluginval se compila con un arreglo de JUCE, `tools/pluginval/`, [ADR 0021](decisiones/0021-aviso-de-latencia-sin-temporizador.md)), `auval` del sistema para el AU, e Inno Setup 6 para el instalador de Windows.

En Linux, el job `plugin` se ejecuta en un contenedor `ubuntu:22.04` con GCC 11, para que el VST3 y el CLAP pidan como mucho glibc 2.35 y la libstdc++ de GCC 11; lo comprueba `packaging/unix/check-linux-binary.sh`. En macOS, el CI firma ad hoc cada paquete y comprueba sus dos arquitecturas, macOS 11 como mínimo y la firma antes de validarlo ([ADR 0022](decisiones/0022-compatibilidad-en-macos-y-linux.md)).

En Windows la biblioteca de ejecución de MSVC se enlaza de forma estática (`CMAKE_MSVC_RUNTIME_LIBRARY`, `/MT`): el plugin no depende del redistribuible de Visual C++, que el instalador no incluye. El CI lo comprueba con `dumpbin`.

Además, la tipografía Inter 4.1 (SIL OFL 1.1) va copiada en `ui/assets/fonts/`, sin modificar y con su licencia, y se embebe en el binario.
