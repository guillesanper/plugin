# Saturación

Cómo funciona la saturación de esceQ: una etapa global de saturación suave tras el ecualizador, para dar color armónico, con sobremuestreo contra el aliasing, compensación del drive y mezcla con la señal seca. Código en `dsp/include/esceq/dsp/saturation.h` (`dsp::Saturation`, el motor, y `dsp::Oversampler`, el sobremuestreo). Las decisiones y sus motivos están en la [ADR 0026](../decisiones/0026-saturacion.md), y el comportamiento que verá el usuario, en la sección 5.8 del [plan](../../PLAN.md).

Estado: el motor (bloque 6.7) y su integración en `dsp::PhaseEqualizer`, con los parámetros `sat_mode`, `sat_drive` y `sat_mix` (bloque 6.8), están hechos y medidos: suena en el plugin y se puede automatizar desde el host. Falta la interfaz (bloque 6.9).

## Cadena

```
entrada ─► subir ×2 ─► subir ×2 ─► subir ×2 ─► x ─┬─► c·(tanh(d·x + b) − tanh(b)) ─► bloqueador de continua ─► húmeda ─┐
                                                 │                                                                     ▼
salida ◄── bajar ×2 ◄── bajar ×2 ◄── bajar ×2 ◄──┴───────────────────────────── x + mezcla·(húmeda − x) ◄──────────────┘
```

Todo lo que no es lineal ocurre dentro, a unos 384 kHz: ×8 hasta 50 kHz (44,1 y 48 kHz), ×4 hasta 100 kHz y ×2 por encima (`Saturation::stagesFor()`, con el mismo `sampleRateFactor()` que la fase lineal y la dinámica espectral). Así el aliasing y el coste son casi los mismos a cualquier frecuencia de muestreo.

La señal seca pasa por los mismos filtros, sin la curva: la mezcla se hace dentro, con las dos ya alineadas, y no hace un filtro de peine en agudos. Con la mezcla a 0, la salida es la entrada por los filtros de sobremuestreo.

## Curvas

| Modo | Curva | Armónicos |
|---|---|---|
| Off | No se ejecuta | — |
| Soft | `tanh(d·x)` | Solo impares (la curva es impar exacta) |
| Warm | `tanh(d·x + b) − tanh(b)`, con b = 0,25 | Añade los pares |

d es el drive en lineal (0 a 24 dB). En Warm, la polarización b desplaza el punto de trabajo y la curva pierde la simetría; restarle `tanh(b)` deja la salida a cero con la entrada a cero, pero la asimetría sigue dando continua con señal, que quita el bloqueador. Con un seno de −18 dBFS a 1 kHz (medido con el motor):

| Drive | Soft: 2.º / 3.º armónico | Warm: 2.º / 3.º armónico |
|---|---|---|
| 0 dB | −158 / −57,6 dB | −36,3 / −59,3 dB |
| 6 dB (por defecto) | −171 / −45,7 dB | −30,5 / −47,4 dB |
| 12 dB | −167 / −34,1 dB | −25,1 / −35,6 dB |
| 24 dB | −163 / −15,5 dB | −20,9 / −16,0 dB |

b = 0,25 deja el segundo armónico unos 15 dB por encima del tercero con drive bajo, que es lo que se espera de una saturación "cálida", y los dos parejos con mucho drive.

La tanh es propia (`fastTanh` en `saturation.cpp`): sin ramas, para que el compilador la vectorice, y con el mismo resultado en todos los sistemas. Por encima de 0,25 es `(1 − e)/(1 + e)` con `e = e^(−2|x|) = 2^k·2^f` (k entero, |f| ≤ 1/2 y un polinomio de Taylor de grado 7 para 2^f); por debajo, la serie de Taylor hasta x⁹. Es impar exacta (el signo se copia al final) y se aparta de la tanh de la biblioteca 1,1·10⁻⁷ como mucho. Con GCC, `saturation.cpp` se compila con `-fno-trapping-math`, que deja calcular las dos ramas del `?:` y vectorizar, sin cambiar ningún resultado.

## Bloqueador de continua

