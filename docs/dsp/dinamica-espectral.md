# Dinámica espectral

Cómo funciona la dinámica espectral de esceQ: una variante de la banda dinámica que, dentro de su zona, baja o sube **solo las frecuencias que pasan del umbral**. Código en `dsp/include/esceq/dsp/spectral_dynamics.h` (`dsp::SpectralDynamics`, el motor) y `dsp/include/esceq/dsp/phase_equalizer.h` (su integración en el ecualizador). Las decisiones y sus motivos están en las ADR [0024](../decisiones/0024-dinamica-espectral.md) y [0025](../decisiones/0025-dinamica-espectral-en-el-procesador.md), y el comportamiento que verá el usuario, en la sección 5.7 del [plan](../../PLAN.md).

Estado: el motor (bloque 6.4) y su integración en `dsp::PhaseEqualizer`, con el parámetro `bNN_spectral` (bloque 6.5), están hechos: suena en el plugin en los cuatro modos de fase. Falta la interfaz (bloque 6.6): hasta entonces se enciende desde la automatización del host.

## Cadena

```
entrada del EQ (o sidechain) ─► ventana de Hann ─► FFT ─► energía en 1/6 oct ─► envolvente por bin ─► curva ─► × forma del detector ─┐
                                                                                                                                    │ (por banda,
                                                                                                                                    ▼  suma en dB)
salida del EQ ─► raíz de Hann ─► FFT ─────────────────────────────────────────────► × máscara L/R ─► × máscara M/S ─► FFT inversa ─► raíz de Hann ─► suma solapada ─► salida
```

Una sola transformada para todas las bandas. Por cada salto se hacen, sean cuantas sean las bandas espectrales y sus canales:

| FFT compleja de N puntos | Qué lleva |
|---|---|
| 1 de la señal | L en la parte real y R en la imaginaria |
| 1 del detector interno, si alguna banda lo escucha | L y R de la entrada del ecualizador (o de lo que se le dé como `detectorInput`) |
| 1 del sidechain, si alguna banda lo escucha | Sus dos canales |
| 1 inversa | L y R de la salida |

Como las dos señales son reales, cada una se separa de la transformada conjunta con `A[k] = (Z[k] + Z*[N−k])/2` y `B[k] = −i·(Z[k] − Z*[N−k])/2`, y M y S salen de A y B porque la FFT es lineal. En mono cuesta lo mismo que en estéreo (la parte imaginaria va a cero).

## STFT

- **N = 2048 puntos** hasta 50 kHz, 4096 hasta 100 kHz y 8192 hasta 200 kHz (`sampleRateFactor`, el mismo escalado que la fase lineal): bins de 23,4 Hz a 48 kHz y 21,5 Hz a 44,1, y la misma resolución en hercios y en milisegundos a 96 y 192 kHz.
- **Salto de N/4** (10,7 ms a 48 kHz, 11,6 a 44,1) y **ventana raíz de Hann periódica** en el análisis y en la síntesis. Con un solapamiento del 75 %, la suma de los cuadrados es 2 en todas las muestras, así que la síntesis multiplica por `1/(2N)` y, sin modificar el espectro, la salida es la entrada retrasada.
- **Latencia: N + N/4 muestras** (2560: 53,3 ms a 48 kHz, 58,0 a 44,1). Cada trama empieza cuando llega su última muestra, se calcula durante el salto siguiente (ver «Cálculo repartido») y su salida se suma a sus N muestras en el anillo de salida, que se lee N + N/4 por detrás. Sin repartir, bastaría con N (lo que tenía el bloque 6.4); con menos de N (N − N/4, por ejemplo) quedaría fuera la última trama de cada muestra: en el primer cuarto de cada ventana la suma de los cuadrados se quedaría en 1,5, con un rizado de hasta 2,5 dB (un error de −12 dB).
- **Por debajo del umbral, o sin bandas**, la salida es la entrada retrasada con el error de redondeo de las FFT en float: −139 dB en L y −129 dB en R (la parte imaginaria), de 44,1 a 192 kHz.

## Cálculo repartido

