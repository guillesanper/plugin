# 0012. Modos de fase: IIR y fase lineal Low, Medium y High

**Estado:** Aceptada (noviembre de 2026)

## Contexto

La fase 5 del [plan](../../PLAN.md) incluye la fase lineal. Se piden cuatro modos: IIR (los filtros de siempre, latencia cero) y fase lineal en tres resoluciones (Low, Medium y High), con más precisión y más latencia en cada una. La sección 4.4 del plan dejaba abiertas varias preguntas, la más difícil la de la dinámica. Había que decidir:

1. Cómo se diseña el FIR, y de qué respuesta.
2. Cómo se aplica, y con qué tamaños y latencias.
3. Cómo encajan el canal por banda (L, R, M, S) y la dinámica.
4. Cómo se diseña sin bloquear el hilo de audio, y qué pasa al renderizar.
5. Qué pasa al cambiar de modo con el audio en marcha, y cómo se entera el host de la latencia.
6. Cómo se guarda el modo y cómo se elige.

## Decisión

1. **Muestreo en frecuencia de la magnitud exacta de las bandas, con ventana de Kaiser (β = 8).** La magnitud es la de los filtros digitales del modo IIR, la que dibuja la interfaz: la curva sigue siendo lo que suena. FFT inversa en `double`, `n − 1` coeficientes simétricos (fase lineal exacta) y una rejilla de `2n` puntos. Se descartaron:
   - *Diseño por mínimos cuadrados o Parks-McClellan:* mejor control del error, pero iterativos y mucho más lentos; hay que rediseñar mientras se arrastra una banda.
   - *Hacer simétrica la respuesta al impulso del IIR* (filtrar hacia delante y hacia atrás): eleva la magnitud al cuadrado y no permite elegir la longitud.
   - *β de Kaiser:* β = 8 pierde poca resolución frente a β = 5 y gana 19 dB de rechazo en el peor caso medido (tabla en [fase-lineal.md](../dsp/fase-lineal.md)).
2. **Convolución particionada uniforme por FFT, con latencia `n/2 + B`.** Tamaños a 48 kHz: Low 2048 coeficientes y bloques de 256 (26,7 ms), Medium 8192 y 512 (96 ms), High 32768 y 1024 (362,7 ms). Se duplican con cada duplicación de la frecuencia de muestreo, para que la resolución y la latencia en tiempo no cambien. Los dos canales van juntos en una FFT compleja. Se descartaron:
   - *Convolución no uniforme o sin latencia de bloque (Gardner):* quitaría `B` muestras de latencia a cambio de mucha más complejidad; en fase lineal la latencia la domina `n/2`.
   - *Recortar los primeros `B` coeficientes para esconder la latencia de bloque:* rompe la simetría.
3. **Canales: una matriz de 2×2 por frecuencia.** Con filtros de fase cero, cada banda es una matriz real sobre (L, R); las bandas en serie la multiplican. Hacen falta uno, dos o cuatro FIR según los canales que haya, y el caso normal (todo Stereo) cuesta lo mismo que uno.
   **Dinámica: la parte estática en el FIR, la dinámica en el IIR, como `H(−g)·H(g+d)`.** Es la "diferencia sobre el FIR estático" que proponía el plan. En los tipos con ganancia `H(−g) = 1/H(g)` exactamente, así que la magnitud es exactamente la del modo IIR, y la banda es de fase lineal mientras la dinámica no actúa. `H(−g)` va primero: la sección que cambia oye la misma señal que en IIR. Los detectores y el sidechain se retrasan la latencia. Se descartaron:
   - *Dinámica con FIR rediseñados:* demasiado lentos para seguir la ganancia muestra a muestra.
   - *Bandas dinámicas enteras en el IIR:* perderían la fase lineal siempre, también en reposo, y al activar la dinámica la parte estática cambiaría de filtro.
   - *Desactivar la dinámica en fase lineal:* quita una función a cambio de nada.
   - *`H(g+d)·H(−g)` (el otro orden):* la misma magnitud, pero hasta 5 dB más de energía en agudos con modulación rápida y realce estático.
