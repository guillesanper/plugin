# Modos de fase

Cómo funcionan los cuatro modos de fase de `dsp::PhaseEqualizer`: IIR (los filtros de siempre, sin latencia) y tres de fase lineal (Low, Medium y High). Las decisiones y sus alternativas están en la [ADR 0012](../decisiones/0012-modos-de-fase.md); los filtros de cada banda, en [filtros.md](filtros.md).

## Los cuatro modos

| Modo | Filtro | Coeficientes (≤ 50 kHz) | Bloque | Latencia a 48 kHz | Para qué |
|---|---|---|---|---|---|
| **IIR** | IIR (secciones SVF), el `Equalizer` de siempre | — | — | 0 | Mezcla, grabación, tiempo real |
| **Linear Low** | FIR simétrico | 2048 | 256 | 1280 muestras (26,7 ms) | Fase lineal con poca latencia |
| **Linear Medium** | FIR simétrico | 8192 | 512 | 4608 muestras (96 ms) | Buses y mastering |
| **Linear High** | FIR simétrico | 32768 | 1024 | 17408 muestras (362,7 ms) | Mastering y render |

- **IIR** suena exactamente igual que el ecualizador de la 0.1: mismo código, bit a bit (lo comprueba `tests/dsp/phase_equalizer_test.cpp`). Como cualquier IIR, desplaza la fase de forma distinta en cada frecuencia, como un ecualizador analógico.
- **Fase lineal**: todas las frecuencias se retrasan lo mismo (la latencia), así que la forma de onda no se deforma: un transitorio sale entero, solo más tarde. A cambio, hay latencia y un preeco (el FIR es simétrico: lo que suena después del centro también suena antes).
- **Frecuencias de muestreo altas.** Cada duplicación de la frecuencia de muestreo duplica coeficientes y bloque (hasta ×8 a partir de 400 kHz). Así la resolución en hercios y la latencia en milisegundos no cambian: a 96 kHz, High tiene 65536 coeficientes y 34816 muestras de latencia, los mismos 362,7 ms. Hasta 50 kHz los tamaños son los mismos: a 44,1 kHz la latencia es un 9 % mayor en milisegundos (29 ms en Low) y la resolución en hercios, un 9 % mejor.

## Diseño del FIR

`dsp::LinearPhaseDesigner` convierte los ajustes de las bandas en un FIR de `n` coeficientes (`dsp/linear_phase.h`):

1. **Magnitud objetivo.** Para cada banda activada, la magnitud exacta de su filtro digital, `|H(f)|`, la misma que dibuja la interfaz (`dsp::responseAtTangent`), en una rejilla de `2n` frecuencias de 0 a fs. Así **la curva que se dibuja es la que suena** también en fase lineal, incluida la forma cerca de Nyquist. Solo cuenta la parte estática de cada banda (su ganancia); la dinámica va aparte (ver más abajo).
2. **Canales.** Cada banda actúa sobre una parte de la señal estéreo (ver [ecualizador.md](ecualizador.md)). Con filtros de fase cero, el efecto de una banda en cada frecuencia es una matriz real de 2×2 sobre (L, R):

   | Canal | Matriz |
   |---|---|
   | Stereo | `[h 0; 0 h]` |
   | Left / Right | `[h 0; 0 1]` / `[1 0; 0 h]` |
   | Mid | `[1+d d; d 1+d]`, con `d = (h−1)/2` |
   | Side | `[1+d −d; −d 1+d]` |

   Las bandas en serie multiplican sus matrices en orden (`channelMatrix`, `operator*`). Si todas son Stereo basta un FIR; con Left o Right, uno por canal; con Mid o Side, cuatro (`ll`, `lr`, `rl`, `rr`). En **mono** solo cuentan las bandas Stereo, Left y Mid, como en el IIR.