Calcular una trama entera de una vez, en la llamada que completa un salto, deja picos de CPU: con 24 bandas a 48 kHz esa llamada tardaba 0,42–0,53 ms, y a 192 kHz 1,8–1,9 ms, más que un bloque de 256 muestras (1,33 ms). Por eso ([ADR 0025](../decisiones/0025-dinamica-espectral-en-el-procesador.md)) la trama se calcula en pasos, en este orden:

1. La señal: ventana y FFT.
2. Cada fuente del detector que haga falta (la entrada y el sidechain): ventana, FFT y niveles de 1/6 de octava.
3. Cada banda activa: envolvente, curva y suma a la máscara.
4. Cada una de las cuatro máscaras (L, R, M y S), de dB a lineal: un `pow` por bin, lo más caro de la síntesis.
5. La síntesis: aplicar las máscaras, la FFT inversa y la suma solapada.

Con 24 bandas y las dos fuentes son 32 pasos. Al empezar la trama se fijan los ajustes con los que se calcula (los de las bandas, el número de canales y el de cada fuente) y los pasos; durante el salto siguiente, cada llamada hace los que le tocan por la parte del salto que ha llegado, `⌈pasos · muestras del salto / salto⌉` en total, y la que lo completa termina la trama antes de empezar la siguiente. Para que la trama no se sobrescriba mientras tanto, los anillos de la señal, de las fuentes y de la salida son de 2N muestras.

El resultado es bit a bit el del cálculo de una vez retrasado un salto (comparado con el motor del bloque 6.4 con 24 bandas, bloques de 1, 64 y 4096 muestras a 48 kHz y de 333 a 96 kHz, y con una banda apagada a mitad del audio), y sigue sin depender del tamaño de bloque. Un cambio de ajustes actúa en la primera trama que empieza después, como antes.

## Detector

Para cada banda, con su fuente (la entrada o el sidechain) y su canal:

1. **Ventana de Hann** (no la raíz), que deja menos energía fuera del lóbulo principal.
2. **Energía de cada bin en 1/6 de octava a su alrededor**, de `f·2^(−1/12)` a `f·2^(1/12)`, con un bin a cada lado como mínimo (hasta unos 400 Hz a 48 kHz son 3 bins; a 1 kHz, 5; a 10 kHz, 50). Se suma con sumas acumuladas, en un recorrido.
3. **Calibrada para que un seno lea su nivel de pico**: con la ventana de Hann, un seno de amplitud A en el centro de un bin da `|X| = A·N/4`, y su lóbulo principal suma 1,5 veces esa energía, así que el nivel es `√(energía · 16 / (1,5·N²))`. Como se suma la energía de varios bins, un seno entre dos bins (el peor caso) lee lo mismo: la suma de 3 bins recoge el 98 % de su energía (−0,09 dB).
4. **Canal**, como en la banda dinámica ([ADR 0007](../decisiones/0007-canal-por-banda.md)): Stereo escucha el máximo de L y R en cada bin (detección enlazada); Left, Right, Mid y Side, ese canal o la suma `(L+R)/2` y la diferencia `(L−R)/2` de la fuente. Una fuente mono se escucha tal cual.

Sumar la energía de 1/6 de octava es el suavizado en frecuencia contra el ruido musical que pide la ADR, pero hecho en el nivel y no en la ganancia (ver «Ruido musical»). Con ruido de banda ancha, el nivel es el de una banda de 1/6 de octava, así que un ruido rosa lee lo mismo en todo el espectro.

## Envolvente, curva y máscara

