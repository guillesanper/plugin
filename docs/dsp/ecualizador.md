# Ecualizador y analizador

Cómo convierte `dsp::Equalizer` los cambios de parámetros en audio sin chasquidos, y cómo funciona el analizador. Los filtros de cada banda están en [filtros.md](filtros.md), y la dinámica por banda en [dinamica.md](dinamica.md).

## Bandas

`dsp::Equalizer` tiene 24 bandas (`kMaxBands`) en serie. Cada una recibe un `BandSettings`: activada o no, y unos `FilterParams` (tipo, frecuencia, ganancia, Q, pendiente). Las desactivadas no se procesan.

El procesador llama a `setBand()` para las 24 bandas en cada bloque. Si los ajustes no han cambiado, la llamada no hace nada.

## Dos clases de cambio

No todos los parámetros pueden cambiar de forma continua.

| Cambio | Tratamiento | Duración |
|---|---|---|
| Frecuencia, ganancia, Q | **Suavizado**: el valor que suena se acerca al objetivo y los coeficientes se recalculan | Constante de tiempo de 15 ms |
| Activar o desactivar la banda, cambiar el tipo, cambiar la pendiente de un corte | **Fundido**: el filtro anterior y el nuevo suenan a la vez y se pasa de uno a otro | 10 ms, lineal |
| Bypass | **Fundido** entre la señal seca y la procesada | 10 ms, lineal |

### Suavizado

- Frecuencia y Q se suavizan en escala logarítmica (una octava cuesta lo mismo en graves que en agudos); la ganancia, en dB.
- Es un filtro de un polo: `actual = objetivo + (actual - objetivo) · c`, con `c = e^(-32 / (0,015 · fs))`.
- Se aplica cada `kChunkSize` = 32 muestras, recalculando los coeficientes con `designBand`. Es posible porque el filtro SVF admite cambios de coeficientes sin volverse inestable (ver [filtros.md](filtros.md)).
- Cuando la diferencia es despreciable (1e-4 en logaritmo, 0,001 dB), salta al objetivo exacto. A partir de ahí la banda suena **exactamente** igual que un `BandFilter` con esos ajustes.

### Fundido

- Al empezar, el filtro activo se copia, **con su memoria**, como "anterior", y el activo pasa a la configuración nueva.
- Si la banda estaba desactivada, el filtro nuevo empieza con la memoria a cero. Si cambia de tipo, conserva la memoria del anterior: está más cerca del régimen permanente que una memoria a cero.
- Una banda desactivada se representa como un diseño sin secciones (identidad), así que activar y desactivar son fundidos entre la señal sin tocar y la filtrada.
- Si llega otro cambio estructural durante un fundido, el fundido se reinicia desde el filtro activo en ese momento.

### Bypass

Durante el fundido de bypass se guarda una copia de la señal seca y se mezcla con la procesada. Con el bypass completo **no se procesa ninguna banda** (ahorra CPU) y la salida es la entrada bit a bit. Al quitar el bypass, los filtros se reinician limpios y el fundido de entrada oculta el arranque.

## Independencia del tamaño de bloque

FL Studio entrega bloques de tamaño variable, a veces de muy pocas muestras. El ecualizador trocea el audio en tramos alineados con un contador global de muestras, de modo que el suavizado ocurre siempre en las mismas muestras. Con parámetros fijos, la salida es idéntica bit a bit sea cual sea el tamaño de bloque (lo comprueba un test con bloques de 1, 13, 64, 511 y 4096).

## Verificación

`tests/dsp/equalizer_test.cpp` comprueba, entre otras cosas:

- Con todas las bandas desactivadas el audio pasa bit a bit.
- Una banda asentada suena igual que `BandFilter`, para los nueve tipos.
- Varias bandas en serie suman sus respuestas en dB (±0,01 dB).
- **Ausencia de chasquidos:** con un seno, saltos bruscos de frecuencia y ganancia cada 128 muestras, y una secuencia de activar, cambiar de tipo, cambiar de pendiente y desactivar. El test mide el mayor salto entre muestras consecutivas. Se ha comprobado que el test detecta el problema: desactivando el suavizado y los fundidos, los saltos pasan de estar por debajo del límite a ser de 1,0 (frente a límites de 0,52 y 0,12).

## Analizador

El analizador no está en el hilo de audio:

1. El procesador escribe el audio en mono (media de los canales) en dos `SpscFifo` de 32768 muestras: antes y después del EQ.
2. El editor, 30 veces por segundo, vacía las colas en dos `SpectrumAnalyzer`, aunque el analizador esté apagado, para no mostrar audio viejo al encenderlo.
3. `SpectrumAnalyzer` guarda las últimas 4096 muestras y calcula su FFT con ventana de Hann.
4. Para cada uno de los 320 puntos de la pantalla toma el máximo de los bins dentro de ±1/24 de octava, o interpola entre los dos bins vecinos si no hay ninguno (graves).
5. Aplica una inclinación de 4,5 dB/oct centrada en 1 kHz, para que la música suene "plana" en la pantalla.
6. La interfaz suaviza el resultado en el tiempo: sube con una constante de 60 ms y baja con una de 350 ms.

Calibración: un seno de amplitud 1 se lee a 0 dB (exacto si cae en un bin; con hasta 1,42 dB de pérdida entre bins, propia de la ventana de Hann). Escala de dibujo: 0 dB arriba, −90 dB abajo, independiente de la escala de la curva.

## Limitaciones conocidas

- El analizador muestra la media de los canales, no L/R ni M/S por separado.
- Resolución fija de 4096 puntos: unos 12 Hz por bin a 48 kHz, poca definición por debajo de 100 Hz.
- Los fundidos de 10 ms son lineales en amplitud; entre señales poco correladas pueden notarse como una pequeña bajada de nivel.
