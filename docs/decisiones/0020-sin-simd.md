# 0020. Optimización SIMD: medida, y no compensa

**Estado:** Aceptada (noviembre de 2026)

## Contexto

La sección 4.5 del [plan](../../PLAN.md) dice "SIMD solo después de medir", y la fase 5 deja la optimización SIMD para después de las funciones, solo si las medidas la justifican. Optimizar el hilo de audio tiene un precio en este proyecto: el contrato 3 de la sección 2.1 exige que una configuración dada suene igual tras actualizar, el código tiene que compilar y dar el mismo resultado en x86-64 (MSVC, GCC, Clang) y arm64 (macOS universal), y todo camino con intrínsecos necesita una alternativa escalar testeada. Había que decidir, con números:

1. Dónde se va el tiempo del hilo de audio: 24 bandas, con y sin dinámica, en IIR y en fase lineal, de 44,1 a 192 kHz y con bloques de 32 a 1024 muestras.
2. Qué ganaría el SIMD allí donde se va.
3. Con qué criterio se adopta, y si se cumple.

## Medidas

Todas en el equipo de desarrollo (Linux, x86-64, Xeon a 2,1 GHz en una máquina virtual, GCC 13, `-O3` sin `-march`, como la compilación Release), en % de un núcleo para una instancia estéreo, con la mediana de cinco tandas. La máquina virtual da un ±20 % entre tandas; las cifras van redondeadas. Se repiten con `esceq_bench` (`tools/bench`), que se añade para eso y se compila con `-DESCEQ_BUILD_TOOLS=ON`.

**Coste por escenario.** El tamaño de bloque del host apenas influye: el ecualizador trocea el audio en tramos de 32 muestras ([ecualizador.md](../dsp/ecualizador.md)), así que de 32 a 1024 muestras la diferencia queda dentro del ruido. Con el algoritmo Matched salen las mismas cifras, dentro del ruido de la medida.

| Escenario (Classic) | Modo | 44,1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|
| Mezcla: 8 bandas (2 cortes de 24 dB/oct, 4 campanas, 2 de ellas dinámicas, un shelf) | IIR | 0,55 | 0,65 | 1,3 | 2,4 |
| | Linear Low / Medium / High | 0,75–0,95 | 0,75–0,85 | 1,3–1,8 | 2,8–3,9 |
| 24 campanas estáticas | IIR | 0,75 | 0,85 | 1,7 | 3,6 |
| | Linear (los tres) | 0,35–0,45 | 0,3–0,85 | 0,6–1,05 | 1,2–2,3 |
| 24 cortes de 96 dB/oct (192 secciones) | IIR | 5–6 | 5,5–8,5 | 11–12 | 22 |
| | Linear (los tres) | 0,3–0,45 | 0,3–0,5 | 0,7–1,3 | 1,5–3,2 |
| 24 campanas dinámicas comprimiendo sin parar | IIR | 4,5 | 5 | 9,5 | 15–20 |
| | Linear (los tres) | 4,3–5,4 | 4,6–6,4 | 10–12 | 21–22 |

**El procesador no añade casi nada.** Medido con `processBlock` del plugin (tests de `esceq_plugin_tests`, Release con LTO): sin bandas, 0,04 % a 48 kHz con bloques de 256 y 0,11 % con bloques de 32; con bandas, lo mismo que `dsp` más eso. El tiempo está en `dsp::Equalizer` y en la convolución.

**Dónde se va dentro de las dinámicas** (callgrind, instrucciones, 24 campanas dinámicas a 48 kHz): un 32 % en `BandFilter::process` (la banda y su detector, en tramos que la dinámica parte en cada cambio de ganancia), un 23 % en `log10` de la libm, un 11 % en la curva de ganancia (`dynamicGainDb`) y un 17 % en `processBand`, con el rediseño de la banda. Es aritmética escalar por muestra que depende de la muestra anterior.

**Por qué las cascadas IIR no son más rápidas.** Cada sección SVF es una recurrencia: el tiempo lo marca la latencia de la cadena de operaciones de una muestra a la siguiente (unos 16 a 20 ciclos), no el número de operaciones. `BandFilter` ya procesa los dos canales en el mismo bucle para solapar sus dos cadenas ([filtros.md](../dsp/filtros.md)). GCC no lo vectoriza (el ensamblador solo tiene `mulsd` y `addsd`), y empaquetarlo a mano ahorra poco (experimentos, abajo): las dos cadenas siguen teniendo la misma latencia, y solo bajan las instrucciones.

**Fase lineal: el pico, no la media.** El producto de la convolución (`PartitionedConvolver::accumulate`) ya lo vectoriza el compilador (`mulps`, `addps`); lo escalar es la FFT radix 2: la directa y la inversa son algo menos de la mitad del trabajo de un bloque. Ese trabajo se hace entero en la llamada que completa el bloque de la convolución:

| Modo | 48 kHz | 96 kHz | 192 kHz | Cada |
|---|---|---|---|---|
| Linear Low | 0,01 ms (0,02 con fundido) | 0,02 (0,04) | 0,05 (0,09) | 5,3 ms |
| Linear Medium | 0,03 (0,05) | 0,06 (0,11) | 0,12 (0,24) | 10,7 ms |
| Linear High | 0,08 (0,15–0,18) | 0,2 (0,4) | 0,44 (0,77) | 21,3 ms |

El fundido (al cambiar el FIR mientras suena) calcula dos salidas. Con bloques del host de 32 muestras, el periodo de una llamada es de 0,67 ms a 48 kHz, 0,33 ms a 96 kHz y 0,17 ms a 192 kHz: a 192 kHz, Linear High tarda en esa llamada más que el periodo siempre, y Medium al cambiar el FIR; a 96 kHz, Linear High al cambiar el FIR.

