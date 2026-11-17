# Ecualizador y analizador

Cómo convierte `dsp::Equalizer` los cambios de parámetros en audio sin chasquidos, y cómo funciona el analizador. Los filtros de cada banda están en [filtros.md](filtros.md), y la dinámica por banda en [dinamica.md](dinamica.md). Este es el motor del modo IIR; los modos de fase lineal están en [fase-lineal.md](fase-lineal.md).

## Bandas

`dsp::Equalizer` tiene 24 bandas (`kMaxBands`) en serie. Cada una recibe un `BandSettings`: activada o no, y unos `FilterParams` (tipo, frecuencia, ganancia, Q, pendiente). Las desactivadas no se procesan.

El procesador llama a `setBand()` para las 24 bandas en cada bloque. Si los ajustes no han cambiado, la llamada no hace nada.

## Canal por banda

Cada banda tiene un `ChannelMode`: qué parte de la señal estéreo procesa. Decisiones y motivos en la [ADR 0007](../decisiones/0007-canal-por-banda.md).

| Modo | Qué procesa | Qué pasa intacto |
|---|---|---|
| Stereo | Los dos canales, con el mismo filtro | Nada |
| Left / Right | Solo ese canal | El otro canal, bit a bit |
| Mid | La suma `m = (L+R)/2` | La diferencia |
| Side | La diferencia `s = (L−R)/2` | La suma |

Mid y Side no decodifican con `L = M+S`: filtran la suma (o la diferencia) y suman a los canales solo lo que el filtro ha cambiado.

```
d = filtro(m) − m        L += d        R += d      (Mid)
d = filtro(s) − s        L += d        R −= d      (Side)
```

Así la componente que no se procesa no sufre redondeos, y una banda que no hace nada deja pasar toda la señal bit a bit. Con audio mono, Stereo, Left y Mid procesan el único canal, y Right y Side no hacen nada.

## Dos clases de cambio

No todos los parámetros pueden cambiar de forma continua.

| Cambio | Tratamiento | Duración |
|---|---|---|
| Frecuencia, ganancia, Q | **Suavizado**: el valor que suena se acerca al objetivo y los coeficientes se recalculan | Constante de tiempo de 15 ms |
| Activar o desactivar la banda, cambiar el tipo, cambiar la pendiente de un corte, cambiar el canal | **Fundido**: el filtro anterior y el nuevo suenan a la vez y se pasa de uno a otro | 10 ms, lineal |
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
- Si cambia el canal, el fundido es entre la banda en el canal antiguo y en el nuevo, y el filtro nuevo empieza con la memoria a cero: va a oír otra señal.
- Si llega otro cambio estructural durante un fundido, espera a que este termine (como mucho 10 ms y un tramo de 32 muestras) y entonces empieza el suyo. Reiniciar el fundido a medias descartaría de golpe el filtro que se está desvaneciendo, y se oiría un salto. Mientras espera, la banda sigue sonando como está; si el cambio se deshace antes de empezar, no pasa nada.

### Bypass

Durante el fundido de bypass se guarda una copia de la señal seca y se mezcla con la procesada. Con el bypass completo **no se procesa ninguna banda** (ahorra CPU) y la salida es la entrada bit a bit. Al quitar el bypass, los filtros se reinician limpios y el fundido de entrada oculta el arranque.

## Independencia del tamaño de bloque

FL Studio entrega bloques de tamaño variable, a veces de muy pocas muestras. El ecualizador trocea el audio en tramos alineados con un contador global de muestras, de modo que el suavizado ocurre siempre en las mismas muestras. Con parámetros fijos, la salida es idéntica bit a bit sea cual sea el tamaño de bloque (lo comprueba un test con bloques de 1, 13, 64, 511 y 4096).

## Valores no finitos

Un NaN o un infinito que entre en un filtro se queda en su memoria para siempre: el filtro ya no vuelve a dar números. Por eso:

- Al final de cada bloque se comprueba la salida. Si tiene algún valor no finito, el bloque sale en silencio y todos los filtros se reinician, como en `reset()`. Un estado no finito siempre llega a la salida (incluso en una campana a 0 dB, porque `0·NaN` es NaN), así que basta con mirar ahí.
- El detector de la dinámica no llega a la salida: si el sidechain trae un valor no finito, se reinician el detector y la envolvente de esa banda.
- Los ajustes no finitos (frecuencia, ganancia o Q) toman su valor por defecto antes de ajustarse al rango.
- El analizador trata las muestras no finitas como silencio.

