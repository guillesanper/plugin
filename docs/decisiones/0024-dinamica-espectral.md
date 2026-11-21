# 0024. Dinámica espectral: una STFT compartida tras el ecualizador, máscara por bin con los controles de la banda, y latencia solo mientras se use

**Estado:** Propuesta (noviembre de 2026). La cierra el bloque 6.4 del [plan](../../PLAN.md) con sus medidas: si cambia algo de lo de abajo, lo dice al aceptarla.

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
3. **Una sola STFT para todas las bandas**: L y R de la señal y del detector, y M y S calculados de L y R en frecuencia, porque la FFT es lineal. Son 6 FFT por salto, sean cuantas sean las bandas espectrales y sus canales; el coste depende de que haya alguna, no de cuántas.
4. **La máscara sustituye a la parte dinámica de la banda**, con sus mismos umbral, rango, rodilla, tiempos (en tramas, con un salto de 10,7 ms como mínimo), sidechain y canal; la parte estática se queda en el ecualizador. Así la banda no actúa dos veces, y no hay controles nuevos.
5. **Tras el ecualizador (IIR o FIR) y antes de la ganancia de salida**, con el detector en la entrada (o el sidechain) retrasado como el audio en fase lineal. La latencia se suma a la del modo. La STFT no se integra en el FIR de fase lineal: la máscara cambia en cada trama y el FIR se rediseña en otro hilo.
6. **Contra el ruido musical, la ganancia de cada bin se pondera con la forma del detector de la banda y se suaviza a 1/6 de octava**, además de la envolvente temporal (Cappé, 1994). El bloque 6.4 mide si basta.

## Consecuencias

- 24 parámetros nuevos (`bNN_spectral`) al final de la tabla, apagados por defecto y dentro de los presets; el estado no sube de versión.
- Encender la primera banda espectral, o cargar un preset que lo haga, deja un hueco breve mientras el host recalcula la compensación; automatizar ese interruptor no es útil.
- La memoria de la STFT se reserva al preparar el audio; el hilo de audio no asigna.
- En graves (por debajo de unos 200 Hz) la resolución de 23 Hz no separa armónicos cercanos: es el precio de 42,7 ms.
- Si la síntesis permite sacar la salida un salto antes (latencia N − N/4), el bloque 6.4 lo mide y lo dice al aceptar esta nota.