- **Envolvente por bin**, la de `EnvelopeFollower` (pico con ataque y relajación separados), por tramas: `c = e^(−salto/τ)`, con `τ = max(tiempo, salto)`. Los tiempos menores de un salto (10,7 ms) cuentan como un salto. Con los **tiempos automáticos** ([ADR 0016](../decisiones/0016-tiempos-automaticos-en-la-dinamica.md)), cada bin usa los de su frecuencia (el bin 0, los del 1) y la etapa lenta, con ese mismo mínimo.
- **Curva de ganancia** de la sección 4.2 (umbral, rango y rodilla), la de `dynamicGainDb`, calculada en float y sin ramas para que el compilador la vectorice, con su propio logaritmo (`decibels`: serie de atanh sobre la mantisa, error por debajo de 1e-4 dB y el mismo resultado en todos los sistemas). Por debajo del principio de la rodilla, 0 dB; si ningún bin de la banda llega ahí, la banda no hace nada más en esa trama.
- **Forma de la banda**: la ganancia de cada bin se multiplica por la magnitud del filtro del detector de la banda (`designDetector`: paso banda en las campanas, paso bajo o alto de 12 dB/oct en los shelves, 1 en tilt), normalizada a 1 en su máximo. Se recalcula solo si cambian el tipo, la frecuencia, el Q o el algoritmo (la ganancia no influye).
- **Varias bandas**: las del mismo dominio se suman en dB (Stereo suma en L y en R; Left, Right, Mid y Side, en el suyo) y se aplican a la señal: primero L y R y luego M y S. Si solo hay bandas Stereo (o el audio es mono), los dos canales reciben la misma ganancia real y simétrica y se aplica sin separarlos.
- **Medidor**: `gainDb(banda)` es la ganancia del bin que más cambia en la última trama, con signo, y `binGainsDb(banda)`, la de cada bin (la forma de la máscara, para el sombreado del bloque 6.6).

## Cambios con el audio en marcha

El motor no tiene fundidos propios: un cambio de ajustes actúa en la trama siguiente, y el solapamiento de las tramas lo funde en N muestras (cada muestra de salida es la suma de cuatro tramas ponderadas por la ventana). Al encenderse una banda, sus envolventes empiezan en cero y la reducción crece con el ataque. Medido con un tono de 200 Hz reducido 12 dB: encender la banda, llevar el umbral de −30 a 0 dB de golpe y apagarla dejan los agudos espurios (por encima de 8 kHz) en **−122 dB**, cuando el límite de los tests de la banda dinámica es −60 dB.

El cambio de latencia al encender la primera banda espectral, con su fundido a silencio, es cosa de `PhaseEqualizer` (ver «En el ecualizador»).

Valores no finitos: si una trama de la señal tiene un NaN o un infinito, no suena (su parte de la salida se queda a cero) y las demás sí, así que la salida baja alrededor del valor malo durante algo menos de 2N muestras (las cuatro tramas que lo contienen) y vuelve sola. Si lo tiene una fuente del detector, se escucha silencio en esa trama y las envolventes de las bandas que la escuchan vuelven a cero.

## En el ecualizador

`dsp::PhaseEqualizer` lleva un `SpectralDynamics`, con la memoria reservada en `prepare()`, y lo hace sonar mientras alguna banda tenga `dynamics.spectral` (el interruptor `bNN_spectral`, que `core/band_model` traduce), en los cuatro modos de fase ([fase-lineal.md](fase-lineal.md)):

- **Qué hace cada banda.** Mientras suena la dinámica espectral, una banda espectral llega al `Equalizer` sin dinámica (su parte estática sigue en el IIR o en el FIR) y al motor con ella; las demás, al revés. Sin ninguna banda espectral, el `Equalizer` recibe exactamente lo de antes y el motor no se llama.
- **Dónde.** Tras el IIR o el FIR, sobre su salida. Su detector escucha la entrada del ecualizador y el sidechain retrasados como el audio: en IIR, tal cual (una copia de la entrada antes del IIR, en tramos de hasta 512 muestras); en fase lineal, los retardos que ya tiene el camino del FIR para los detectores de la dinámica.
- **Latencia.** La del modo más `N + N/4`. Cambia solo con el interruptor: al encender la primera banda espectral, o al apagar la última, como el cambio de modo (fundido a silencio en 10 ms, el cambio, silencio mientras se llena la latencia nueva y fundido de entrada; el motor empieza de cero); el hilo de control avisa al host ([ADR 0021](../decisiones/0021-aviso-de-latencia-sin-temporizador.md)). Apagar la banda, su dinámica o poner el rango a 0 no la cambia. Las demás bandas espectrales entran y salen sin hueco: la dinámica normal se funde en el `Equalizer` y la máscara entra en la trama siguiente.
- **Bypass.** Fundido de 10 ms con la señal seca retrasada lo que la máscara (y el FIR, en fase lineal): la latencia no cambia, y con el bypass completo la salida es la entrada retrasada, bit a bit.
- **Medidor.** `PhaseEqualizer::dynamicGainDb(banda)`, que el procesador publica en sus 24 atómicos, es en una banda espectral el `gainDb()` del motor (el bin que más cambia). `spectralDynamics()` da el motor, para que la interfaz lea las máscaras (bloque 6.6).

