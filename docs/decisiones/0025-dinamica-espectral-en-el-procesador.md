# 0025. Dinámica espectral en el procesador: el cálculo de cada trama repartido en el salto siguiente, y la latencia ligada al interruptor

**Estado:** Aceptada (noviembre de 2026), en el bloque 6.5 del [plan](../../PLAN.md). Cambia la latencia de la [ADR 0024](0024-dinamica-espectral.md) (decisión 5 y «Cambios al aceptarla»): N + N/4 muestras en vez de N. El resto de la 0024 sigue igual.

## Contexto

Al integrar la dinámica espectral en `dsp::PhaseEqualizer` (bloque 6.5) quedaban dos cosas por decidir:

1. **Picos de CPU.** El motor del bloque 6.4 calcula una trama entera en la llamada que completa un salto. Medido con `PhaseEqualizer`, lo que llama el procesador, con 24 bandas espectrales en L/R y M/S (`esceq_bench --scenario espectral`, mediana de las llamadas más lentas de cada repetición): 0,42–0,53 ms a 48 kHz, cuando un bloque de 32 muestras dura 0,67 ms; y 1,8–1,9 ms a 192 kHz, cuando un bloque de 256 dura 1,33 ms. Con una mezcla con 2 bandas espectrales, 0,6 ms a 192 kHz. Una llamada que dura más que su bloque es un corte en el audio, aunque la media sea baja (3,8 % de un núcleo a 48 kHz, 16 % a 192 kHz).
2. **Qué enciende la latencia.** La sección 5.7 dice que hay latencia mientras haya "una banda espectral encendida", y hay dos lecturas: el interruptor `bNN_spectral` de cualquier banda, o solo una banda que de verdad actúe (encendida, con la dinámica, en un tipo con ganancia y con rango).

Las dos las ha decidido el usuario.

## Decisión

1. **El cálculo de cada trama se reparte entre las llamadas del salto siguiente**, a cambio de un salto más de latencia: **N + N/4** muestras (2560: 53,3 ms a 48 kHz y 58,0 ms a 44,1; los mismos milisegundos a 96 y 192 kHz). La trama se divide en pasos que se hacen en orden: la FFT de la señal, cada fuente del detector (la entrada y el sidechain), cada banda, la conversión de cada una de las cuatro máscaras de dB a lineal y la síntesis (aplicar las máscaras, la FFT inversa y la suma solapada); con 24 bandas, 32 pasos. Cada llamada hace los que le tocan por la parte del salto que trae (redondeando hacia arriba), y la que completa el salto termina la trama antes de empezar la siguiente. Los anillos pasan a 2N muestras, para que la trama en curso no se sobrescriba mientras se calcula, y la salida se lee N + N/4 por detrás. Los ajustes de las bandas se fijan al empezar cada trama, así que un cambio actúa en la primera trama que empieza después, como antes.

   El resultado es **bit a bit** el de antes retrasado un salto (comprobado con 24 bandas, con bloques de 1, 64 y 4096 muestras a 48 kHz y de 333 a 96 kHz, y con una banda apagada a mitad con los de 1 y 64), y no depende de cómo se trocee el audio.

2. **La latencia la enciende el interruptor**: hay latencia espectral mientras alguna banda tenga `bNN_spectral` encendido, haga algo o no. Apagar la banda, su dinámica, cambiarla a un tipo sin ganancia o poner el rango a 0 no la cambian y no dejan hueco; solo la cambian el interruptor (de 0 a 1 bandas, o al revés) y cargar un preset o un proyecto que lo cambie.

3. **Integración en `PhaseEqualizer`** (lo que pedía la sección 5.7, sin alternativas que decidir):
   - `dsp::DynamicSettings::spectral` lleva el interruptor (`core/band_model` lo traduce de `bNN_spectral`). En las bandas espectrales, mientras suena la dinámica espectral, el `Equalizer` recibe la banda sin dinámica y el motor, con ella; las demás, al revés. Sin ninguna banda espectral, el `Equalizer` recibe exactamente lo de antes.
   - La dinámica espectral va tras el IIR o el FIR, con su detector escuchando la entrada del ecualizador (y el sidechain) retrasada como el audio: en IIR, la entrada sin retrasar; en fase lineal, la que ya retrasa el camino del FIR.
   - La primera banda espectral, o la última, cambia la latencia con el fundido del cambio de modo (10 ms a silencio, el cambio, silencio mientras se llena la latencia nueva y 10 ms de subida); el hilo de control avisa al host ([ADR 0021](0021-aviso-de-latencia-sin-temporizador.md)). Las demás se encienden y se apagan sin hueco: la dinámica normal de la banda se funde en el `Equalizer` y la máscara entra en la trama siguiente.
   - El bypass hace un fundido con la señal seca retrasada lo que la máscara, como en fase lineal, para que la latencia no cambie.
   - El medidor de la banda (`PhaseEqualizer::dynamicGainDb`) es, en una banda espectral, el del bin que más cambia de su máscara.