3. **Al tiempo.** FFT inversa (en `double`) del espectro real y par: da un impulso real, par y centrado en 0. Dos espectros van juntos en una sola FFT compleja (uno en la parte real y otro en la imaginaria).
4. **Ventana.** Se toman los `n − 1` coeficientes centrales y se multiplican por una ventana de Kaiser con β = 8. El coeficiente 0 queda a cero, y `h[c+k] = h[c−k]` exactamente (se fuerza promediando los dos lados): **fase lineal exacta**, con retardo `c = n/2`.
5. **Identidad.** Si ninguna banda cambia la señal (todas desactivadas, campanas a 0 dB, pasa todo), el FIR es un retardo puro y la salida se toma de la señal seca retrasada: **bit a bit**.

El diseñador guarda la magnitud de cada banda: al mover una sola, solo se recalcula esa.

### Por qué la ventana de Kaiser con β = 8

La ventana decide el compromiso entre resolución (lo fino que puede ser un detalle del espectro) y fugas (cuánto se cuela de una zona a otra, por ejemplo en la banda de rechazo de un corte). Medido con una campana estrecha en graves (60 Hz, Q 2, +6 dB) y un corte de 96 dB/oct en 300 Hz, a 48 kHz:

| β | Error de la campana, Low / Medium / High | Peor nivel de 20 a 100 Hz del corte, Low |
|---|---|---|
| 5 | 2,1 / 0,37 / 0,028 dB | −93 dB |
| **8** | **2,6 / 0,56 / 0,046 dB** | **−112 dB** |
| 10 | 2,8 / 0,66 / 0,058 dB | −120 dB |

Con β = 8 se pierde poca resolución respecto a β = 5 y se ganan 19 dB de rechazo en el peor caso.

### Precisión medida

Errores de magnitud del FIR frente a la magnitud exacta de las bandas, a 48 kHz (`tests/dsp/linear_phase_test.cpp`):

| Caso | Low | Medium | High |
|---|---|---|---|
| Cualquier tipo, por encima de 1 kHz | < 0,15 dB | < 0,02 dB | < 0,005 dB |
| Campana de 60 Hz, Q 2, +6 dB (de 20 a 400 Hz) | < 3 dB | < 0,6 dB | < 0,05 dB |
| Ajuste completo (corte de 30 Hz, campanas, shelf), de 40 Hz a 20 kHz | — | — | < 0,05 dB |
| Rechazo de un corte de 96 dB/oct, una octava por debajo y más | > 100 dB | > 100 dB | > 100 dB |

Es lo que distingue los tres modos: **la resolución en graves**. Low no puede reproducir detalles estrechos por debajo de unos 200 Hz; High los reproduce casi exactos hasta 20 Hz.

## Convolución

`dsp::PartitionedConvolver` aplica el FIR por bloques en el dominio de la frecuencia: convolución particionada uniforme con solapamiento y descarte (Gardner, 1995; Wefers, 2015). El FIR se parte en `n / B` particiones de `B` coeficientes, y en cada bloque de `B` muestras:

1. FFT de `2B` puntos de los dos últimos bloques de entrada, que se guarda en una línea de retardo de espectros.
2. Suma de los productos de cada partición por el espectro del bloque que le corresponde.
3. FFT inversa; la segunda mitad es la salida del bloque siguiente.

La latencia de la convolución es `B`; la del FIR, `n/2`. **Latencia total: `n/2 + B`.**

**Los dos canales en una sola FFT.** Se transforma `z = L + i·R`. La salida `yL + i·yR` es `p ∗ z + q ∗ conj(z)`, con

```
p = ((ll + rr) + i·(rl − lr)) / 2
q = ((ll − rr) + i·(rl + lr)) / 2
```

y el espectro de `conj(z)` es `conj(Z[N−k])`, que no hace falta calcular aparte. Si los dos canales se tratan igual (todas las bandas en Stereo), `q = 0`: una FFT directa, una inversa y un producto por partición, para los dos canales a la vez.

**Independencia del tamaño de bloque del host.** La convolución acumula las muestras hasta completar su bloque, así que el resultado es idéntico bit a bit con bloques del host de 1, 13, 64, 511 o 4096 muestras.

## Dinámica en fase lineal

La ganancia de una banda dinámica cambia muestra a muestra; un FIR no se puede rediseñar a esa velocidad. La solución:

- **La parte estática de todas las bandas va en el FIR**, también la de las dinámicas.
- **La parte dinámica la aplica el `Equalizer` IIR en modo "solo dinámica"** (`setDynamicsOnly`), después del FIR. Cada banda dinámica aplica

  ```
  H(−g) · H(g + d)        g: ganancia estática, d: ganancia dinámica
  ```

  En los cuatro tipos con ganancia (campana, shelves y tilt) `H(−g) = 1/H(g)` exactamente, también en digital (medido: error < 4·10⁻¹⁴), así que:

  - con la dinámica en reposo (`d = 0`), la sección es la unidad: **la banda sigue siendo de fase lineal**;
  - la magnitud total es `|H(g)| · |H(g+d)| / |H(g)| = |H(g+d)|`: **exactamente la del modo IIR**;
  - `H(−g)` va antes que `H(g+d)`: la sección que cambia oye la misma señal que en IIR, y sus transitorios son los mismos. Con el orden contrario, al moverse la ganancia aparecían hasta 5 dB más de energía por encima de 8 kHz (medido en el test de modulación rápida); con este, el resultado es el del modo IIR.

  Mientras la dinámica actúa, esa banda añade el desfase de un IIR solo en la medida en que la ganancia se separa de la estática. Las bandas sin dinámica no pasan por el IIR.
- **Los detectores** escuchan la entrada del ecualizador retrasada la latencia, y el **sidechain** se retrasa igual. La ganancia dinámica es la misma que en IIR, muestra a muestra, solo que llega con la latencia (lo comprueba un test bit a bit).

## Dinámica espectral

Mientras alguna banda tenga `dynamics.spectral` (el parámetro `bNN_spectral`), después del IIR o del FIR suena `dsp::SpectralDynamics` ([dinamica-espectral.md](dinamica-espectral.md), ADR [0024](../decisiones/0024-dinamica-espectral.md) y [0025](../decisiones/0025-dinamica-espectral-en-el-procesador.md)), en los cuatro modos:

- En las bandas espectrales, **la máscara sustituye a la parte dinámica**: el `Equalizer` (el IIR, o el de solo dinámica en fase lineal) recibe la banda sin dinámica, y la parte estática sigue en el IIR o en el FIR. Las demás bandas dinámicas no cambian.
- **El detector** de la máscara escucha la entrada del ecualizador, y el sidechain, retrasados como el audio: en IIR, sin retrasar; en fase lineal, los mismos retardos que usan los detectores de la dinámica (lo comprueba un test bit a bit contra el motor solo).
- **Latencia**: la del modo más la de la STFT, `N + N/4` (2560 muestras a 44,1 y 48 kHz, 5120 a 96 y 10240 a 192: 53,3 ms a 48 kHz y 58 ms a 44,1). En IIR, de 0 a 53,3 ms; en Low, de 26,7 a 80 ms; en Medium, de 96 a 149,3 ms; en High, de 362,7 a 416 ms.
- **Sin ninguna banda espectral**, `PhaseEqualizer` hace exactamente lo de antes: el mismo camino, el mismo sonido bit a bit y la misma latencia en los cuatro modos (comparado con el código anterior: ver [dinamica-espectral.md](dinamica-espectral.md)).

## Saturación

Con `sat_mode` fuera de Off, al final (tras el IIR o el FIR y la dinámica espectral, antes de la ganancia de salida) suena `dsp::Saturation` ([saturacion.md](saturacion.md), [ADR 0026](../decisiones/0026-saturacion.md)), con los filtros del sobremuestreo según el modo que suena:

- **IIR:** polifásicos IIR pasa todo, sin latencia (con unas 6 muestras de retardo de grupo en graves a 48 kHz).
- **Low, Medium y High:** FIR de media banda de fase lineal, con su latencia sumada a la del modo: 47 muestras a 44,1 y 48 kHz (0,98 ms a 48 kHz), 43 a 96 kHz y 35 a 192 kHz. En Low, 1327 muestras a 48 kHz; en Medium, 4655; en High, 17455; con alguna banda espectral, 2560 más.
- **Con Off**, la etapa no se ejecuta: `PhaseEqualizer` hace exactamente lo de antes, con la misma latencia en los cuatro modos (comparado con el código anterior: ver [saturacion.md](saturacion.md)).

