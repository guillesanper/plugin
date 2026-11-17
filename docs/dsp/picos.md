# Picos del espectro

Cómo encuentra esceQ los picos del analizador para crear bandas desde el espectro ([PLAN.md](../../PLAN.md) 5.3) y cómo estima su Q. Código en `ui/logic/include/esceq/ui/spectrum_peaks.h` (detección, Q y cambios de la banda) y `meter_logic.h` (`SpectrumAverage`); las decisiones y sus motivos, en la [ADR 0019](../decisiones/0019-bandas-desde-el-espectro.md). Todo corre en el hilo de la interfaz, nunca en el de audio.

## De dónde salen los niveles

La curva dibuja 320 puntos repartidos en escala logarítmica de 20 Hz a 20 kHz (1/32 de octava entre puntos), cada uno con el máximo de los bins de la FFT en ±1/24 de octava y la inclinación sumada (4,5 dB/oct por defecto, 0 dB en 1 kHz; ver [ecualizador.md](ecualizador.md#analizador)).

Los picos **no** se buscan en lo que se pinta, sino en un promedio del mismo espectro (`SpectrumAverage`): la media exponencial de la potencia de cada punto, con una constante de tiempo de 1 s, igual al subir que al bajar. Se promedia el espectro post, o el pre si el modo es pre + post, y se olvida (empieza de cero) al cambiar el modo, la resolución o la inclinación. Congelado el analizador, el promedio no se mueve, como el espectro.

El motivo está medido: lo que se pinta es una sola FFT por fotograma con un suavizado rápido al subir (60 ms), y en el ruido su rizado supera los 6 dB de prominencia. Con ruido rosa (6 s, sin ninguna resonancia), picos detectados por fotograma:

| Resolución | En lo que se pinta | En el promedio de 1 s |
|---|---|---|
| 4096 | 1,44 (hasta 11; todos por debajo de 920 Hz) | 0 |
| 8192 | 4,07 (hasta 13) | 0 |
| 16384 | 8,08 (hasta 17) | 0,09 (hasta 2, todos por debajo de 80 Hz) |

Con más resolución hay más bins sueltos en graves y más rizado. Un promedio de la potencia (no de los dB, que lee el ruido unos 2,5 dB bajo) reduce el rizado sin cambiar el nivel de un tono estable; y una resonancia, que es lo que se quiere atacar, se mantiene, mientras que el azar del ruido no.

## Detección

`findPeaks(niveles, frecuencias, opciones)`:

1. **Sin la inclinación.** Se resta `tilt · log2(f / 1 kHz)` (`dsp::SpectrumAnalyzer::tiltDb`, la misma fórmula que la suma): se trabaja con el espectro real. Así el nivel de un pico es el de la señal (un seno de amplitud 1 se lee a 0 dB), comparable con el umbral de la dinámica, y la forma de los picos no se tuerce: con la inclinación, la meseta de un seno en agudos (abajo) sería una rampa y el pico se iría a su borde superior, hasta 1/24 de octava.
2. **Máximos locales y mesetas.** En agudos, varios puntos seguidos toman el máximo del mismo bin y quedan al mismo nivel. Puntos a menos de 0,001 dB entre sí forman una meseta, que cuenta como un solo máximo si sus dos vecinos están por debajo. Los extremos de la pantalla no son picos (no se sabe si sigue subiendo) y los niveles no finitos no cuentan para nada.
3. **Nivel mínimo:** el máximo, por encima de −60 dB.
4. **Prominencia:** el máximo está al menos 6 dB por encima del mínimo de su entorno de ±1/3 de octava. Nivel mínimo y prominencia se miden en el punto muestreado, no en el vértice interpolado: la interpolación no puede convertir en pico un rizado.
5. **Frecuencia y nivel refinados:** el vértice de la parábola por el máximo y sus dos vecinos (en log2 de la frecuencia, desplazamiento de ±1/2 punto como mucho); en una meseta, su centro.

Coste: 5,9 µs por llamada con 320 puntos (Release, x86-64, un núcleo), más 9,3 µs por fotograma para el promedio. La interfaz solo busca cuando ha cambiado el espectro y el ratón está sobre la curva.

## Q

Del ancho entre los puntos a −3 dB del máximo refinado. Desde el máximo (o los bordes de la meseta) hacia fuera, hasta el primer punto que queda a −3 dB o menos, con interpolación lineal en log2(f) entre él y el anterior. Si antes de llegar el nivel vuelve a subir (un valle: lo siguiente es otro pico), ese lado no cuenta y el pico se supone simétrico con el otro. Con un ancho de N octavas entre los dos puntos:

```
Q = f0 / (f2 − f1) = 1 / (2^(N/2) − 2^(−N/2))
```

que es exacto para una resonancia de segundo orden (sus puntos a −3 dB son simétricos en escala logarítmica). El resultado se limita a 0,5–30. Como la prominencia se mide en ±1/3 de octava, una resonancia más ancha que Q ≈ 3,5 no baja 6 dB en ese entorno y no se detecta: las bandas desde el espectro son para resonancias, no para ondulaciones amplias.

## Precisión medida

**Senos** (amplitud 0,5, 48 kHz, una sola FFT, 600 frecuencias de 25 Hz a 18 kHz): el pico queda a menos de 1/48 de octava de la frecuencia a partir de 475 Hz con 4096 puntos, de 238 Hz con 8192 y de 119 Hz con 16384. Por debajo manda la resolución del analizador, no la detección: sin bins dentro de ±1/24 de octava, la pantalla interpola entre los dos vecinos y su máximo cae en un bin, a hasta medio bin del seno (5,9 Hz con 4096 puntos a 48 kHz; a 100 Hz eso son 0,08 octavas). Con una forma exacta (sin la resolución del analizador de por medio), el error es menor de 1/48 en todo el rango.

**Resonancias** (ruido blanco por un paso banda de Q conocido, 24 dB sobre un fondo de ruido rosa, 5 s, error del Q estimado):

| f0 | Q 4 | Q 6 | Q 8 | Q 12 | Q 16 |
|---|---|---|---|---|---|
| 200 Hz | +40 % | +13 % | −10 % | −4 % | −23 % |
| 500 Hz | — | +3 % | −11 % | −29 % | −41 % |
| 1 kHz | +17 % | +2 % | +3 % | −11 % | −25 % |
| 3 kHz | −1 % | −5 % | −8 % | −19 % | −29 % |
| 8 kHz | +10 % | −1 % | −9 % | −20 % | −30 % |

(4096 puntos; "—": no se detecta, el pico no destaca 6 dB en ±1/3 de octava.) Con una forma exacta (la de la resonancia, muestreada en los 320 puntos, de 60 Hz a 15 kHz), el error del Q es menor del 1 % con Q de 4 a 10, del 6 % con Q 20 y del 19 % con Q 30, cuyo ancho a −3 dB es de solo 1,5 puntos; la frecuencia, a menos de 0,0025 octavas. Con el analizador, dos efectos opuestos: el máximo en ±1/24 de octava ensancha cada pico 1/12 de octava, así que las resonancias estrechas (Q 12 o más) salen con un Q menor (hasta −30 % con Q 16, −41 % a 500 Hz); y el máximo de muchos bins de ruido sube más donde el espectro es plano (la cima) que en los flancos, así que las anchas salen algo más estrechas. Restar el ensanchamiento (1/12 de octava) se probó y empeora el conjunto: el error medio pasa del 15 % al 70 %. Con 16384 puntos los resultados son parecidos, algo peores en graves.

Los tests (`tests/ui/spectrum_peaks_test.cpp`) comprueban los criterios del plan con formas exactas (senos entre puntos y en mesetas, dos senos a media octava, ruido rosa con rizado por debajo de la prominencia, resonancias de Q 4 a 20 dentro del ±25 %) y con el analizador real (senos desde 500 Hz con 4096 puntos y desde 120 Hz con 16384, dos senos a media octava, ruido rosa sin picos en el promedio, y resonancias de Q 6 y 10 a 1 y 3 kHz dentro del ±25 %).

## La banda

- `peakBandChanges`: una campana en la frecuencia del pico, con su Q y la ganancia del arrastre, y todo lo demás por defecto (como el doble clic, `newBandChanges`).
- `dynamicPeakChanges` (Alt al soltar): la dinámica encendida con el umbral 3 dB por debajo del nivel del pico; la ganancia arrastrada pasa a ser el rango (el de por defecto, −6 dB, si no se ha arrastrado: menos de 0,05 dB) y la ganancia estática vuelve a 0 dB. Así la banda no toca la señal mientras la resonancia no pase del umbral, y la baja (o la sube) como mucho lo arrastrado. El detector de una campana es un paso banda con su frecuencia y su Q, 0 dB en el centro ([dinamica.md](dinamica.md)), así que un tono estable en el pico lo lee al mismo nivel que el analizador sin la inclinación: con la rodilla por defecto (6 dB), empieza bajando 3 dB.