Se descartaron:

- **Aceptar los picos** y documentarlos. Con bloques pequeños a 96 y 192 kHz, o con varios plugins en la misma pista, una llamada de 2 ms cortaría el audio; 10,7 ms más de latencia en un efecto que ya tiene 42,7 no cambian el uso (la latencia se compensa).
- **Hacer el cálculo en otro hilo.** Obligaría a sincronizar dos hilos con el de audio en cada salto (o a esperar, que no se puede), y la latencia extra sería la misma o mayor.
- **Repartir en partes iguales de tiempo** (medir cada paso). Haría depender el resultado y el orden de la máquina; los recuentos de pasos son deterministas y se prueban sin tiempos.
- **Encender la latencia solo con bandas que actúan.** Menos latencia en un caso raro (el interruptor encendido en una banda que no hace nada), a cambio de huecos al editar: apagar la banda un momento para compararla, o arrastrar el rango por 0, cambiaría la latencia.

## Consecuencias

- La ayuda del interruptor (bloque 6.6) dirá 53,3 ms ("Spectral · adds 53.3 ms latency"), o lo que dé la frecuencia de muestreo.
- Medido con `PhaseEqualizer` y 24 bandas espectrales, la llamada más lenta pasa a 0,08 ms a 48 kHz con bloques de 32 (antes, 0,42–0,53) y a 0,17 ms a 192 kHz con bloques de 64 (antes, 1,8–1,9 ms con bloques de 256). El paso más caro es el análisis de una fuente: 27 µs a 48 kHz y 127 µs a 192 kHz. A 192 kHz con bloques de 32 muestras (0,17 ms) ese paso ocupa tres cuartos del bloque. El coste medio no cambia.
- Un interruptor encendido en una banda que no actúa deja la latencia puesta: en la interfaz (bloque 6.6), el interruptor debe verse encendido aunque esté desactivado por tener la dinámica apagada, y su ayuda dice la latencia.
- `SpectralDynamics::latencySamples()` y `latency()` son N + N/4; `stepsProcessed()` cuenta los pasos, para los tests.
- Contratos: ninguno cambia. Los 24 parámetros nuevos están al final de la tabla (índices 364 a 387) y el estado sigue en la versión 3; con todos los interruptores apagados, el sonido y la latencia son bit a bit los de antes en los cuatro modos.

## Nota: la interfaz (bloque 6.6)

Al hacer la interfaz se decidió, sin cambiar nada de lo anterior:

- **La curva de una banda espectral es la estática.** La sección 5.7 decía que el medidor y la curva muestran la reducción máxima de la máscara. El medidor del panel, el de junto al nodo y la etiqueta flotante ("Dyn") sí la muestran; pero dibujar la campana entera bajada lo que baja su bin más reducido contradecía al sombreado y a lo que suena (con una resonancia reducida 9 dB, la curva total bajaba 9 dB en toda la banda). Así que en una banda espectral la curva de la banda es la de su ajuste estático, sin la referencia punteada (coincidiría), la máscara se sombrea bajo ella y la curva total le suma la máscara columna a columna (`ui::soundingBandSettings` no suma la ganancia dinámica en las bandas espectrales; `CurveView`).
- **La máscara llega a la interfaz reducida a 192 puntos** de una rejilla logarítmica fija de 20 Hz a 20 kHz (la de la curva), cada uno con el bin de su tramo que más cambia (para que un tono reducido en un bin se vea con su profundidad en agudos, donde caben varios por punto) e interpolada en graves, publicada unas 30 veces por segundo de audio por un `dsp::TripleBuffer` (`core/spectral_mask.h`). Reducirla en el hilo de audio cuesta recorrer los bins de las bandas activas 30 veces por segundo; publicar los bins enteros serían hasta 4097 por banda (400 KB por copia a 192 kHz). La curva la lleva a sus columnas interpolando (`ui/spectral_mask.h`).
- **El interruptor *Spectral* va en la misma fila que *Auto*:** la columna lateral de la tarjeta de dinámica pasa de 104 a 156 px y las celdas de los potenciómetros, en el tamaño mínimo, de 88 a 81 px (el mínimo es 66). Sin dinámica, o en un tipo que no la admite, se desactiva pero se ve encendido, y su ayuda avisa de que la latencia sigue.