Con todos los interruptores apagados, `PhaseEqualizer` es bit a bit el de antes de la dinámica espectral: comparado con el código del bloque 6.4 (sin integrar), con 6 bandas (dinámicas, con sidechain y en los cinco canales), bloques de 1, 77 y 512 muestras, un bypass a mitad y los cuatro modos, las salidas y las latencias son idénticas.

## Medidas

Todas con `tests/dsp/spectral_dynamics_test.cpp`, a 48 kHz salvo que se diga otra cosa.

### Selectividad

| Caso | Medido | Criterio |
|---|---|---|
| Tono de 1 kHz 20 dB por encima del umbral, rango −6 dB | −5,995 dB | −6 ± 0,5 dB |
| Tono débil a 1/3 de octava (1,26 kHz), por debajo del umbral | −0,0001 dB | menos de 0,2 dB |
| El mismo tono débil con la banda dinámica normal | −3,19 dB | debe cambiar |
| Tonos entre dos bins a 315 Hz, 1011,7 Hz, 2,5, 5 y 12,3 kHz, rango −9 dB | −9 ± 0,5 dB | |
| A 44,1, 96 y 192 kHz | −6 ± 0,5 dB | |

En graves, la máscara no es plana sobre el lóbulo del tono (el detector tiene solo 3 bins) y el tono baja algo menos que el rango: −11,5 dB a 100 Hz y −11,3 dB a 60 Hz con un rango de −12. En un prototipo, un tono débil a 1/3 de octava de uno fuerte a 300 Hz cambiaba 0,19 dB (a 1 kHz, nada): por debajo de unos 200 Hz, la banda espectral separa poco, como dice la ADR.

### Ruido musical

Ruido blanco con el umbral en mitad de su nivel (la máscara baja unos 3 dB de media), entre 2 y 8 kHz, con ataque mínimo y 100 ms de relajación. Se mide cuánto se mueve la ganancia de cada bin de una trama a otra (desviación típica en el tiempo) y de un bin al siguiente:

| Detector | En el tiempo | Entre bins |
|---|---|---|
| Un bin, sin suavizar (prototipo) | 1,86 dB | 1,53 dB |
| 1/6 de octava en el nivel (el motor) | **0,59 dB** | **0,11 dB** |
| Con la relajación mínima: un bin / 1/6 de octava (prototipo) | 2,8 dB / 0,84 dB | 2,4 dB / 0,13 dB |

Suavizar la ganancia a 1/6 de octava, como proponía la ADR, quitaría el ruido musical igual, pero diluye la reducción de un tono, que ocupa unos pocos bins: −3,2 dB en vez de −6 a 1 kHz y −0,7 dB a 5 kHz (prototipo). Suavizar el nivel no la diluye, porque todos los bins a menos de 1/12 de octava del tono leen su energía entera.

### Tiempos

Ráfagas a −6 dBFS sobre un tono de prueba débil de la misma frecuencia, medidas con la ganancia de la salida frente a la entrada en ventanas de un periodo (de 1 ms como mínimo):

| Ajustes | 90 % de la reducción | Recuperación al 90 % |
|---|---|---|
| Ataque 0,05 ms y relajación 1 ms (los dos cuentan como 10,7 ms), 7 kHz, rango agotado | 1 ms tras el principio | 35 ms |
| Ataque 30 ms y relajación 300 ms, 7 kHz | 7 ms | 756 ms (24 dB de exceso con τ = 300 ms: unos 790) |
| Automáticos, 12 dB por encima, ráfaga aislada de 50 ms: 7 kHz / 1 kHz / 100 Hz | 19 / 20 / 37 ms | 29 / 26 / 98 ms (la banda dinámica: 24 / 23 / 71) |