Un polo a 5 Hz, a la frecuencia de dentro y en double: `y[n] = x[n] − x[n−1] + r·y[n−1]`, con `r = e^(−2π·5/fs_dentro)`. Va en los dos modos, para que pasar de Soft a Warm no lo encienda de golpe; en Soft no quita nada a una entrada sin continua. Solo actúa sobre la húmeda: la seca conserva su continua, si la trae, y sus graves. Con Warm, el drive máximo y un seno de 100 Hz a −6 dBFS (lo que más continua da), tras 1 s queda a −192 dB (IIR) y −195 dB (fase lineal).

## Compensación del drive

La salida húmeda se multiplica por `c = 1/g`, con g la ganancia que la curva da a un seno de −18 dBFS: su valor eficaz a la salida (sin la continua, que quita el bloqueador) dividido por el de la entrada. Como la curva no tiene memoria, basta un periodo: 256 puntos de fase, 256 tanh en double (`Saturation::compensationGain()`, unos microsegundos), en el hilo de audio y sin asignar, cada vez que cambia el drive o la curva. Con cualquier drive, ese seno sale al nivel al que entra: medido, a +0,0002 dB en los dos modos y con los dos filtros (el resto es el rizado de los filtros); el criterio pide ±0,5 dB.

## Filtros de sobremuestreo

Cada etapa ×2 es un filtro de media banda, el mismo al subir (con ceros intercalados y ganancia 2) y al bajar. Las bandas, en función de fs (la frecuencia de muestreo de fuera):

| Etapa | Plana hasta | Atenúa desde | Por qué |
|---|---|---|---|
| 1.ª (a 2·fs) | 0,41·fs (18 kHz a 44,1 kHz) | 0,59·fs | Lo que pasa de 0,59·fs se pliega al bajar por encima de 0,41·fs: solo cae aliasing entre 18 y 24 kHz, y atenuado |
| 2.ª (a 4·fs) | 0,59·fs | 1,41·fs | Lo que se plegaría al bajar sobre lo que la 1.ª deja pasar |
| 3.ª (a 8·fs) | Los mismos coeficientes que la 2.ª, con las bandas al doble: 1,18·fs y 2,82·fs | | Lo que se cuela entre 2,59 y 2,82·fs se pliega a la transición de la 2.ª y acaba en la banda que la 1.ª atenúa |

**IIR** (modo de fase IIR): dos ramas de secciones pasa todo de primer orden en z⁻², `H(z) = (A0(z²) + z⁻¹·A1(z²))/2` (Valenzuela y Constantinides, 1983), con coeficientes elípticos calculados con las funciones theta de Jacobi para el número de coeficientes y la semianchura de la transición: 8 coeficientes y 0,045 en la primera (−103 dB) y 6 y 0,1025 en las demás (−105 dB). Son complementarios en potencia: el rizado en la banda de paso es de 10⁻⁹ dB. Cada rama procesa a la frecuencia de su entrada, y los dos canales van en el mismo bucle para que sus recurrencias se solapen. No tienen latencia que comunicar, pero sí retardo de grupo: unas 6 muestras de fuera en graves (0,13 ms a 48 kHz) y 11 a 18 kHz. Como la seca pasa por los mismos filtros, la mezcla no lo nota.

**Fase lineal** (los tres modos lineales): FIR de media banda diseñados con Parks-McClellan con las mismas bandas, de 71 coeficientes en la primera etapa (−106 dB) y 31 en las demás (−110 dB); en un media banda, el coeficiente central vale 1/2 y los de distancia par al central, 0, así que solo se multiplican los de la rama par (36 y 16). Se calculan como convolución por bloques (la memoria y el tramo seguidos, con el bucle de dentro sobre las salidas), que el compilador vectoriza y suma siempre en el mismo orden. Subir y bajar retrasan 47 muestras con tres etapas (hasta 50 kHz: 0,98 ms a 48 kHz), 43 con dos (hasta 100 kHz) y 35 con una: el retardo de los FIR redondeado hacia arriba con un pequeño retardo a la frecuencia de dentro, para que sea un número entero de muestras de fuera (`Saturation::latencySamples()`).

Los coeficientes están en `saturation.cpp`, en float. Los tests miden la respuesta del sistema entero con esos valores, no la del diseño.

## Cambios con el audio en marcha

