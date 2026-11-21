# 0024. Dinámica espectral: una STFT compartida tras el ecualizador, máscara por bin con los controles de la banda, y latencia solo mientras se use

**Estado:** Aceptada (noviembre de 2026), con las medidas del bloque 6.4 del [plan](../../PLAN.md). Lo que esas medidas han cambiado de la propuesta está en «Cambios al aceptarla», al final, y ya está recogido en la decisión.

## Contexto

La sección 5.7 del plan pide una variante de la banda dinámica que, dentro de su zona, actúe solo en las frecuencias que pasan del umbral. Necesita una transformada de tiempo corto, que añade latencia, en un plugin cuyo modo IIR hoy no tiene ninguna. Había que decidir:

1. Si la latencia existe siempre, solo mientras se use, o se puede reservar.
2. El tamaño de la transformada.
3. Cuántas transformadas hacen falta con varias bandas y canales.
4. Qué hace la parte dinámica normal de una banda espectral.
5. Dónde va en la cadena, también en fase lineal.
6. Cómo se evita el ruido musical.

Los puntos 1 y 2 los ha decidido el usuario.

## Decisión

1. **Latencia solo mientras haya al menos una banda espectral encendida**, gestionada como el cambio de modo de fase: fundido a silencio, cambio, silencio mientras se llena y fundido de entrada; el aviso al host lo da el hilo de control ([ADR 0021](0021-aviso-de-latencia-sin-temporizador.md)). Con todas apagadas, el sonido y la latencia son bit a bit los de ahora, que es lo que exige el contrato de la sección 2.1. Descartado reservarla con un ajuste (un parámetro o una línea de estado más para un caso raro, automatizar el primer interruptor) y tenerla en cuanto haya una banda dinámica (cambiaría proyectos que hoy no tienen latencia).
2. **STFT de 2048 puntos a 44,1 y 48 kHz**, escalada con la frecuencia de muestreo (×2 a 96 kHz, ×4 a 192 kHz) como la fase lineal, salto de N/4 y ventana raíz de Hann en análisis y síntesis (Allen, 1977; Griffin y Lim, 1984). Bins de 23 Hz y 42,7 ms: bien desde unos 200 Hz; en graves, la banda espectral se comporta casi como la dinámica normal. Descartados 4096 (85 ms, peor con transitorios) y un selector (otro parámetro global).
3. **Una sola STFT para todas las bandas**: L y R de cada señal en una FFT compleja (L en la parte real, R en la imaginaria), y M y S calculados de L y R en frecuencia, porque la FFT es lineal. Son 3 FFT por salto (la señal, el detector y la inversa; 4 si unas bandas escuchan la entrada y otras el sidechain), sean cuantas sean las bandas espectrales y sus canales. Las FFT cuestan algo más del 1 % de un núcleo a 48 kHz; con muchas bandas manda el cálculo de cada bin de cada banda.
4. **La máscara sustituye a la parte dinámica de la banda**, con sus mismos umbral, rango, rodilla, tiempos (en tramas, con un salto de 10,7 ms como mínimo), sidechain y canal; la parte estática se queda en el ecualizador. Así la banda no actúa dos veces, y no hay controles nuevos.
5. **Tras el ecualizador (IIR o FIR) y antes de la ganancia de salida**, con el detector en la entrada (o el sidechain) retrasado como el audio en fase lineal. La latencia se suma a la del modo. La STFT no se integra en el FIR de fase lineal: la máscara cambia en cada trama y el FIR se rediseña en otro hilo.
6. **Contra el ruido musical, el nivel de cada bin es la energía de 1/6 de octava a su alrededor** (3 bins como mínimo, con ventana de Hann y calibrado para que un seno lea su pico), además de la envolvente temporal (Cappé, 1994), y la ganancia se pondera con la forma del detector de la banda. Basta: la ganancia se mueve 0,59 dB de una trama a otra y 0,11 dB de un bin al siguiente con ruido en el umbral, frente a 1,86 y 1,53 dB con un bin.

## Consecuencias