## Cambios con el audio en marcha

| Cambio | Qué pasa |
|---|---|
| Ajustes de las bandas en fase lineal | El hilo de diseño calcula el FIR nuevo (de 0,2 a 13 ms) y lo entrega; en el bloque de convolución siguiente se hace un **fundido de un bloque** (de 5 a 21 ms) entre la salida con el FIR anterior y con el nuevo. Mientras se diseña, suena el anterior. |
| Modo de fase | Mientras se prepara el modo nuevo, suena el anterior. Después: **fundido a silencio** en 10 ms, cambio, silencio mientras el modo nuevo llena su latencia, y **fundido de entrada** en 10 ms. La latencia que se comunica al host cambia en el momento del cambio. |
| Primera banda espectral encendida, o última apagada | Como el cambio de modo: fundido a silencio en 10 ms, cambio, silencio mientras se llena la latencia nueva y fundido de entrada. Encender o apagar las demás no cambia la latencia ni deja hueco. |
| Saturación encendida o apagada (desde Off o hacia Off) | En fase lineal, como el cambio de modo: fundido a silencio en 10 ms, cambio, silencio mientras se llena la latencia nueva (la del modo más 47 muestras a 48 kHz, o sin ellas) y fundido de entrada. En IIR no cambia la latencia: el fundido de 20 ms de la saturación, sin silencio. Entre Soft y Warm, y el drive y la mezcla, con las rampas de 20 ms de la saturación, en todos los modos. |
| Modo de fase con la saturación encendida | Los filtros del sobremuestreo cambian (IIR o de fase lineal) en el silencio del cambio de modo, con la latencia nueva. |
| Bypass en fase lineal, con dinámica espectral o con saturación | Fundido de 10 ms con la señal seca **retrasada**: la latencia no cambia con el bypass, y con el bypass completo la salida es la entrada retrasada, bit a bit. |

Cambiar de modo, encender la primera banda espectral o, en fase lineal, encender o apagar la saturación (también al cargar un preset que lo haga: la saturación va en los presets), deja un hueco: no se puede pasar sin él de una latencia a otra. El host recalcula su compensación de retardo; mientras tanto la pista puede quedar desalineada un momento respecto a las demás. Por eso el modo de fase es un ajuste "de sesión": no se automatiza con normalidad y los presets no lo cambian.

## Hilos

```
hilo de audio                                   hilo de control (core::ControlThread)
─────────────                                   ─────────────────────────────────────
setBand / setPhaseMode ─► TripleBuffer ───────► serviceDesigns(): lee la última petición
                                                 diseña el FIR (y, si cambia el modo,
process(): recoge ◄──────── Handoff ◄──────────  construye el camino: convolución y retardos)
           devuelve lo que ya no usa ─► Handoff ─► y los destruye
```

- **El hilo de audio no asigna ni libera memoria.** Las peticiones van en un `TripleBuffer` (el lector ve siempre la última, sin esperar). Los FIR y los caminos van en un `Handoff`: el hilo de diseño entrega y el de audio recoge; lo que el audio ya no usa vuelve por el mismo `Handoff` y lo destruye el hilo de diseño. `take()` solo entrega si queda sitio para devolver, así que el hilo de audio nunca tiene que liberar nada (`dsp/handoff.h`). El job de RealtimeSanitizer del CI ejecuta todos los tests con esto.
- **El hilo de diseño** es el hilo de control de la instancia (`core::ControlThread`), que también calcula la compensación automática de ganancia ([compensacion-de-ganancia.md](compensacion-de-ganancia.md)). Mira cada 10 ms si hay algo nuevo (una lectura atómica). En la 0.x hay uno por instancia.
- **Render sin tiempo real.** El procesador llama a `ControlThread::flush()` en cada bloque: el FIR de los ajustes de ese bloque se diseña en el momento. El resultado no depende de lo rápido que vaya el render (lo comprueba un test que renderiza dos veces con automatización).
- **Al preparar el audio** (`prepareToPlay`) también se diseña en el momento: el primer bloque ya suena en el modo pedido y el host conoce la latencia antes de empezar.
- Los cambios rápidos de modo (de Medium a IIR y vuelta a Medium antes de que el hilo de diseño lo vea, por ejemplo) se resuelven con dos contadores: uno de "época" (modo y frecuencia de muestreo) y otro de "generación" (cualquier cosa que cambie el FIR). Lo que llega para una época o generación antigua se devuelve sin usar.