Con entrada finita nada de esto cambia el resultado: la salida es la misma bit a bit.

## Verificación

`tests/dsp/equalizer_test.cpp` comprueba, entre otras cosas:

- Con todas las bandas desactivadas el audio pasa bit a bit.
- Una banda asentada suena igual que `BandFilter`, para los nueve tipos.
- Varias bandas en serie suman sus respuestas en dB (±0,01 dB).
- **Ausencia de chasquidos:** con un seno, saltos bruscos de frecuencia y ganancia cada 128 muestras, y una secuencia de activar, cambiar de tipo, cambiar de pendiente y desactivar. El test mide el mayor salto entre muestras consecutivas. Se ha comprobado que el test detecta el problema: desactivando el suavizado y los fundidos, los saltos pasan de estar por debajo del límite a ser de 1,0 (frente a límites de 0,52 y 0,12).

`tests/dsp/equalizer_channel_test.cpp` comprueba el canal por banda:

- Left y Right procesan un canal y el otro sale bit a bit; Mid y Side filtran su componente y dejan la otra intacta.
- Nulos: Left + Right con los mismos ajustes es idéntico a Stereo; Mid + Side equivale a Stereo (diferencia < 5·10⁻⁶).
- Una banda que no hace nada pasa bit a bit en los cinco modos.
- La detección dinámica sigue al canal, también con sidechain externo (ver [dinamica.md](dinamica.md)).
- Cambiar de canal no produce saltos, y el resultado no depende del tamaño de bloque. Se comprobó que los tests detectan el fallo: sin el fundido, con el signo de Side cambiado o con el detector sin seguir al canal, fallan.

## Analizador

El analizador no está en el hilo de audio:

1. El procesador escribe el audio en mono (media de los canales) en dos `SpscFifo` de 32768 muestras: antes y después del EQ.
2. El editor, 30 veces por segundo, vacía las colas en dos `SpectrumAnalyzer`, aunque el analizador esté apagado, para no mostrar audio viejo al encenderlo. Al abrirse, las vacía sin analizarlas: con el editor cerrado se han llenado con el audio de cuando se cerró.
3. `SpectrumAnalyzer` guarda las últimas 4096 muestras (8192 o 16384 con la resolución elegida) y calcula su FFT con ventana de Hann.
4. Para cada uno de los 320 puntos de la pantalla toma el máximo de los bins dentro de ±1/24 de octava, o interpola entre los dos bins vecinos si no hay ninguno (graves).
5. Aplica una inclinación de 4,5 dB/oct centrada en 1 kHz (0, 3 o 6 a elección), para que la música suene "plana" en la pantalla.
6. La interfaz suaviza el resultado en el tiempo: sube con una constante de 60 ms y baja con una de 350 ms (1 s o 120 ms a elección). Con *Peak hold* guarda además el máximo de cada punto ya suavizado; congelado, no calcula nada pero sigue llenando el historial.

Los ajustes y sus valores por defecto (los de siempre) están en la [ADR 0018](../decisiones/0018-analizador-ampliado.md). Coste de cada espectro en Release (x86-64, un núcleo): 0,09 ms con 4096 puntos, 0,17 con 8192 y 0,36 con 16384; con pre + post a 30 fotogramas por segundo, 2 % de un núcleo en el peor caso, así que no hace falta bajar el ritmo con la resolución alta.

Con más puntos, un seno se lee al mismo nivel (la calibración no depende de la resolución), pero el ruido se lee más bajo, porque su energía se reparte entre más bins: medido con ruido blanco, de 1,6 a 2,4 dB menos por cada duplicación (de 4096 a 16384, 3,2 dB menos a 100 Hz, 3,3 a 1 kHz y 4,7 a 10 kHz).

Calibración: un seno de amplitud 1 se lee a 0 dB (exacto si cae en un bin; con hasta 1,42 dB de pérdida entre bins, propia de la ventana de Hann). Escala de dibujo: 0 dB arriba, −90 dB abajo, independiente de la escala de la curva.

## Limitaciones conocidas

- El analizador muestra la media de los canales, no L/R ni M/S por separado. La curva total de la interfaz suma todas las bandas sin distinguir su canal.
- Con la resolución por defecto, 4096 puntos, unos 12 Hz por bin a 48 kHz: poca definición por debajo de 100 Hz. Con 16384, 2,9 Hz, a cambio de una respuesta más lenta (la ventana abarca 341 ms).
- Los fundidos de 10 ms son lineales en amplitud; entre señales poco correladas pueden notarse como una pequeña bajada de nivel.
