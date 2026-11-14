# Filtros

Cómo están hechos los filtros de esceQ, de dónde sale cada fórmula y qué garantías dan. Código en `dsp/include/esceq/dsp/filter_design.h` y `band_filter.h`.

## Topología: SVF con integradores trapezoidales

Cada banda es una cascada de secciones de primer o segundo orden de un filtro de variable de estado (SVF) con integradores trapezoidales, también llamado TPT (*topology-preserving transform*).

Por qué no un biquad en forma directa, que es lo más habitual:

- Un EQ dinámico cambia la ganancia y la frecuencia de sus filtros continuamente. El biquad en forma directa guarda como estado muestras pasadas de entrada y salida; al cambiar los coeficientes ese estado deja de ser coherente y aparecen chasquidos o inestabilidad.
- El SVF guarda como estado el contenido de sus integradores, que tiene significado físico independiente de los coeficientes. Se puede modular a velocidad de audio y sigue siendo estable.
- Además se comporta mejor numéricamente a frecuencias bajas respecto a la de muestreo.

La respuesta en frecuencia es idéntica a la del biquad equivalente: ambos son la transformada bilineal del mismo prototipo analógico. Lo comprueba el test que compara la campana con la fórmula del *Audio EQ Cookbook*.

### Sección de segundo orden

Con `g = tan(π·f/fs)` y `k = 1/Q`:

```
a1 = 1 / (1 + g·(g + k))      a2 = g·a1      a3 = g·a2

v3 = x − ic2
v1 = a1·ic1 + a2·v3           (paso banda)
v2 = ic2 + a2·ic1 + a3·v3     (paso bajo)
ic1 = 2·v1 − ic1
ic2 = 2·v2 − ic2

y = m0·x + m1·v1 + m2·v2
```

Los coeficientes de mezcla `m0, m1, m2` determinan el tipo de filtro.

### Sección de primer orden

```
v  = (x − ic1) · g/(1 + g)
lp = v + ic1
ic1 = lp + v

y = m0·x + m2·lp
```

## Tipos

`A = 10^(gananciaDb/40)` salvo en Tilt, donde `A = 10^(gananciaDb/20)`.

| Tipo | Orden | `g` | `k` | `m0` | `m1` | `m2` |
|---|---|---|---|---|---|---|
| Bell | 2 | g | 1/(Q·A) | 1 | k·(A²−1) | 0 |
| LowShelf | 2 | g/√A | 1/Q | 1 | k·(A−1) | A²−1 |
| HighShelf | 2 | g·√A | 1/Q | A² | k·(1−A)·A | 1−A² |
| HighCut (paso bajo) | 2 | g | 1/Q | 0 | 0 | 1 |
| LowCut (paso alto) | 2 | g | 1/Q | 1 | −k | −1 |
| Notch | 2 | g | 1/Q | 1 | −k | 0 |
| BandPass | 2 | g | 1/Q | 0 | k | 0 |
| AllPass | 2 | g | 1/Q | 1 | −2k | 0 |
| HighCut | 1 | g | — | 0 | — | 1 |
| LowCut | 1 | g | — | 1 | — | −1 |
| Tilt | 1 | g·A | — | A | — | 1/A−A |

Notas:

- **Bell:** el factor `A` en `k` hace que realce y atenuación sean simétricos: +6 dB seguido de −6 dB con los mismos ajustes se cancelan exactamente.
- **Shelves:** en la frecuencia nominal la ganancia es la mitad de la pedida (en dB).
- **BandPass:** normalizado a 0 dB en el centro.
- **Tilt:** `H(s) = (A·s + 1)/(s + A)`. Vale `1/A` en continua, `A` en agudos y 1 en el pivote.

## Pendientes de los cortes

LowCut y HighCut admiten de 6 a 96 dB/oct en pasos de 6. Una pendiente de `6·N` dB/oct es un filtro Butterworth de orden `N`:

- `N/2` secciones de segundo orden, con `Q_i = 1 / (2·sin((2i − 1)·π / (2N)))`.
- Una sección de primer orden adicional si `N` es impar.

Las secciones se ordenan de menor a mayor Q.

**Q del usuario en los cortes:** escala únicamente el Q de la sección más resonante, por el factor `Q/0,7071`. Con el valor por defecto (0,7071) el filtro es Butterworth puro, con −3,01 dB en la frecuencia de corte para cualquier pendiente. Subirlo crea un único pico de resonancia en el corte. A 6 dB/oct no tiene efecto. En orden 2 la ganancia en el corte es exactamente Q.