## Coste

Medido a 48 kHz con bloques de 256 muestras, compilado con optimización, en el equipo de desarrollo (un núcleo moderno; en otros equipos cambia la escala, no las proporciones):

| Modo | CPU, 1 banda | CPU, 8 bandas (una Mid) | Diseño inicial | Diseño al mover una banda |
|---|---|---|---|---|
| IIR | 0,03 % | 0,20 % | — | — |
| Linear Low | 0,22 % | 0,30 % | 0,6 ms | 0,3 ms |
| Linear Medium | 0,30 % | 0,45 % | 2,9 ms | 1,3 ms |
| Linear High | 0,72 % | 1,0 % | 13 ms | 5,9 ms |

En fase lineal el coste apenas depende del número de bandas: es el de la convolución. El cálculo se concentra en las llamadas en que se completa un bloque de convolución (cada 256, 512 o 1024 muestras a 48 kHz, y el doble a cada duplicación de la frecuencia de muestreo). Esa llamada tarda (`esceq_bench --convolution`, medianas; entre paréntesis, al cambiar de FIR, que calcula dos salidas para el fundido):

| Modo | 48 kHz | 96 kHz | 192 kHz | Cada |
|---|---|---|---|---|
| Linear Low | 0,01 ms (0,02) | 0,02 ms (0,04) | 0,05 ms (0,09) | 5,3 ms |
| Linear Medium | 0,03 ms (0,05) | 0,06 ms (0,11) | 0,12 ms (0,24) | 10,7 ms |
| Linear High | 0,08 ms (0,15–0,18) | 0,2 ms (0,4) | 0,44 ms (0,77) | 21,3 ms |

El producto por las particiones ya lo vectoriza el compilador; la FFT directa y la inversa son algo menos de la mitad. La [ADR 0020](../decisiones/0020-sin-simd.md) tiene las medidas de los cuatro modos de 44,1 a 192 kHz y por qué no se optimiza con SIMD. Memoria en High a 48 kHz: el camino de audio ocupa de 1,3 MB (todas las bandas en Stereo) a 2,3 MB (con Mid, Side, Left o Right), y el diseñador de 2,5 a 3 MB más 0,26 MB por banda activa (unos 9 MB con las 24). Medium ocupa la cuarta parte, y Low la dieciseisava. En IIR no se reserva nada de esto.

## Verificación