- 24 parámetros nuevos (`bNN_spectral`) al final de la tabla, apagados por defecto y dentro de los presets; el estado no sube de versión.
- Encender la primera banda espectral, o cargar un preset que lo haga, deja un hueco breve mientras el host recalcula la compensación; automatizar ese interruptor no es útil.
- La memoria de la STFT se reserva al preparar el audio; el hilo de audio no asigna.
- En graves (por debajo de unos 200 Hz) la resolución de 23 Hz no separa armónicos cercanos: es el precio de 42,7 ms.
- La reducción empieza hasta media ventana (22 ms) antes del sonido que la provoca: cada trama ve 42,7 ms y su ganancia se aplica a toda ella.
- El cálculo de una trama llega entero en la llamada que completa un salto (0,46 ms con 24 bandas a 48 kHz, unos 2,5 ms a 192 kHz). El bloque 6.5 decide, con el plugin en un host, si basta o si se reparte entre las llamadas del salto, a cambio de un salto más de latencia.

## Cambios al aceptarla

Medidas en [docs/dsp/dinamica-espectral.md](../dsp/dinamica-espectral.md), con `tests/dsp/spectral_dynamics_test.cpp` y `esceq_bench --spectral`.

- **El suavizado de 1/6 de octava va en el nivel del detector, no en la ganancia.** Suavizar la ganancia diluye la reducción de un tono, que ocupa unos pocos bins: −3,2 dB en vez de −6 a 1 kHz y −0,7 dB a 5 kHz. Con la energía de 1/6 de octava en el nivel, todos los bins cercanos al tono leen su energía entera y el tono baja lo que marca el rango (−5,995 dB), y el ruido musical queda contenido igual (las cifras de la decisión 6). Además, un seno entre dos bins lee su nivel con −0,09 dB de error, y un ruido rosa lee lo mismo en todo el espectro.
- **El detector usa la ventana de Hann, no la raíz**, porque deja menos energía fuera del lóbulo principal; la señal sigue con la raíz de Hann en el análisis y en la síntesis.
- **3 FFT por salto, no 6**: dos señales reales en cada FFT compleja (decisión 3). En mono cuesta lo mismo que en estéreo, no la mitad.
- **Latencia N, no N − N/4.** Sacar la salida un salto antes deja fuera la última trama de cada muestra: un rizado de hasta 2,5 dB (un error de −12 dB).
- **Sin fundidos ni suavizado de parámetros en el motor**: un cambio actúa en la trama siguiente y el solapamiento lo funde en N muestras. Encender la banda, mover el umbral 30 dB de golpe y apagarla deja los agudos espurios en −122 dB.
- **Tiempos mínimos**: con el ataque mínimo y el rango agotado, el 90 % de la reducción llega 1 ms después de la ráfaga, y la recuperación tarda 35 ms; con el rango sin agotar, unos 20 ms en llegar, porque la ráfaga entra poco a poco en la ventana. Con los tiempos automáticos, tras una ráfaga aislada de 50 ms, 29 ms a 7 kHz, 26 a 1 kHz y 98 a 100 Hz (la banda dinámica: 24, 23 y 71).
- **Coste**: con 24 bandas en L/R y M/S a 48 kHz, del 3,9 al 4,2 % de un núcleo con GCC y el 3,1 % con Clang (criterio: menos del 5 %); con una banda, el 1,1 %, y no "casi lo mismo", porque con 24 manda la curva de cada bin (unos 13 ns por bin y banda). Para llegar ahí, la curva se calcula en float, con un logaritmo propio (el mismo resultado en todos los sistemas) y en bucles sin ramas; con `log10` en double y ramas costaba del 5,1 al 5,3 %.
- **En graves**, el tono baja hasta 0,7 dB menos que el rango (−11,3 dB a 60 Hz con −12), porque 3 bins no dan una máscara plana sobre su lóbulo.
- **El escalado con la frecuencia de muestreo es el de la fase lineal** (`dsp::sampleRateFactor`, que pasa a ser público en `linear_phase.h`).
- **Sin cambios**: N = 2048 con su escalado, el salto de N/4, la latencia solo mientras haya una banda espectral, la máscara en lugar de la parte dinámica de la banda, la ponderación con la forma del detector, la suma en dB de las máscaras (primero L/R y luego M/S), el lugar en la cadena y el salto como tiempo mínimo.