4. **Un hilo de diseño por instancia, en `core`, y entrega sin bloqueos.** `dsp::PhaseEqualizer` publica lo que hay que diseñar en un `TripleBuffer`; `core::PhaseDesignThread` lo diseña cada 10 ms y lo entrega por un `Handoff`, por el que también vuelve lo que el audio ya no usa para destruirlo fuera del hilo de audio. Los hilos viven en `core`, como los de la sesión de espectros ([ADR 0011](0011-espectros-compartidos.md)); `dsp` solo tiene las estructuras sin bloqueo. **Al renderizar sin tiempo real** y **al preparar el audio**, el diseño se hace en el momento (`flush()`): el render no depende de la velocidad del hilo y el primer bloque suena en el modo pedido. Se descartaron:
   - *Diseñar en el hilo de mensajes:* algunos hosts lo tienen ocupado durante segundos, y no corre durante un render.
   - *Diseñar en el hilo de audio:* tarda hasta 13 ms en High.
   - *Un hilo para todas las instancias:* menos hilos, pero un diseño en High de una instancia retrasaría los de las demás. Se puede cambiar detrás de la misma interfaz si hiciera falta.
5. **Cambio de modo con fundido a silencio y latencia comunicada desde el hilo de mensajes.** No hay forma de pasar de una latencia a otra sin un hueco. Mientras se prepara el modo nuevo suena el anterior; luego, 10 ms de bajada, cambio, silencio mientras el modo nuevo llena su latencia y 10 ms de subida. El procesador comunica la latencia en `prepareToPlay` y, si cambia en marcha, con un temporizador en el hilo de mensajes (20 Hz; sustituido por un aviso del hilo de control en la [ADR 0021](0021-aviso-de-latencia-sin-temporizador.md)): `setLatencySamples` avisa al host, y eso no se hace desde el hilo de audio. Un FIR nuevo dentro del mismo modo entra con un fundido de un bloque de convolución.
6. **Un parámetro global, `phase_mode`, al final de la tabla, que los presets no cambian.** Opciones, en este orden (contrato, como `dsp::PhaseMode`): IIR, Linear Low, Linear Medium, Linear High. Los nombres describen la técnica (filtros IIR, fase lineal) y no copian los de ningún producto comercial; el primero se llama IIR y no "Natural Phase", que es el nombre de un modo de un ecualizador comercial. Por defecto, IIR: los proyectos anteriores abren sin latencia y suenan igual. Es un parámetro del host (se guarda con el proyecto, se deshace y el host lo muestra), pero **no forma parte de los presets** (`core::kParametersOutsidePresets`): elegir un preset no debe cambiar la latencia de la pista. Se elige con un desplegable en la barra superior, cuya ayuda dice la latencia del modo que suena.

## Consecuencias

- En IIR, el sonido es idéntico bit a bit al de la 0.1 (el test de contrato de sonido de la 0.1 sigue pasando sin cambios).
- El estado sigue en la versión 2: un parámetro más, con su valor por defecto si falta. Los tests que reescriben fixtures v2 comparan sin las líneas de los parámetros añadidos después (`tests/core/state_test_utils.h`), en lugar de saltarse la comprobación.
- El ancho mínimo de la ventana pasa de 960 a 1080 px (y el tamaño S, a 1080 × 580), lo que pide la barra superior con un control más sin estrechar el navegador de presets por debajo de 140 px. La [ADR 0008](0008-sistema-visual.md) fijaba 960 por lo que necesitaba entonces el panel; el criterio, que nada se corte, es el mismo.
- Limitaciones propias de la fase lineal: preeco, menos resolución en graves en Low, notches menos profundos que en el IIR, y un pasa todo no hace nada. El analizador "post" va con la latencia de retraso respecto al "pre". Detalles y medidas en [fase-lineal.md](../dsp/fase-lineal.md).
- Coste: de 0,2 % a 1 % de un núcleo en fase lineal, casi independiente del número de bandas; memoria de 1,3 a 2,3 MB en el camino de audio en High, más la del diseñador. En IIR no cambia nada.