- **Drive y compensación:** rampa lineal de 20 ms en dB. En lineal, el producto del drive por la compensación se pasaba de los dos extremos durante la rampa (una subida de nivel de varios dB al pasar de 6 a 24 dB); en dB va de uno a otro sin pasarse. Los valores lineales solo se recalculan mientras la rampa avanza.
- **Mezcla:** rampa lineal de 20 ms.
- **Soft ↔ Warm:** la polarización va de 0 a 0,25 (o al revés) en 20 ms, con la compensación; el bloqueador ya estaba en marcha.
- **Off ↔ Soft o Warm:** fundido de 20 ms con la entrada tal cual. Al encender desde parado, la memoria empieza a cero; al apagar, terminado el fundido, se borra y `process()` deja de tocar las muestras, y la salida es la entrada bit a bit.
- **Filtros (IIR ↔ fase lineal) y frecuencia de muestreo:** borran la memoria y cambian la latencia de golpe. Quien lo use lo hace en silencio, como el cambio de modo de fase. Por la misma razón, en fase lineal, encender y apagar cambian la latencia que hay que comunicar (de 0 a 47 muestras a 48 kHz), y el fundido con la entrada sin retrasar solo vale bajo ese silencio: `PhaseEqualizer` lo hace así (ver «En el procesador»).

Ningún cambio hace chasquidos: con un seno de 50 Hz y todos esos cambios seguidos, la mayor diferencia entre dos muestras no pasa de la de los tramos estables (test "los cambios van con rampa y sin chasquidos").

## En el procesador

`dsp::PhaseEqualizer` lleva una `Saturation` al final (bloque 6.8, nota en la [ADR 0026](../decisiones/0026-saturacion.md)), que recibe con `setSaturation()` los ajustes de `sat_mode`, `sat_drive` y `sat_mix` (`core::saturationSettings()`, la mezcla de 0 a 100 % pasada a 0..1):

- **Lugar en la cadena:** tras el IIR o el FIR (con la dinámica) y tras la dinámica espectral; antes de la ganancia de salida y de la compensación automática, que no la tiene en cuenta (compensa la curva; la saturación compensa su drive). La salida es, bit a bit, la de una `Saturation` sola sobre la salida del ecualizador sin ella.
- **Filtros:** los del modo que suena, IIR en IIR y de fase lineal en Low, Medium y High. Cambian (con `setFilter()`, que borra la memoria) en el silencio del cambio de modo.
- **Latencia:** la del modo y la de la dinámica espectral, más `Saturation::latencySamples()` mientras `sat_mode` no esté en Off (`PhaseEqualizer::saturationLatency()`): 0 en IIR; en fase lineal, 47 muestras a 44,1 y 48 kHz, 43 a 96 y 35 a 192. La mezcla no la cambia: con la mezcla al 0 % la salida pasa por los filtros igual.
- **Encender y apagar:** en IIR, el fundido de 20 ms del motor con la entrada tal cual, sin silencio (no cambia la latencia; mientras dura, se mezclan la entrada y la salida por los filtros IIR, con unas pocas muestras de retardo de grupo entre ellas). En fase lineal cambia la latencia, así que se hace como el cambio de modo: 10 ms a silencio, el cambio (con la memoria borrada y sin fundido: la saturación entra o sale entera en el silencio), silencio mientras se llena la latencia nueva y 10 ms de subida; el hilo de control avisa al host. Si se vuelve a pedir lo que suena antes del cambio (Off, Soft y Off otra vez), la bajada se deshace. Entre Soft y Warm, el drive y la mezcla: las rampas de 20 ms del motor, sin silencio, en todos los modos.
- **Bypass:** como la dinámica espectral, un fundido de 10 ms con su entrada retrasada lo que la saturación (47 muestras en fase lineal; nada en IIR), así que la latencia no cambia y, con el bypass completo, la salida es la entrada retrasada, bit a bit. La saturación sigue en marcha durante el bypass.
- **Con Off**, terminado el fundido, la etapa no se ejecuta: el ecualizador es bit a bit el de antes, con la misma latencia en los cuatro modos. Comparado con el código anterior a la integración (el commit `a12f972`): un programa que pasa 60 000 muestras de ruido por `PhaseEqualizer`, con una campana, una banda dinámica en Mid, un corte y una banda dinámica en Side escuchando el sidechain (en otra tanda, espectral), a 44,1, 48 y 96 kHz, en los cuatro modos, con bloques irregulares, el bypass puesto y quitado, un cambio de modo y una banda movida, y que con el código nuevo llama además a `setSaturation()` con Off y otros drive y mezcla: las 24 salidas son iguales bit a bit, con las mismas latencias. Con Soft, las 24 cambian (el programa distingue).
- **No asigna:** su memoria (el motor y las líneas de retardo de la seca) se reserva en `prepare()`; `setSaturation()` es segura en tiempo real (calcula la compensación si cambia el drive: unos microsegundos).