**Experimentos SIMD** (copias de `band_filter.cpp` fuera del repositorio, 48 kHz, bloques de 256, mediana de siete tandas, dos veces; mismas sumas de control de la salida que el código actual, como era de esperar con las mismas operaciones en el mismo orden y sin FMA):

| Variante | 24 cortes de 96 dB/oct | 24 campanas | 24 dinámicas |
|---|---|---|---|
| Actual | 5,7 | 0,87 | 3,8 |
| SSE2: L y R en un `__m128d` | 4,7 (−18 %) | 0,64 (−25 %) | 3,5 (dentro del ruido) |
| Dos secciones de la cascada en el mismo bucle, escalar | 5,2 (−9 %) | — | sin cambio |
| Las dos cosas a la vez | 3,4 (−40 %) | — | sin cambio |
| `-march=native` (AVX2 y AVX-512, autovectorizado) | 4,6 | 0,8 | 3,6 |

**Plataformas.** Clang en arm64 (macOS) ya contrae la recurrencia del SVF en `fmadd` y `fnmsub` (comprobado compilando el bucle para `arm64-apple-macos11`); GCC y MSVC en x86-64, no (`CMAKE_CXX_EXTENSIONS OFF`: GCC no contrae en modo ISO, y MSVC no lo hace por defecto). Hoy un Mac con Apple Silicon y un PC ya difieren en los últimos bits, dentro de la tolerancia de 0,001 dB de los archivos de referencia ([filtros.md](../dsp/filtros.md)). Un camino NEON tendría que elegir entre igualar el escalar de arm64 (con FMA) o el de x86-64, y no podría prometer las dos cosas.

## Decisión

1. **Criterio.** El SIMD se adopta si cumple una de dos condiciones: ahorra al menos un 25 % en una configuración realista (la mezcla de 8 bandas) cuando su coste importa, es decir, más de un 2 % de un núcleo a 44,1 o 48 kHz; o quita un riesgo medido de cortes en el audio. Además, el resultado tiene que ser el mismo bit a bit en cada plataforma, porque detrás de una versión de algoritmo solo deberían ir los cambios que mejoran el sonido, no los de velocidad.

2. **No se cumple, y no se optimiza nada.**
   - La mezcla realista cuesta del 0,55 al 0,95 % de un núcleo a 44,1 y 48 kHz, y del 2,4 al 3,9 % a 192 kHz. El SIMD ahorraría como mucho unas décimas de punto.
   - Lo único que baja mucho (−40 %) son cascadas muy largas, como 24 cortes de 96 dB/oct, que nadie usa así. Para eso hay que reescribir `BandFilter` con secciones fusionadas, con intrínsecos SSE2 y NEON y una versión escalar, y la de arm64 no daría el mismo resultado que la de x86-64.
   - Lo segundo más caro, las dinámicas, no mejora con SIMD: se va en `log10`, en la curva de ganancia y en el rediseño, por muestra y en serie. Vectorizar el logaritmo exige una aproximación propia, que cambia la ganancia en los últimos bits y, con la resolución de 0,001 dB, el momento en que se rediseña la banda: el sonido cambiaría, y haría falta una versión de algoritmo para ganar velocidad.
   - El riesgo de la fase lineal, el pico de la llamada que completa un bloque, no lo quita el SIMD: una FFT el doble de rápida lo bajaría en torno a un 20 %, y en Linear High a 192 kHz con bloques de 32 seguiría por encima del periodo. La solución es repartir ese trabajo entre llamadas (particiones no uniformes, o la FFT por etapas), que es un cambio de algoritmo y no de instrucciones.

3. **Se queda la herramienta de medida.** `esceq_bench` (`tools/bench`, solo depende de `dsp`) mide los cuatro escenarios en los cuatro modos, a las frecuencias y bloques que se le pidan, y el pico de la convolución (`--convolution`). El CI la compila en Windows, macOS y Linux y pasa una prueba de humo, para que siga funcionando cuando haga falta volver a medir.

4. **Cuándo revisar esta decisión.** Si llegan bucles limitados por el número de operaciones y no por la latencia: la dinámica espectral (sección 5.7, FFT en cada bloque y ganancias por bin) o el sobremuestreo de la saturación (5.8, FIR polifásicos). Ahí el SIMD sí rinde y se medirá con esta herramienta. Para las cascadas IIR, si alguna vez hicieran falta, la candidata medida es la de la tabla: dos secciones por bucle con L y R empaquetados, con su versión escalar y un test bit a bit contra ella en x86-64.

## Consecuencias

- Ningún cambio de sonido, estado ni parámetros: el código del hilo de audio no se toca. Los proyectos y presets anteriores se cargan y suenan igual.
- Sin dependencias nuevas.
- `tools/bench` y la opción `ESCEQ_BUILD_TOOLS` en el job `core` del CI (antes solo la usaba `tools/snapshot`, que necesita el plugin y se sigue compilando solo con él).
- Queda documentado, como limitación de la fase lineal, el pico de trabajo en la llamada que completa un bloque de la convolución, con su medida ([fase-lineal.md](../dsp/fase-lineal.md)). En el uso normal (bloques de 128 muestras o más a 96 kHz, de 256 o más a 192 kHz) cabe en el periodo con margen.
- Las cifras de coste de las demás páginas de `docs/dsp/` se midieron en otros momentos y con otras señales; las de esta ADR son las comparables entre sí.