Lo que se nombra "6 dB/oct" son en realidad `20·log10(2) = 6,02` dB por octava y orden.

## Respuesta en frecuencia

El SVF trapezoidal equivale a evaluar el prototipo analógico normalizado en

```
s = j · tan(π·f/fs) / g
```

con lo que la respuesta de cada sección es

```
orden 2:  H = m0 + (m1·s + m2) / (s² + k·s + 1)
orden 1:  H = m0 + m2 / (s + 1)
```

y la de la banda es el producto de sus secciones. `response()` calcula esto. Es la respuesta **exacta** del filtro digital, no una aproximación analógica: incluye la compresión de frecuencias de la transformada bilineal cerca de Nyquist. Por eso la curva que dibuje la interfaz coincidirá con lo que se oye.

Verificado: 4911 comparaciones entre la respuesta medida sobre el audio procesado y la calculada, a 44,1, 48, 96 y 192 kHz, con una desviación máxima de 0,000065 dB (el criterio era ±0,1 dB).

## Límites de los parámetros

`designBand()` ajusta cualquier entrada a un rango seguro, así que nunca produce coeficientes inestables:

| Parámetro | Rango |
|---|---|
| Frecuencia | 1 Hz a 0,49·fs |
| Q | 0,025 a 40 |
| Ganancia | ±60 dB |
| Pendiente | 6 a 96 dB/oct, redondeada a múltiplo de 6 |

Los rangos que ve el usuario, más estrechos, los define la tabla de parámetros de `core/` (`core/src/parameters.cpp`).

## Coste

Una sección de segundo orden es una recurrencia: cada muestra depende de la anterior, y el tiempo lo marca la latencia de esa cadena de operaciones, no su número. Por eso `BandFilter` procesa los dos canales de una banda estéreo en el mismo bucle: las dos recurrencias son independientes y el procesador las solapa. Cada canal hace exactamente las mismas operaciones que en una pasada propia, así que el resultado es idéntico bit a bit, y cuesta un tercio menos (24 campanas estéreo a 48 kHz: 0,9 % de un núcleo; 24 cortes de 96 dB/oct: 6 %).

## Precisión

El estado y los coeficientes son `double`; el audio entra y sale en `float`. Entre secciones de una cascada la señal pasa por `float`, lo que no supone un error apreciable (ver la cifra de verificación de arriba).

## Limitaciones conocidas

- **Deformación cerca de Nyquist.** A 44,1 y 48 kHz, las campanas y shelves por encima de unos 10 kHz quedan más estrechas que su equivalente analógico. Es inherente a la transformada bilineal. Está previsto corregirlo en la fase 5 (coeficientes ajustados de Vicanek o sobremuestreo), como un algoritmo nuevo para no alterar el sonido de los presets existentes.
- **Cambios de coeficientes sin suavizar.** `BandFilter::setDesign()` aplica el cambio de inmediato. Es estable, pero un salto grande de parámetros produce un salto audible. El suavizado lo hace quien llama al filtro: `dsp::Equalizer` (ver [ecualizador.md](ecualizador.md)).
- **Shelves con pendiente variable.** Solo hay shelves de segundo orden.
- **Máximo dos canales** por `BandFilter`.

## Contrato de sonido

`tests/fixtures/dsp/respuesta_v1.txt` guarda la respuesta de 540 configuraciones. `tests/dsp/filter_golden_test.cpp` falla si alguna cambia más de 0,001 dB. Si una mejora cambia el sonido a propósito, no se regenera ese archivo: se añade el algoritmo nuevo junto al antiguo, se guarda en el estado cuál usa cada instancia y se crea un archivo de referencia nuevo.

## Referencias

- V. Zavalishin, *The Art of VA Filter Design*. Teoría de los filtros TPT.
- A. Simper (Cytomic), *Solving the continuous SVF equations using trapezoidal integration and equivalent currents* y *SVF linear trapezoidal optimised*: https://cytomic.com/technical-papers/. De ahí salen la estructura y los coeficientes de mezcla.
- R. Bristow-Johnson, *Audio EQ Cookbook*: https://www.w3.org/TR/audio-eq-cookbook/. Referencia cruzada en los tests.
- M. Vicanek, *Matched Second Order Digital Filters*: https://www.vicanek.de/articles.htm. Para la corrección futura cerca de Nyquist.