## Medidas

En el equipo de desarrollo (Linux, x86-64, Xeon a 2,1 GHz en una máquina virtual, Release), con `esceq_bench --saturation` (7 repeticiones; la máquina da un ±20 % entre tandas, así que van rangos) y `tests/dsp/saturation_test.cpp`.

### Alternativas contra el aliasing

La curva Soft sin compensar, con 12 dB de drive y a 48 kHz. Aliasing de un seno de 10 kHz: por Parseval, la potencia que no es la continua ni un armónico por debajo de Nyquist (10 y 20 kHz), relativa a la fundamental; con 4800 muestras todo cae en un número entero de periodos. Respuesta en agudos con señal pequeña (−60 dBFS, la húmeda dividida por el drive): la húmeda sola y la mezcla al 50 %, a 18 kHz y relativas a 1 kHz. El coste es el de cada alternativa con la tanh de la biblioteca en double, no el del motor.

| Alternativa | Aliasing a −18 / −6 / 0 dBFS (dB) | Húmeda a 18 kHz | Mezcla al 50 % | % de un núcleo |
|---|---|---|---|---|
| Sin sobremuestreo | −34,1 / −15,2 / −10,5 | 0 | 0 | 0,15 |
| ADAA de primer orden | −45,6 / −24,9 / −19,1 | −8,32 dB | −4,42 dB | 0,25–0,45 |
| ×2 IIR | −127 / −53,3 / −31,7 | 0 | 0 | 0,33–0,40 |
| ×2 IIR + ADAA | −135 / −80,8 / −54,7 | −1,60 dB | −1,14 dB | 0,57–0,65 |
| ×4 IIR | −131 / −103 / −58,3 | 0 | 0 | 0,68–1,04 |
| ×4 IIR + ADAA | −130 / −114 / −78,8 | −0,38 dB | −0,28 dB | 1,13–1,40 |
| ×8 IIR | −130 / −114 / −110 | 0 | 0 | 1,38–1,80 |
| ×4 fase lineal | −133 / −104 / −58,7 | 0 | 0 | 0,76–1,15 |
| ×8 fase lineal | −279 / −122 / −116 | 0 | 0 | 1,33–2,12 |

- **Sin sobremuestreo**, el tercer armónico (30 kHz) se pliega a 18 kHz con todo su nivel. **ADAA** lo atenúa poco (su efecto es un promedio de dos muestras: −6,5 dB a 30 kHz) y, por la misma razón, es un filtro paso bajo para la señal (−8,3 dB a 18 kHz) con media muestra de retardo, que con la seca hace un peine.
- Con sobremuestreo, el aliasing es el que se pliega *dentro*: los armónicos que pasan de la mitad de la frecuencia de dentro. Comparado con un sobremuestreo ideal (en frecuencia), el de ×4 es el mismo (−58 dB a 0 dBFS) y el de ×8 ideal da −125: con estos filtros, ×8 queda en −110 por el redondeo de float. Lo limita la curva, no los filtros.
- **El nivel de la entrada decide:** con 12 dB de drive, `d·x` llega a 4 a 0 dBFS, y tanh(4·sen) es casi una onda cuadrada, con armónicos que caen despacio. ×4 cumple el criterio de −80 dB hasta unos −4 dBFS de pico (−78 dB a −3 dBFS); solo ×8 lo cumple a cualquier nivel.
- **ADAA sobre ×4** casi llega a 0 dBFS (−79), pero cuesta como ×8 y pierde agudos.

### El motor

A 48 kHz (×8), con 12 dB de drive:

| Curva | Filtros | Aliasing a −18 / −6 / 0 dBFS | Seno de −18 dBFS a 1 kHz | Mezcla al 0 %, 18 kHz |
|---|---|---|---|---|
| Soft | IIR | −131 / −114 / −110 dB | +0,0002 dB | 0,0000 dB |
| Soft | fase lineal | −139 / −122 / −116 dB | +0,0002 dB | 0,0000 dB |
| Warm | IIR | −130 / −114 / −110 dB | +0,0002 dB | 0,0000 dB |
| Warm | fase lineal | −136 / −121 / −115 dB | +0,0002 dB | 0,0000 dB |

