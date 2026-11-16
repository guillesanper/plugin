# Compensación automática de ganancia

Cómo calcula `dsp::autoGainDb` (`dsp/auto_gain.h`) la ganancia que deja igual la sonoridad al ecualizar, para poder comparar con el bypass sin que "más fuerte" parezca "mejor". Las decisiones y sus alternativas están en la [ADR 0015](../decisiones/0015-compensacion-automatica-de-ganancia.md); cómo se aplica en el plugin, en [arquitectura.md](../arquitectura.md).

## Qué se calcula

La compensación sale de la **curva estática**, no de medir el audio: medir haría que la ganancia subiera y bajara con la música (bombeo), y lo que se quiere es una ganancia fija para cada ajuste de la curva. Se pregunta cuánto cambia la curva la sonoridad de una señal de referencia, el ruido rosa (misma potencia por octava, el espectro medio aproximado de una mezcla), medida como la mide ITU-R BS.1770 (ponderación K):

```
G = −10 · log10( ∫ |K(f)|² · P(f) d(log f)  /  ∫ |K(f)|² d(log f) )      de 20 Hz a 20 kHz
```

- **Ruido rosa en log f.** Su densidad es `1/f`, que en `log f` es constante: el peso de cada punto es solo `|K(f)|²`.
- **Ponderación K** (`dsp::kWeightingPower`): el shelf de unos +4 dB en agudos y el paso alto "RLB" de BS.1770, con los coeficientes que da la norma a 48 kHz, evaluados en cada frecuencia (de 20 Hz a 20 kHz están por debajo de 24 kHz). No depende de la frecuencia de muestreo del audio. Vale +0,691 dB en 997 Hz, lo que compensa el −0,691 de la definición de LKFS.
- **`P(f)`, la potencia de la curva:** `|H(f)|²`, el producto de las bandas en serie, con la respuesta exacta de sus filtros digitales (`dsp::responseAtTangent`, la misma que dibuja la interfaz y que imita la fase lineal). Solo la ganancia estática de cada banda: **la dinámica no se compensa**, porque compensarla anularía su efecto.
- **Canales.** Con bandas en L, R, M o S, la media de la potencia de los dos canales de salida, para dos canales de entrada incorrelados y de la misma potencia (como dos ruidos rosas distintos, o lo que tienen de distinto los dos canales de una mezcla). Sin Mid ni Side, `P = (|H_L|² + |H_R|²)/2`, con el producto de las bandas de cada canal. Con Mid o Side, la matriz de 2×2 de cada banda con su magnitud, igual que en la fase lineal ([fase-lineal.md](fase-lineal.md)), y `P = (ll² + lr² + rl² + rr²)/2`. Para una banda Mid o Side sola, o una Mid y otra Side, es exacto también con la fase del IIR: `P = (1 + |H|²)/2`. En **mono**, el producto de las bandas Stereo, Left y Mid, las que procesan el único canal.
- **Límites.** El resultado se limita a ±12 dB. Si ninguna banda cambia la magnitud (todas desactivadas, campanas, shelves y tilt a 0 dB, pasa todo), es **exactamente 0**, sin errores de redondeo: así la salida con la compensación encendida es bit a bit la de apagada.
- **Frecuencia de muestreo.** El límite superior baja a 0,49·fs si fs no llega a 40,8 kHz. Entre 44,1 y 192 kHz el resultado de una curva típica cambia menos de 0,05 dB (lo que cambian los filtros cerca de Nyquist).

## La rejilla

La integral se calcula con la regla del trapecio en `log2 f`. Una rejilla uniforme no sirve: una campana de Q 30 mide 0,05 octavas, y según caiga entre los puntos se cuenta de más o de menos. Medido contra una rejilla uniforme de 1/1536 de octava, con 1500 curvas aleatorias (de 1 a 8 bandas de cualquier tipo, canal, ganancia de ±30 dB y Q de 0,1 a 30):

