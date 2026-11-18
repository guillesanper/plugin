# Filtros

Cómo están hechos los filtros de esceQ, de dónde sale cada fórmula y qué garantías dan. Código en `dsp/include/esceq/dsp/filter_design.h` y `band_filter.h`.

Hay dos algoritmos para pasar de cada prototipo analógico al filtro digital (`FilterAlgorithm`). **Classic**, el de la 0.1, es la transformada bilineal con la frecuencia predeformada. **Matched** iguala la magnitud del prototipo analógico y corrige la deformación cerca de Nyquist ([más abajo](#algoritmo-matched-corrección-en-agudos); decisión en la [ADR 0013](../decisiones/0013-correccion-en-agudos.md)). Los dos usan la misma topología y la misma tabla de tipos.

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

Con cualquiera de los dos algoritmos, el SVF trapezoidal equivale a evaluar el prototipo analógico normalizado en

```
s = j · tan(π·f/fs) / g
```

con lo que la respuesta de cada sección es

```
orden 2:  H = m0 + (m1·s + m2) / (s² + k·s + 1)
orden 1:  H = m0 + m2 / (s + 1)
```

y la de la banda es el producto de sus secciones. `response()` calcula esto. Es la respuesta **exacta** del filtro digital, no una aproximación analógica: con Classic, incluye la compresión de frecuencias de la transformada bilineal cerca de Nyquist. Por eso la curva que dibuje la interfaz coincidirá con lo que se oye. Para evaluar muchas bandas en las mismas frecuencias, `responseTangent()` calcula `tan(ω/2)` una vez y `responseAtTangent()` / `magnitudeDbAtTangent()` la reutilizan, con el mismo resultado bit a bit.

Verificado (Classic): 4911 comparaciones entre la respuesta medida sobre el audio procesado y la calculada, a 44,1, 48, 96 y 192 kHz, con una desviación máxima de 0,000065 dB (el criterio era ±0,1 dB).

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

El compilador no vectoriza la pareja de canales, y hacerlo a mano con SSE2 ahorra un 18–25 % en cascadas estáticas y nada con la dinámica: el límite es la latencia de la recurrencia, no el número de operaciones. Por eso no se usa SIMD ([ADR 0020](../decisiones/0020-sin-simd.md), con las medidas de 44,1 a 192 kHz).

## Precisión

El estado y los coeficientes son `double`; el audio entra y sale en `float`. Entre secciones de una cascada la señal pasa por `float`, lo que no supone un error apreciable (ver la cifra de verificación de arriba).

En una misma plataforma el resultado es siempre el mismo, bit a bit. Entre arquitecturas puede variar en los últimos bits: Clang en arm64 (Apple Silicon) contrae las multiplicaciones y sumas de la recurrencia en instrucciones FMA, con un solo redondeo, y en x86-64 ni GCC ni MSVC lo hacen. Queda muy por debajo de la tolerancia de los archivos de referencia (0,001 dB, abajo).

## Limitaciones conocidas

- **Deformación cerca de Nyquist (Classic).** A 44,1 y 48 kHz, las campanas y shelves por encima de unos 10 kHz quedan más estrechas que su equivalente analógico. Es inherente a la transformada bilineal. Matched lo corrige; Classic se conserva para que los proyectos guardados suenen igual.
- **Lo que queda por encima de Nyquist (Matched).** Una campana o un shelf anchos centrados cerca de Nyquist tienen parte de su respuesta analógica por encima de fs/2, y ningún filtro digital de segundo orden puede copiarla. Matched iguala la magnitud en Nyquist y en la frecuencia de la banda, pero entre las dos se equivoca en unos dB a 44,1 kHz (tabla de abajo).
- **Cambios de coeficientes sin suavizar.** `BandFilter::setDesign()` aplica el cambio de inmediato. Es estable, pero un salto grande de parámetros produce un salto audible. El suavizado lo hace quien llama al filtro: `dsp::Equalizer` (ver [ecualizador.md](ecualizador.md)).
- **Shelves con pendiente variable.** Solo hay shelves de segundo orden (con Matched y Q > 0,7071, en dos secciones, pero con la misma pendiente).
- **Máximo dos canales** por `BandFilter`.

## Algoritmo Matched: corrección en agudos

La bilineal lleva todo el eje de frecuencias analógico, hasta el infinito, al intervalo de 0 a Nyquist. Cerca de Nyquist lo comprime, y una campana a 15 kHz con +6 dB y Q 1, a 48 kHz, suena así:

| Hz | Analógico | Classic | Matched |
|---|---|---|---|
| 5000 | +0,78 | +0,35 | +1,05 |
| 10000 | +3,52 | +1,99 | +4,04 |
| 12000 | +4,95 | +3,54 | +5,35 |
| 15000 | +6,00 | +6,00 | +6,00 |
| 18000 | +5,25 | +3,02 | +4,85 |
| 20000 | +4,43 | +1,16 | +3,92 |
| 23000 | +3,36 | +0,06 | +3,13 |

Matched (M. Vicanek, *Matched Second Order Digital Filters*, 2016) parte del mismo prototipo analógico de la tabla de tipos y lo digitaliza de otra forma. El código está en `dsp/src/filter_design.cpp`. En `dsp` cada banda lleva su algoritmo en `FilterParams::algorithm`; en el plugin, todas las bandas de una instancia usan el mismo, guardado en el estado y elegido en el menú *HF accuracy* (Matched en las instancias nuevas, Classic en los proyectos anteriores: [ADR 0014](../decisiones/0014-estado-v3-algoritmo-de-filtro.md)).

### Sección de segundo orden

Prototipo `H(s) = (n2·s² + n1·s + n0)/(s² + k·s + 1)`, con `n2 = m0`, `n1 = m0·k + m1` y `n0 = m0 + m2`, y la frecuencia de la sección `w` en rad/muestra, sin predeformar.

1. **Polos por invariancia al impulso:** `z = exp(w·(−ζ ± √(ζ² − 1)))`, con `ζ = k/2`. Así quedan en su sitio, sin deformación. Lo que hace falta del denominador `A(z) = 1 + a1·z⁻¹ + a2·z⁻²` se calcula sin restas que pierdan precisión en graves: `c = (1 − z1)(1 − z2)` con `expm1`, `a = (1 + z1)(1 + z2)` y `b = 2·(1 − z1·z2)`.
2. **Numerador por la magnitud.** Con `φ0 = cos²(ω/2)`, `φ1 = sin²(ω/2)` y `φ2 = sin²ω`, el cuadrado de la magnitud de un polinomio de segundo orden es lineal en tres números: `|B(e^jω)|² = B0·φ0 + B1·φ1 + B2·φ2`. B0 y B1 salen de igualar la magnitud analógica en continua y en Nyquist, y B2 de igualarla en `ωm = min(w, 0,9π)`. Pasado 0,9π, el tercer punto estaría casi encima de Nyquist y el sistema quedaría mal condicionado.
3. **Factorización:** `R = √B0`, `P = √B1` y `Q = 2·√(W² + B2)`, con `W = (√B0 + √B1)/2`, son los coeficientes del numerador en la variable `p = (1 − z⁻¹)/(1 + z⁻¹)`. El signo de Q es el de `n1`: así un paso todo sigue siéndolo. Si `W² + B2 < 0`, el objetivo no lo cumple ningún filtro real, y se toma `Q = 0`.
4. **De vuelta al SVF:** el denominador `a·p² + b·p + c` es `c·(s² + kd·s + 1)` con `s = p/g`, `g = √(c/a)` y `kd = b/√(ac)`. El numerador da `m0 = P/a`, `m1 = Q/√(ac) − m0·kd` y `m2 = R/c − m0`.

Un prototipo constante (una campana a 0 dB) da exactamente `m1 = m2 = 0`: el audio pasa bit a bit, como con Classic.

**Ceros en el eje imaginario** (`n1 = 0`: notch y secciones de los cortes de graves): van al círculo unidad en la misma frecuencia, `ωz = w·√(n0/n2)`, con numerador `β·(1, −2·cos ωz, 1)`. El notch sigue anulando exactamente su frecuencia, y los cortes de graves conservan sus dos ceros en continua (12 dB/oct por sección). Queda libre la escala β. Se iguala en continua si allí no hay un cero (notch). Si lo hay (corte de graves), se iguala en ωm: con Nyquist el error era seis veces mayor.

### Sección de primer orden

`H(s) = m0 + m2/(s + 1)`. La sección digital vale exactamente `(G1·s + G0)/(s + 1)` en `s = j·tan(ω/2)/g`: con `G0` y `G1` las magnitudes analógicas en continua y en Nyquist, coincide con ellas para cualquier `g`. El tercer punto da el polo: `g = tan(ωm/2)·√((Gm² − G1²)/(G0² − Gm²))`. ωm es la media geométrica del polo y del cero, es decir, el pivote del tilt, o la frecuencia de corte si el cero está en 0 o en infinito (Vicanek, *Matched One-Pole Digital Shelving Filters*).

### Lo que el método solo no resuelve

Con los polos fijados por invariancia al impulso, igualar tres puntos no siempre tiene solución. Medido así, tal cual, había errores de hasta 60 dB. Se resuelve con tres reglas:

- **Atenuaciones como inversa del realce.** En campanas, shelves y tilt, la atenuación de g dB es la inversa exacta del realce de g dB. En una atenuación estrecha, los polos caen casi encima del círculo unidad cerca de Nyquist, y su numerador no tiene solución; en el realce son los ceros los que están ahí, y el ajuste funciona. Así, además, `H(g)·H(−g) = 1` exactamente, como con Classic: lo necesita la dinámica en fase lineal ([fase-lineal.md](fase-lineal.md)).
- **El high shelf sale del low shelf:** `HS(g) = 10^(g/20)/LS(g)`, que es una identidad de los shelves del *Audio EQ Cookbook*. Los polos de un high shelf con realce están en `f·√A`, y pueden pasar de Nyquist, donde la invariancia al impulso los doblaría.
- **Shelves con Q > 0,7071 en dos secciones.** El low shelf tiene polos en `w/√A` y ceros en `w·√A`, con el mismo Q. Con Q alto, un solo numerador no reproduce a la vez el pico y la caída. Se factoriza con unos ceros auxiliares E de Q 0,7071:

  ```
  LS = N/D = (E/D) · (N/E)
  ```

  N/E es una campana relativa en la frecuencia de los ceros: una atenuación, que se diseña como inversa de su realce. Vale exactamente 1 con Q = 0,7071, de modo que pasar de una a dos secciones no hace saltar nada. E/D, con los polos resonantes y ceros suaves, se ajusta a `|N/D| / |N/E|` en Nyquist y en ωm, así que el producto es exacto en continua, en Nyquist y en el polo. Si los ceros se acercan a Nyquist, N/E se funde con la unidad entre 0,9π y 0,98π (su Q tiende a 0,7071), porque por encima sus polos ya no tienen imagen digital. Esa parte de la respuesta analógica la sigue aproximando E/D, que se ajusta al prototipo entero en Nyquist.

### Medidas

Peor diferencia con el prototipo analógico, en dB, de 20 Hz a 20 kHz (o hasta 0,4999·fs). Se toma en el rango de uso habitual: frecuencias de 1 a 16 kHz, Q de 0,3 a 8, ±6 y ±18 dB, y en los cortes, pendientes de 12, 24 y 48 dB/oct. En cada celda, Classic · Matched. No cuenta donde los dos están por debajo de −40 dB.

| Tipo | 44,1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| Bell | 10,72 · 1,75 | 7,61 · 1,37 | 1,66 · 0,36 | 0,40 · 0,09 |
| LowShelf, HighShelf | 20,71 · 3,14 | 19,54 · 0,74 | 5,82 · 0,31 | 1,57 · 0,09 |
| LowCut | 34,66 · 1,63 | 26,49 · 1,25 | 4,83 · 0,14 | 1,13 · 0,01 |
| HighCut | 87,17 · 3,76 | 55,85 · 3,69 | 7,47 · 0,30 | 1,68 · 0,02 |
| Notch | 13,60 · 5,86 | 10,54 · 4,01 | 2,10 · 0,31 | 0,50 · 0,02 |
| BandPass | 15,43 · 2,61 | 11,12 · 2,06 | 2,02 · 0,45 | 0,48 · 0,12 |
| Tilt | 9,15 · 1,77 | 6,40 · 1,37 | 1,05 · 0,35 | 0,24 · 0,09 |

Los peores casos de Matched son siempre bandas anchas a 16 kHz y 44,1 kHz, con buena parte de su respuesta analógica por encima de Nyquist. El paso todo conserva magnitud 1 con los dos. Por debajo de fs/24, Matched sigue al analógico en 0,02 dB y, donde la banda suena (por encima de −12 dB), a Classic en 0,05 dB. Cambiar de algoritmo no mueve los graves.

Fuera de ese rango, con Q 30 y ganancias de ±30 dB, Matched también mejora a Classic en todos los tipos medidos salvo uno: el corte de graves con Q 0,1 a 19 kHz y 96 kHz (3,7 dB frente a 2,5).

Lo comprueban `tests/dsp/filter_algorithm_test.cpp` (la tabla, con un 20 % de margen, y que Matched siempre mejora a Classic), la medida sobre el audio procesado, la estabilidad con parámetros extremos, la continuidad al mover frecuencia, Q y ganancia, y la cancelación exacta de realce y atenuación.

### Coste

Diseñar una banda con Matched cuesta de 130 a 370 ns, frente a unos 30 ns de Classic: hacen falta `exp`, `expm1`, `sin` y `sqrt`, y los shelves resonantes diseñan dos secciones. Con parámetros fijos no se nota. En las bandas dinámicas, que rediseñan hasta una vez por muestra, sí. Medido con 24 bandas estéreo a 48 kHz, en % de un núcleo:

| Caso | Classic | Matched |
|---|---|---|
| 24 campanas | 0,8 | 0,8 |
| 24 cortes de 96 dB/oct | 6,3 | 6,0 |
| 24 shelves con Q 2 (dos secciones) | 0,85 | 1,8 |
| 24 campanas dinámicas comprimiendo | 4,3–4,7 | 5,1–5,3 |
| 24 shelves dinámicos comprimiendo | 4,1–4,6 | 4,8–4,9 |

## Contrato de sonido

`tests/fixtures/dsp/respuesta_v1.txt` guarda la respuesta de 540 configuraciones con Classic, y `respuesta_matched.txt` la de 1080 con Matched (las mismas, más frecuencias y sondas cerca de Nyquist). `tests/dsp/filter_golden_test.cpp` falla si alguna cambia más de 0,001 dB. Además, `tests/dsp/filter_algorithm_test.cpp` compara Classic bit a bit con una copia literal del código de la 0.1. Si una mejora cambia el sonido a propósito, no se regenera ninguno de los dos archivos: se añade un algoritmo nuevo junto a los antiguos, se guarda en el estado cuál usa cada instancia y se crea un archivo de referencia nuevo.

## Referencias

- V. Zavalishin, *The Art of VA Filter Design*. Teoría de los filtros TPT.
- A. Simper (Cytomic), *Solving the continuous SVF equations using trapezoidal integration and equivalent currents* y *SVF linear trapezoidal optimised*: https://cytomic.com/technical-papers/. De ahí salen la estructura y los coeficientes de mezcla.
- R. Bristow-Johnson, *Audio EQ Cookbook*: https://www.w3.org/TR/audio-eq-cookbook/. Referencia cruzada en los tests.
- M. Vicanek, *Matched Second Order Digital Filters* (2016) y *Matched One-Pole Digital Shelving Filters* (2019): https://www.vicanek.de/articles.htm. Base del algoritmo Matched.