- **La reducción empieza antes que la ráfaga**: baja más de 1 dB 22 ms antes, porque cada trama ve 42,7 ms de señal y su ganancia se aplica a toda ella. Es una anticipación (lookahead) de hasta media ventana, propia de la STFT, que la banda dinámica no tiene.
- Con el rango sin agotar, la reducción tarda unos 20 ms en llegar al 90 % aunque el ataque sea el mínimo: la energía de la ráfaga entra en la ventana de Hann poco a poco.
- Al acabar una ráfaga, su corte brusco se reparte unos milisegundos en la salida (la máscara es un filtro y su respuesta dura); por eso la recuperación se mide desde el último momento en que la ganancia supera el 10 %.

### Coste

`esceq_bench --spectral` mide el motor solo, en estéreo, con ruido a unos −12 dBFS, en un Xeon a 2,1 GHz y con bloques de 256 muestras (de 32 a 4096 apenas cambia). Mediana de 7 a 9 medidas, en % de un núcleo, con GCC 13 (`-O3`) y, entre paréntesis, Clang 20:

| Escenario | 44,1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| 1 banda Stereo comprimiendo | 1,1 (1,5) | 1,1 (1,0) | 2,2–2,5 (2,0) | 5,2–5,5 (4,4) |
| 24 bandas en L/R y M/S, la mitad con tiempos automáticos y 4 con sidechain, casi todos los bins en la zona en que la curva cambia | 3,6–3,8 (2,8) | 3,9–4,2 (3,1) | 7,7–8,2 (6,8) | 16–19 (13,7) |
| Las 24 en reposo (por debajo del umbral) | 1,4–1,6 (1,3) | 1,6–1,8 (1,5) | 3,8 (3,0) | 7,1–8,8 (6,7) |

- El criterio de la sección 5.7 (menos del 5 % con 24 bandas a 48 kHz) se cumple. Lo que no se cumple es que con una banda cueste "casi lo mismo": las FFT son algo más del 1 %, y con 24 bandas manda el cálculo de cada bin de cada banda (envolvente, curva y suma a la máscara, unos 13 ns por bin y banda con GCC). La primera versión, con `log10` en double y ramas, costaba del 5,1 al 5,3 %; con la curva en float, el logaritmo propio y los bucles sin ramas, el 3,9–4,2 %. GCC no vectoriza dos de los bucles y Clang sí (de ahí la diferencia).
- **El coste llegaba a golpes** (bloque 6.4): toda una trama se calculaba en la llamada que completa un salto, 0,13 ms con una banda y 0,46 ms con 24 a 48 kHz, y 0,6 y 2,3–2,6 ms a 192 kHz. El bloque 6.5 la reparte en el salto siguiente (ver «Cálculo repartido» y abajo). Repartir no cambia el coste medio: medidos ahora en la misma máquina, el motor del 6.4 y el repartido cuestan lo mismo con 24 bandas a 48 kHz (3,1–3,3 % y 3,1–3,2 %). La máquina también varía: con 24 bandas a 48 kHz el motor da hoy el 3,2 % y no el 3,9–4,2 % de la tabla.

#### Con el ecualizador

`esceq_bench --scenario espectral,mezcla-esp,mezcla` mide `PhaseEqualizer`, lo que llama el procesador, con bloques de 64 y de 512 muestras (mediana de 7 medidas, GCC 13, % de un núcleo). "espectral" son las 24 bandas de arriba con `dynamics.spectral` (con sus campanas de 0 dB en el IIR o en el FIR); "mezcla-esp", la mezcla de 8 bandas con sus 2 bandas dinámicas espectrales:

| Escenario | Modo | 44,1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|
| 24 espectrales | IIR | 3,5–3,6 | 3,7–3,9 | 7,7–7,9 | 15,8–16,2 |
| 24 espectrales | Low, Medium y High | 3,1–3,5 | 3,4–3,7 | 7,0–7,2 | 14,5–15,3 |
| Mezcla, 2 espectrales | los cuatro | 1,0–1,1 | 1,1–1,2 | 2,3–2,7 | 5,0–6,0 |
| Mezcla, sin espectrales (referencia) | los cuatro | 0,6–0,8 | 0,6–0,9 | 1,2–1,9 | 2,0–3,2 |

El criterio de la sección 5.7 (menos del 5 % con 24 bandas espectrales a 48 kHz) se cumple también con el ecualizador. En IIR cuesta algo más que en fase lineal porque las 24 campanas de 0 dB pasan por el IIR, y en fase lineal van en el FIR, que es la identidad. En la mezcla, pasar sus 2 bandas dinámicas a espectrales cuesta de 0,3 a 0,5 puntos más a 48 kHz: las FFT y las dos bandas del motor, menos la dinámica que dejan de hacer en el IIR.

#### La llamada más lenta

Lo que importa con bloques pequeños es lo que tarda la llamada más lenta frente a lo que dura el bloque. Con el cálculo repartido, cada paso tarda (24 bandas, mediana):

| Paso | 48 kHz | 192 kHz |
|---|---|---|
| La señal (ventana y FFT) | 16 µs | 81 µs |
| Una fuente del detector | 24–27 µs | 115–127 µs |
| Una banda | 8–9 µs | 31–35 µs |
| Una máscara, de dB a lineal | 10 µs | 41 µs |
| La síntesis | 19 µs | 97 µs |

La síntesis con la conversión de las máscaras dentro tardaba 62 y 266 µs: por eso la conversión va en cuatro pasos aparte. Medido llamada a llamada con `PhaseEqualizer` y las 24 bandas espectrales en IIR (4 s de audio):

| Frecuencia y bloque | Dura el bloque | Llamada: p99,9 / la más lenta | Antes, de una vez |
|---|---|---|---|
| 48 kHz, 32 muestras | 0,67 ms | 0,07 / 0,08 ms | 0,42–0,53 ms |
| 96 kHz, 32 muestras | 0,33 ms | 0,21 / 0,56 ms (4 de 9000 llamadas, más que el bloque) | |
| 192 kHz, 64 muestras | 0,33 ms | 0,16 / 0,17 ms | 1,8–1,9 ms (con bloques de 256) |
| 192 kHz, 32 muestras | 0,17 ms | 0,19 / 0,36 ms (27 de 18000, más que el bloque) | |

Con bloques de 64 muestras o más a 192 kHz (y de 32 o más a 48 kHz), ninguna llamada pasa de la duración del bloque. A 192 kHz con bloques de 32, el análisis de una fuente (127 µs) ya ocupa tres cuartos del bloque; repartir más fino habría que hacerlo dentro de una FFT. La columna «llamada más lenta» de `esceq_bench` es la mediana de la más lenta de cada repetición y recoge también las interrupciones del sistema: en esta máquina, ruidosa, da de 0,1 a 1 ms en todos los escenarios, también en los que no tienen dinámica espectral.

## Verificación

`tests/dsp/spectral_dynamics_test.cpp` comprueba:

- Tamaños y latencia (N + N/4) a 44,1, 48, 88,2, 96, 176,4 y 192 kHz, y qué bandas actúan (encendidas, con la dinámica y rango, y en campanas, shelves y tilt).
- El cálculo repartido: con 24 bandas, bloques de 1 a 4096 muestras y saltos de 512, ninguna llamada hace más de `⌈32 · bloque / 512⌉` pasos (2 con bloques de 32), y todas las tramas se terminan. Se cuentan pasos (`stepsProcessed()`), no tiempos.
- Por debajo del umbral, con bandas en los cuatro dominios y sin ninguna, la salida es la entrada retrasada la latencia con un error menor de −90 dB, de 44,1 a 192 kHz y en mono.
- Selectividad (la tabla de arriba), el rango positivo (sube 4 dB ± 0,5), el medidor y la forma de la máscara en el bin del tono.
- La forma del detector: en una campana, la ganancia a una octava del centro es la de su paso banda; en tilt, la misma en todo el espectro; en un shelf de graves, todo por debajo y casi nada a 8 kHz.
- Canales: Left solo baja L; un tono solo en L no mueve una banda Right; un tono centrado lo baja Mid y no Side; Side baja la diferencia y deja la suma; Stereo enlaza los dos canales; en mono, Right y Side no hacen nada.
- Varias bandas del mismo dominio y de los dos se suman en dB.
- Sidechain estéreo y mono; sin sidechain, las bandas externas no actúan; con detección interna, el sidechain se ignora y manda `detectorInput`.
- Con bloques de 1, 7, 32, 100, 511 y 4096 muestras, la salida es bit a bit la misma (con bandas en los cuatro dominios, tiempos automáticos y sidechain).
- Los tiempos, el ruido musical y los cambios bruscos (las tablas de arriba, con sus límites).
- Un NaN o un infinito en la señal o en el sidechain no la estropea, y `reset()` vuelve a empezar de cero.
- El job `rtsan` del CI pasa todos estos tests con RealtimeSanitizer: `process`, `setBand` y `reset` no asignan ni bloquean.

La integración la comprueban `tests/dsp/phase_equalizer_test.cpp` (sin bandas espectrales, la latencia y el sonido de siempre en los cuatro modos; la latencia sumada a la del modo y medida con un impulso a 44,1, 48, 96 y 192 kHz, en estéreo y en mono; la máscara en lugar de la dinámica de la banda, con su medidor; sin chasquidos ni huecos al encender y apagar la primera banda y las demás; bloques de 1 a 4096 muestras, bit a bit; el bypass; y, en fase lineal, el detector retrasado, bit a bit contra el motor solo), `tests/plugin/spectral_test.cpp` (la latencia que conoce el host es la medida en los cuatro modos y a 44,1, 48, 96 y 192 kHz, su aviso al encender la primera banda, sin chasquidos, el interruptor en el audio y en el medidor, y la cola) y `tests/plugin/phase_test.cpp` (apagada, la latencia y el sonido de antes en los cuatro modos). En `core`, el parámetro: su posición, su valor por defecto, su traducción, los presets y los estados anteriores.

## Limitaciones conocidas

- En graves (por debajo de unos 200 Hz), 23 Hz de resolución no separan armónicos cercanos, y el tono baja hasta 0,7 dB menos que el rango: es el precio de una ventana de 42,7 ms (ADR 0024).
- Latencia de 53,3 ms a 48 kHz (58 a 44,1), sumada a la del modo, mientras alguna banda tenga el interruptor encendido, aunque no actúe (ADR 0025). Encender la primera, o apagar la última, deja un hueco breve mientras el host recalcula la compensación.
- Anticipación de hasta media ventana: la reducción empieza hasta 22 ms antes del sonido que la provoca, y la salida de un corte brusco se reparte unos milisegundos.
- A 192 kHz con bloques de 32 muestras, algunas llamadas tardan más que el bloque (ver «La llamada más lenta»).

## Referencias

- J. B. Allen, *Short Term Spectral Analysis, Synthesis, and Modification by Discrete Fourier Transform*, IEEE TASSP 25(3), 1977. STFT y reconstrucción por suma solapada.
- D. Griffin y J. Lim, *Signal Estimation from Modified Short-Time Fourier Transform*, IEEE TASSP 32(2), 1984. Ventana en el análisis y en la síntesis; con la raíz de Hann y N/4, la suma de los cuadrados es constante.
- O. Cappé, *Elimination of the Musical Noise Phenomenon with the Ephraim and Malah Noise Suppressor*, IEEE TSAP 2(2), 1994. El ruido musical y el suavizado de las ganancias.
- D. Giannoulis, M. Massberg, J. Reiss, *Digital Dynamic Range Compressor Design — A Tutorial and Analysis*, JAES 60(6), 2012. Envolvente y curva de ganancia, las de la banda dinámica ([dinamica.md](dinamica.md)).