| Rejilla | Mediana | Percentil 99 | Peor | Peor con una campana de +30 dB y Q 30 |
|---|---|---|---|---|
| Uniforme, 6 puntos por octava | 0,0006 dB | 3,98 dB | 7,18 dB | 8,53 dB |
| Uniforme, 24 | < 0,0001 dB | 1,31 dB | 3,58 dB | 3,83 dB |
| Uniforme, 96 | < 0,0001 dB | 0,047 dB | 0,34 dB | 0,44 dB |
| **24, refinada alrededor de cada banda** | < 0,0001 dB | 0,002 dB | 0,005 dB | 0,0007 dB |

La rejilla que se usa tiene 24 puntos por octava en toda la banda (240 tramos), y cada tramo se subdivide si está cerca de una banda: a menos de 4 anchos de banda de su frecuencia (el ancho de banda se toma como `1/Q` octavas), cada banda pide 16 puntos por ancho de banda. En los shelves y el tilt la zona se ensancha `|ganancia|/40` décadas, porque con Q alto su resalte queda lejos de la frecuencia de la banda (un primer intento sin esto se equivocaba en 0,12 dB con un low shelf de Q 29). Con Q de hasta 1,5 no se subdivide nada. En las campanas y los cortes, los puntos que añade cada banda no dependen de su Q (unos 128), y en los shelves crecen como mucho con su ganancia: el coste está acotado.

## Coste

`autoGainDb` no asigna memoria ni bloquea, pero su coste crece con las bandas y su Q. Medido en Linux, x86-64, Release:

| Curva | Tiempo |
|---|---|
| Una campana | 13 µs |
| 6 bandas típicas (Q ≤ 2) | 36 µs |
| Las mismas con Mid y Side | 60 µs |
| Las mismas con Q 8 | 130 µs |
| 24 campanas | 75 µs |
| Peor caso: 24 shelves Mid de +30 dB y Q 30 | de 3 a 4 ms |

Por eso no se calcula en el hilo de audio: lo calcula un hilo de control al cambiar los parámetros (`dsp::AutoGain`, abajo), y el hilo de audio solo lee el resultado.

## Entrega entre hilos

`dsp::AutoGain` reúne lo que necesitan los dos lados, sin bloqueos ni memoria dinámica:

- El hilo de audio pide la compensación de unas bandas con `request()`, que copia los ajustes (unos 3 kB) en un `TripleBuffer` y les da un número de petición.
- El hilo de control llama a `service()`, que calcula la última petición, si hay una nueva, y publica el resultado junto con su número en un solo atómico de 64 bits.
- El hilo de audio lee el último resultado con `latestDb()`. No espera a que se calcule la última petición: mientras tanto sigue con la anterior. `restart()` descarta lo calculado hasta entonces, para que al encender la compensación no se aplique un valor de cuando estaba apagada.

## Verificación

`tests/dsp/auto_gain_test.cpp`:

- La ponderación K coincide con la de la norma (en complejos, aparte) y vale +0,691 dB en 997 Hz.
- Con la curva plana el resultado es exactamente 0.
- Una campana de +6 dB, Q 1 a 1 kHz da lo que da la integral numérica (trapecio en 1/1024 de octava, con la ponderación K calculada aparte), con ±0,05 dB; también picos de Q 30, shelves de Q alto y 30 curvas aleatorias.
- El límite de ±12 dB, que la dinámica no cuenta, los canales (L, R, M y S en estéreo y en mono) y la frecuencia de muestreo.
- **Sonoridad medida.** Ruido rosa estéreo (dos canales incorrelados, 8 s) por el `Equalizer` con ocho curvas típicas y la compensación aplicada mide la misma sonoridad integrada (BS.1770, con sus umbrales) que sin ecualizar, con ±0,5 LU. Medido: de −0,20 a +0,20 LU, con curvas que sin compensar cambian la sonoridad de −2,6 a +2,1 LU. El resto se debe sobre todo a lo que el ruido tiene fuera de 20 Hz a 20 kHz, que la integral no cuenta.
- La entrega entre hilos: sin resultado hasta calcular, el último de varias peticiones, el anterior mientras se calcula uno nuevo, y `restart()`.