- `tests/dsp/linear_phase_test.cpp`: tamaños y latencias; matrices de canal frente al IIR; simetría exacta y fase lineal; precisión de la magnitud por tipo y por modo; resolución en graves creciente con el modo; rechazo de los cortes; Mid, Side, Left y mono; caché del diseñador; convolución frente a la convolución directa (con canales mezclados), independencia del tamaño de llamada, fundido entre núcleos, núcleo que no encaja y latencia exacta.
- `tests/dsp/phase_equalizer_test.cpp`: IIR bit a bit igual que el `Equalizer`; retardo bit a bit sin bandas; respuesta simétrica con la magnitud de las bandas; independencia del tamaño de bloque; bypass; sin chasquidos al mover bandas y al cambiar de modo; lo diseñado para un modo que ya no se pide se descarta; canales; dinámica y sidechain retrasados con la misma ganancia que en IIR; NaN en la entrada; cambio de frecuencia de muestreo; mono.
- `tests/dsp/dynamics_test.cpp`: el modo solo dinámica con la parte estática aparte suena como la banda entera, también con modulación rápida.
- `tests/dsp/handoff_test.cpp` y `tests/core/phase_design_thread_test.cpp`: el intercambio entre hilos, también con dos hilos a la vez (para ejecutarlos con ThreadSanitizer, ver [DEV.md](../../DEV.md)).
- `tests/dsp/phase_equalizer_test.cpp`, la dinámica espectral: sin bandas espectrales, la latencia y el sonido de siempre en los cuatro modos; su latencia sumada a la del modo y medida con un impulso a 44,1, 48, 96 y 192 kHz, en estéreo y en mono; la máscara en lugar de la dinámica de la banda; sin chasquidos ni huecos al encender y apagar la primera y las demás; independencia del tamaño de bloque; bypass; el detector retrasado en fase lineal.
- `tests/plugin/phase_test.cpp`: latencia comunicada al host, cola, render sin tiempo real, bypass y estado guardado; y, sin dinámica espectral, la latencia y el sonido de antes en los cuatro modos.
- `tests/dsp/phase_equalizer_test.cpp`, la saturación: con Off, la latencia y el sonido de siempre en los cuatro modos; su latencia sumada a la del modo y medida con un impulso a 44,1, 48, 96 y 192 kHz, sola y con la dinámica espectral; su lugar en la cadena y sus filtros (bit a bit, una `Saturation` sobre la salida sin ella); encender y apagar en IIR sin hueco y en fase lineal como el cambio de modo, sin chasquidos; cambiar de modo con ella; independencia del tamaño de bloque; bypass.
- `tests/plugin/saturation_test.cpp`: con Off, la latencia, la cola y el sonido de antes en los cuatro modos; con ella, la latencia comunicada es la medida en los cuatro modos y a 44,1, 48, 96 y 192 kHz, y el aviso al host en fase lineal.
- `tests/plugin/spectral_test.cpp`: con dinámica espectral, la latencia comunicada es la medida en los cuatro modos y a 44,1, 48, 96 y 192 kHz, el aviso al host al encender la primera banda, sin chasquidos, y el interruptor llegando al audio y al medidor.

Se comprobó que los tests detectan los fallos: quitando el fundido entre núcleos, los fundidos al cambiar de modo, la espera de la latencia, el retraso del detector, el del sidechain o el de la señal seca del bypass, la petición de un FIR nuevo al descartar un camino, o la parte estática del modo solo dinámica, falla al menos un test.

## Limitaciones

- **Preeco.** Inherente a la fase lineal: en Low es corto; en High, un realce o un corte estrecho en graves puede oírse antes del transitorio que lo excita.
- **Resolución en graves** según el modo (ver la tabla de precisión). Un notch estrecho no llega a la profundidad del IIR: un notch de Q 10 en 1 kHz baja unos 11 dB en Low, 22 en Medium y 34 en High.
- **Pasa todo.** En fase lineal un pasa todo no hace nada: solo cambia la fase, y la fase lineal la iguala en todas las frecuencias.
- **El analizador "pre"** muestra la entrada sin retrasar, y el "post" la salida, que llega con la latencia: en High, el "post" va unos 360 ms por detrás.
- **Un hilo de control por instancia**, aunque esté en IIR (mira cada 10 ms si hay algo que hacer).
- **El trabajo de un bloque de la convolución se hace en una sola llamada** (tabla de [Coste](#coste)). Con bloques del host muy pequeños puede tardar más que el periodo de esa llamada: a 192 kHz con bloques de 32 muestras (0,17 ms), Linear High siempre y Medium al cambiar el FIR; a 96 kHz con bloques de 32 (0,33 ms), Linear High al cambiar el FIR. Con bloques de 128 muestras o más a 96 kHz, y de 256 o más a 192 kHz, cabe con margen. Lo que lo resolvería es repartir ese trabajo entre llamadas (particiones no uniformes); el SIMD solo lo bajaría en torno a un 20 % ([ADR 0020](../decisiones/0020-sin-simd.md)).