Con la mezcla al 0 %, de 20 Hz a 18 kHz y de 44,1 a 192 kHz, la respuesta se aparta de 0 dB menos de 0,001 dB; en fase lineal, la salida es además la entrada retrasada 47 muestras, con un error por debajo de −80 dB.

### Coste

Instancia estéreo, 12 dB de drive, mezcla al 50 %, en % de un núcleo: las medianas de Soft y Warm con bloques de 32 y de 256 (el tamaño de bloque apenas influye: se procesa en tramos de 64 muestras), en dos tandas:

| Filtros | 44,1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| IIR (GCC) | 0,8–1,1 | 0,9–1,2 | 0,9–1,2 | 1,1–1,2 (una tanda, 2,3) |
| Fase lineal (GCC) | 0,65–0,8 | 0,7–1,3 | 0,8–1,6 | 1,0–1,3 |
| IIR (Clang) | 0,9–1,0 | 0,9–1,1 | 1,0–1,6 | 1,0–1,4 |
| Fase lineal (Clang) | 0,8–1,2 | 0,9–1,2 | 1,0–1,9 | 1,0–1,4 |

Con el ecualizador (`esceq_bench --scenario mezcla,saturacion,mezcla-sat --rate 44100,48000,192000 --repeat 7`, bloques de 256; `saturacion` es la saturación sola, Warm a 12 dB con la mezcla al 50 %, y `mezcla-sat`, la mezcla de 8 bandas con Soft a 6 dB y al 100 %), en % de un núcleo, la mediana de 7 medidas:

| Escenario, compilador | IIR | Low | Medium | High |
|---|---|---|---|---|
| `mezcla`, GCC, 48 kHz | 0,61 | 0,74 | 0,78 | 0,85 |
| `mezcla-sat`, GCC, 48 kHz | 1,56 | 1,45 | 1,57 | 1,65 |
| `saturacion`, GCC, 48 kHz | 0,90 | 0,95 | 1,00 | 1,08 |
| `mezcla`, Clang, 48 kHz | 0,73 | 0,82 | 0,90 | 0,93 |
| `mezcla-sat`, Clang, 48 kHz | 1,73 | 1,76 | 1,82 | 1,88 |
| `mezcla`, GCC, 192 kHz | 1,83 | 2,15 | 2,42 | 3,17 |
| `mezcla-sat`, GCC, 192 kHz | 2,78 | 3,25 | 3,64 | 4,39 |

La saturación suma a la mezcla del 0,7 al 1,0 % a 44,1 y 48 kHz con los dos compiladores y del 0,95 al 1,2 % a 192 kHz: lo que costaba el motor solo. `saturacion` incluye, en los lineales, el camino del FIR (la identidad retrasada). La llamada más lenta no cambia de escala (de 0,10 a 0,22 ms a 48 kHz con bloques de 256, que duran 5,3 ms). Con bloques de 32 y de 1024 muestras, lo mismo (de 1,6 a 1,8 % con la mezcla a 48 kHz). La máquina es ruidosa: las medidas varían un ±20 %.

El criterio pide menos del 2 % a 48 kHz. La primera versión, con la tanh de la biblioteca, los canales por separado y los FIR muestra a muestra, costaba del 1,6 al 1,9 % en IIR y del 2,5 al 3 % en fase lineal; de ahí la tanh propia (la de la biblioteca se llevaba un 0,65 % ella sola), los canales en el mismo bucle y la convolución por bloques. Con Off, nada.

## Verificación

`tests/dsp/saturation_test.cpp`:

| Test | Qué comprueba |
|---|---|
| latencia de cada filtro | Las etapas con cada frecuencia de muestreo y la latencia: 0 en IIR; 47, 43 y 35 en fase lineal |
| con Off la salida es la entrada bit a bit | Antes de encenderla y después de apagarla, terminado el fundido |
| Soft solo da armonicos impares y Warm anade el segundo | Soft: los pares por debajo de −100 dB a −18 y a 0 dBFS; Warm: el segundo por encima de −60 dB con 12 dB de drive |
| aliasing de un seno de 10 kHz con 12 dB de drive a 48 kHz | Por debajo de −80 dB a −18, −6 y 0 dBFS, en los dos modos y con los dos filtros |
| la continua desaparece en 1 s | Por debajo de −100 dB con Warm y el drive máximo |
| un seno de -18 dBFS a 1 kHz sale al mismo nivel con cualquier drive | ±0,5 dB (en la práctica, 0,01) de 0 a 24 dB |
| la compensacion del drive | `compensationGain()` contra un periodo de 100 000 puntos |
| con la mezcla a 0 la salida es la entrada por los filtros | Menos de 0,1 dB de 20 Hz a 18 kHz de 44,1 a 192 kHz; en fase lineal, la entrada retrasada |
| no depende del tamano de bloque | Bloques de 1 a 4096 muestras, bit a bit, con rampas y fundido en curso |
| los cambios van con rampa y sin chasquidos | Encender, cambiar el drive, la curva y la mezcla, y apagar |
| un NaN o un infinito no la estropea | Silencio en el tramo que lo trae y luego suena |
| reset vuelve a empezar de cero | |
| las imagenes al subir quedan por debajo de -100 dB | `Oversampler` con 1, 2 y 3 etapas, IIR y fase lineal |
| las llamadas del hilo de audio no asignan | Con RealtimeSanitizer (job `rtsan` del CI) |

La integración la comprueban `tests/dsp/phase_equalizer_test.cpp` (con Off, la latencia y el sonido de siempre en los cuatro modos; la latencia sumada a la del modo y medida con un impulso a 44,1, 48, 96 y 192 kHz, sola y con la dinámica espectral; el lugar en la cadena y los filtros, bit a bit contra una `Saturation` sola; encender y apagar en IIR sin hueco y en fase lineal con el silencio del cambio de modo, y de Soft a Warm sin hueco, sin chasquidos; cambiar de modo con ella encendida; bloques de 1 a 4096 muestras, bit a bit; el bypass) y `tests/plugin/saturation_test.cpp` (con Off, la latencia, la cola y el sonido de antes en los cuatro modos; la latencia comunicada al host es la medida en los cuatro modos y a 44,1, 48, 96 y 192 kHz; los proyectos de las versiones 1 a 3 y los presets anteriores la dejan en Off; viaja con el estado; el aviso al host al encenderla en fase lineal, y en IIR, sin cambio; la automatización de los tres parámetros llega al audio sin chasquidos; el bypass en fase lineal). En `core`, los parámetros: su posición, sus valores por defecto, su texto, su traducción, los presets y los estados anteriores. Las llamadas de `PhaseEqualizer` las vigila RealtimeSanitizer en todos sus tests.

## Limitaciones conocidas

- Entre 0,41·fs y fs/2 (de 18 a 22 kHz a 44,1 kHz) la respuesta cae y puede aparecer algo de aliasing atenuado: es la banda de transición de la primera etapa.
- En fase lineal, encender o apagar la saturación (también con un preset que lo haga, o con la automatización de `sat_mode`) deja un hueco: a 48 kHz, 10 ms de bajada, el silencio mientras se llena la latencia nueva (27,6 ms en Low, 97 en Medium y 363,6 en High) y 10 ms de subida. No se puede pasar sin él de una latencia a otra. En IIR, no.
- En IIR, la fase de la señal cambia en agudos (11 muestras de retardo de grupo a 18 kHz frente a 6 en graves) aunque la mezcla esté a 0: los filtros de sobremuestreo están en la cadena mientras la saturación no esté en Off.
- La compensación iguala el nivel de un seno de −18 dBFS. Con señales mucho más fuertes y mucho drive, la salida queda por debajo de la entrada, porque la curva limita.
- El bloqueador quita también la continua (y lo que haya por debajo de unos 5 Hz) de la parte húmeda; la seca la conserva.

## Referencias

- J. Parker, V. Zavalishin y E. Le Bivic, *Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution*, DAFx-16, 2016.
- S. Bilbao, F. Esqueda, J. Parker y V. Välimäki, *Antiderivative Antialiasing for Memoryless Nonlinearities*, IEEE Signal Processing Letters 24(7), 2017.
- A. Valenzuela y A. Constantinides, *Digital Signal Processing Schemes for Efficient Interpolation and Decimation*, IEE Proceedings G 130(6), 1983.
- P. P. Vaidyanathan, *Multirate Systems and Filter Banks*, Prentice Hall, 1993.
- J. H. McClellan, T. W. Parks y L. R. Rabiner, *A Computer Program for Designing Optimum FIR Linear Phase Digital Filters*, IEEE Transactions on Audio and Electroacoustics 21(6), 1973.
