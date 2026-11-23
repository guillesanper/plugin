# Igualar a una referencia

Cómo convierte `dsp::matchCurve` (`dsp/curve_match.h`) dos espectros medios, el de la pista y el de una referencia, en las campanas y shelves que acercan la pista a la referencia ([PLAN.md](../../PLAN.md) 5.6). Las decisiones y sus alternativas están en la [ADR 0023](../decisiones/0023-igualar-a-una-referencia.md). Aquí está sobre todo el ajuste (bloque 6.1); al final, cómo se aprenden los espectros y quién llama al ajuste (bloque 6.2, `core/match.h` y `ui/logic/match.h`), con la medida de extremo a extremo.

`matchCurve` es puro: no tiene estado, ni hilos, ni azar, y calcula en `double` y en un orden fijo. La misma entrada da bit a bit las mismas bandas con el mismo binario; entre sistemas, `libm` puede cambiar el último bit. Asigna memoria y tarda hasta un segundo, así que nunca se llama en el hilo de audio.

## Entrada

- **Los dos espectros**, en dB, en una rejilla creciente común: la de la sesión de espectros, 192 puntos de 20 Hz a 20 kHz (`core::SpectrumSession`). `dsp` no ve `core`, así que la rejilla entra como argumento. Los dos se miden igual (el analizador de 8192 puntos y su promedio), y sus sesgos (el máximo por punto, la inclinación de 4,5 dB/oct) se anulan al restar. Un nivel no finito no es un dato: ni ese punto ni los que lo suavizan cuentan.
- **La resolución del análisis** (`resolutionHz`): la separación entre bins de la FFT que los midió, la frecuencia de muestreo entre 8192 (5,86 Hz a 48 kHz). Sirve para ensanchar el suavizado en graves (abajo).
- **Las bandas actuales**: se ajusta lo que falta sobre su curva estática, vista al ancho del suavizado (abajo). `dsp::staticCurveDb` (`auto_gain.h`) da esa curva con la regla de canales de la compensación de ganancia ([ADR 0015](../decisiones/0015-compensacion-automatica-de-ganancia.md)): `|H|²` en mono o con bandas Stereo; con bandas en L, R, M o S, la media de la potencia de los dos canales de salida. Solo cuenta la ganancia estática, como allí.
- **La frecuencia de muestreo y el algoritmo de filtro** de la instancia: las curvas de las bandas nuevas son las de `designBand` y `magnitudeDbAtTangent` con esos dos, así que lo que se ve es lo que suena.
- **Los ajustes del panel**: cantidad (0 a 1), suavizado, bandas como mucho y rango (30 Hz a 16 kHz por defecto).

## Objetivo

```
D(f) = suav(R)(f) − suav(P)(f)                      R: referencia, P: pista, en dB
T(f) = clamp( cantidad · (D(f) − mediana_w(D)), ±12 dB )
N(f) = T(f) − S(C)(f)                               C: curva estática de las bandas actuales;
                                                    S: su promedio en potencia en la ventana del suavizado
```

1. **Suavizado.** Cada espectro se suaviza en potencia con `dsp::smoothOctaves` al ancho elegido. En graves, la ventana mide al menos 8 bins de la FFT (`kMatchMinWindowBins`, con el parámetro `minWidthHz` que se ha añadido a `smoothOctaves`; con 0, da lo mismo que antes, bit a bit). Con 8192 puntos a 48 kHz, eso deja 1/3 de octava igual por encima de unos 200 Hz y la ensancha por debajo: 0,67 octavas a 100 Hz, 1,3 a 50 Hz. Los motivos están en la prueba nula, más abajo.
2. **Presencia.** Un punto cuenta si **las dos** señales están presentes en él, con `dsp::presence` y los umbrales de las colisiones sobre los espectros suavizados (−66 dB, o 30 dB por debajo del máximo de cada una). Tampoco cuentan los puntos a menos de un ancho de ventana de uno que no esté presente: su valor suavizado mezcla lo que hay con lo que no, y en el borde de una referencia sin agudos sale un falso corte de 2 a 3 dB.
3. **Rejilla del ajuste.** Son 384 puntos logarítmicos entre la primera y la última frecuencia de la entrada, el doble de densa que la de la sesión (1/38 de octava), con los dos espectros interpolados en log f, para que una campana estrecha no caiga entre dos puntos. Un punto intermedio cuenta si cuentan sus dos vecinos.
4. **Pesos.** Dentro del rango, 1 donde las dos están presentes y 0 donde no. Fuera, 0,1 (`kMatchOutsideWeight`). Entre los dos hay una transición suave de 1/6 de octava.
5. **Centrado: la mediana.** El nivel no se iguala, solo el balance, así que la diferencia se centra. Se centra en su **mediana** ponderada (en log f), no en la media: se iguala lo que se aparta de lo más común. Con la media, una referencia con más graves y más agudos sube el «cero» y el ajuste pide cortar todos los medios. Es el mismo balance, pero no las bandas que pondría nadie. Medido con un low shelf de +5 dB, dos campanas y un high shelf de +4 dB: con la media salen cuatro campanas de corte (una de −6,6 dB de Q 0,46 en los medios); con la mediana, los dos shelves y las dos campanas.
6. **Fuera del rango**, lo que falta es lo del borde (el punto que cuenta más cercano), con peso 0,1: las bandas nuevas no crean nada ahí, pero un shelf puede seguir. Con 0 fuera, como decía el plan, un solo low shelf de +6 dB a 100 Hz sale como tres shelves con el rango por defecto, porque el tramo de 20 a 30 Hz tira del shelf hacia 0.
7. **Límite.** La cantidad se aplica antes de limitar a ±12 dB, así que al 50 % la propuesta es la mitad que al 100 % mientras nada llegue al límite.
8. **La curva actual, al mismo ancho.** T está suavizado y C no lo estaba, así que una banda estrecha que ya hay se veía entera en lo que falta y el ajuste la rellenaba (abajo, «Bandas estrechas que ya hay»). Ahora C se compara suavizada en potencia con la misma ventana que los espectros (el ancho elegido, con el mínimo de los graves), en la rejilla del ajuste: S(C).

## Ajuste

Se minimiza

```
Σ w(f)·(N(f) + c − [S(C + Σ Hk)(f) − S(C)(f)])²  +  Σw·(10⁻⁴·Σ gk² + 0,1·c²)
```

donde Hk es la curva en dB de la banda k (en la rejilla, con las tangentes de `responseTangent` calculadas una vez) y c es un **nivel casi libre**. Lo que se compara con lo que falta es lo que añaden las bandas nuevas a la curva actual **visto al mismo ancho**: dentro del rango, el residuo es T + c − S(C + Σ Hk), la curva total suavizada frente al objetivo. Así, aplicar la propuesta y volver a pedirla ve lo mismo que dejó el primer ajuste, y la segunda vez no hay nada que añadir. Sin suavizado (ni mínimo en graves), la ventana es el propio punto y el residuo es el de antes, N + c − Σ Hk. La mediana no siempre cae en el nivel que mejor encaja (con campanas anchas queda unas décimas lejos), y c lo absorbe. Está penalizado para que no se vaya a donde los realces de los extremos salen como cortes de los medios: con 0,01 en vez de 0,1, el caso de los cuatro filtros vuelve a dar siete bandas, y con 0,001, cortes anchos con c = −3,6 dB. La penalización de las ganancias (10⁻⁴ dB² por dB²) evita que dos campanas anchas de ±18 dB casi iguales se resten para dibujar un detalle (pasaba en un objetivo de 24 campanas superpuestas); a las bandas normales casi no las toca.

**Inicio voraz.** En el punto del rango con más error ponderado se coloca una banda. Su lóbulo es el tramo alrededor con el mismo signo y más de la mitad de ese error, sin cruzar puntos de peso 0. Si el lóbulo llega a un extremo del rango, la banda es un **shelf**: su frecuencia es el borde interior del lóbulo, donde el error cruza la mitad (allí un shelf del *Audio EQ Cookbook* tiene la mitad de su ganancia), su ganancia es la del extremo y su Q, 0,707. Si no, es una **campana** en el máximo, con su ganancia y el Q del ancho del lóbulo (`dsp::qForBandwidth`, que viene de `ui/logic`, ADR 0019). Después se refinan todas las bandas juntas (20 iteraciones como mucho) y se repite.

**Levenberg-Marquardt** (Madsen, Nielsen y Tingleff, 2004, algoritmo 3.16), en las variables naturales: log2 de la frecuencia, ganancia en dB y log del Q de cada banda, y c.

- El jacobiano es por diferencias centrales (paso de 10⁻⁴), y de cada variable solo cambia la curva de su banda: seis curvas por banda en cada paso aceptado. La derivada del suavizado en potencia es la media de ∂Hk/∂θ en la ventana, ponderada por la potencia de C + Σ Hk: ∂S_i/∂θ = Σ_j p_j·∂Hk_j/∂θ / Σ_j p_j (la curva se limita a ±200 dB antes de pasarla a potencia, para el centro de un notch).
- El paso resuelve (JᵀJ + μI)·h = −Jᵀe por Cholesky, con μ inicial de 10⁻³ por la diagonal mayor y la actualización de Nielsen.
- Los límites se proyectan en cada paso: la frecuencia dentro del rango (y por debajo de 0,49·fs), ±18 dB, Q de 0,3 a 16 en campanas y de 0,5 a 1 en shelves. El Q de una campana tampoco pasa del de una que abarca la ventana del suavizado en su frecuencia (`dsp::matchMaxBellQ`: 4,3 con 1/3 de octava, 8,6 con 1/6, 1,4 con 1 octava, menos en graves con el mínimo de 8 bins). El objetivo no ve nada más estrecho, y con el ajuste suavizado, sin ese límite, lo mismo se dibuja con campanas estrechas y altas: apilaba tres campanas de Q 4 a 43 Hz, y rellenaba el notch de abajo con +8 dB de Q 7.
- La reducción prevista se calcula con el paso ya proyectado.
- Se para con el gradiente o el paso casi nulos, o al acabar las iteraciones.

**Parada y limpieza.** No se añaden más bandas cuando:

- ningún punto del rango tiene un error ponderado de 0,5 dB o más (`kMatchStopErrorDb`);
- se llega a las bandas que se pueden crear;
- o la banda nueva, ya refinada, quita menos de 0,05 dB de error RMS, medido como √(E²antes − E²después) con las penalizaciones (`kMatchMinImprovementDb`); esa banda se descarta.

Después hay un refino final de 100 iteraciones, se quitan las bandas de menos de 0,3 dB, se vuelve a refinar si se ha quitado alguna, y las bandas se ordenan de grave a agudo.

Dos cambios sobre el plan, los dos por medidas:

- **El plan paraba con un error RMS de 0,5 dB.** El RMS de toda la rejilla diluye un detalle estrecho. Al 50 %, una campana de −1,5 dB de Q 2 quedaba sin igualar, y la propuesta no era la mitad de la del 100 % (se apartaba 1 dB).
- **El plan medía la mejora como la diferencia de los RMS.** Esa diferencia es más pequeña cuanto más error queda en el resto: con 24 campanas pedidas, paraba a las 4 bandas, con 1,2 dB de error.

El resultado lleva las bandas (con el algoritmo de la entrada y la pendiente por defecto), c, el error RMS antes y después, y en la rejilla del ajuste los pesos, T, C (sin suavizar) y la curva propuesta (C más las bandas, sin suavizar), para dibujarlas (bloque 6.3). También lleva el número de evaluaciones (curvas de una banda en toda la rejilla) y de iteraciones: el coste, sin depender de la máquina. Con un `std::atomic<bool>` de cancelación, vuelve tras la iteración en curso, sin bandas.

## Precisión

Referencia igual a la pista (plana) por unas bandas conocidas, en la rejilla de la sesión, al 100 %, con el rango por defecto y la resolución de 8192 puntos a 48 kHz. El error es el de la curva propuesta frente a la conocida dentro del rango, sin contar el nivel.

| Objetivo | Suavizado | Bandas | Error RMS (máximo) |
|---|---|---|---|
| Campanas de +4 dB Q 1 a 200 Hz, −3 dB Q 2 a 2 kHz y +5 dB Q 0,7 a 8 kHz | 1/6 | 3 | 0,15 dB (0,34) |
| ídem | 1/3 | 3 | 0,15 dB (0,35) |
| ídem | 1 octava | 3 | 0,21 dB (0,48) |
| Low shelf de +6 dB a 100 Hz | 1/6 y 1/3 | 1 | 0,004 dB (0,01) |
| Low shelf de +5 dB a 120 Hz, campanas de −4 dB Q 1,5 a 900 Hz y +3 dB Q 2 a 3,5 kHz, high shelf de +4 dB a 9 kHz | 1/3 | 4 (los dos shelves y las dos campanas) | 0,17 dB (0,37) |
| ídem, al 50 % | 1/3 | 4 | 0,09 dB (0,19); a menos de 0,02 dB de la mitad de la del 100 % |

Con la curva actual al mismo ancho (punto 8 del objetivo) el ajuste compara la curva suavizada de las bandas con el objetivo, que también lo está, así que recupera mejor las bandas que lo generaron: el shelf pasa de 0,05 a 0,004 dB, y con 1 octava, las tres campanas de 0,31 dB (hasta 1 dB) a 0,21 dB (hasta 0,48). Lo demás queda igual.

Las bandas son las de la referencia en tipo y frecuencia (a menos de 0,03 octavas). Sus ganancias y Q se apartan algo, porque el nivel no se iguala: con las tres campanas sale c = 0,57 dB y ganancias de 3,7, −3,2 y 4,6 dB.

Los tests (`tests/dsp/curve_match_test.cpp`) comprueban:

- las tres campanas (como mucho 6 bandas y error < 0,5 dB) y el shelf (una banda y < 0,3 dB);
- que un objetivo plano, o con un rizado de ±0,3 dB, no crea bandas;
- que al 50 % la propuesta es la mitad de la del 100 %, con ±0,2 dB;
- que con las bandas actuales iguales a lo que falta, o tras aplicar una vez, no se crea nada;
- que una banda estrecha que ya hay (el notch de `tools/snapshot`) cuenta por lo que quita al ancho del suavizado, que lo propuesto no pasa del Q de la ventana (`matchMaxBellQ`) y que aplicar dos veces no crea nada, con 1/6, 1/3 y 1 octava y al 50 y al 100 %;
- la regla de canales de la curva actual;
- que lo que no tiene contenido no cuenta (una referencia sin nada por encima de 10 kHz) y que, sin contenido común, no se crea nada;
- los pesos del rango, y que la propuesta es la curva de las bandas con el algoritmo y la frecuencia dados (Classic y Matched, 44,1 y 96 kHz);
- el ancho mínimo en graves, el determinismo bit a bit, el límite de evaluaciones, la cancelación y las entradas no válidas.

## Bandas estrechas que ya hay

Encontrado en el bloque 6.3: con las bandas de `tools/snapshot` (un corte de graves a 35 Hz de 24 dB/oct, dos campanas, un high shelf y un notch de Q 8 a 5,2 kHz) y su referencia (pista plana; la referencia, con un low shelf de −4 dB a 120 Hz, una campana de +3 dB a 2,5 kHz y un high shelf de +5 dB a 7 kHz), la propuesta llevaba una campana de +17,7 dB y Q 16 a 5194 Hz que deshacía el notch. El objetivo está suavizado y la curva actual no: N = T − C veía el notch entero.

**Decisión** (con el usuario, ADR 0023): una banda estrecha que ya hay cuenta por **la energía que quita al ancho del suavizado**, que es lo que el objetivo puede ver. Con 1/3 de octava, un notch de Q 8 quita unos 3,7 dB de la potencia de su tercio de octava. Es coherente con cómo se mide el objetivo. Y si el notch quita una resonancia de la pista, la pista ya tiene ese exceso en su espectro (se mide antes del ecualizador), y lo que falta casi se anula. La alternativa, no contar lo que sea más estrecho que la ventana, pondría encima un corte ancho en ese caso (dos veces lo mismo), y con una mediana el ajuste dejaba de poder repetirse sin crear bandas.

Medido con ese caso, el rango por defecto y la resolución de 8192 puntos a 48 kHz (`tests/dsp/curve_match_test.cpp`, `esceq_bench --match`, objetivo *notch*):

| Suavizado | Antes, junto al notch | Ahora, junto al notch | Aplicar y volver a pedir |
|---|---|---|---|
| 1/6 | +17,8 a 17,9 dB, Q 16 | +10,1 a 10,8 dB, Q 8,6 | nada |
| 1/3 | +17,8 a 17,9 dB, Q 16 | +5,7 a 6,2 dB, Q 4,3 | nada |
| 1 octava | +17,8 a 18 dB, Q 16 | +1,9 dB, Q 1,4 (o un high shelf de +2,4 dB) | nada |

(Al 50 y al 100 %; antes, la segunda vez salían de 0 a 3 bandas más.) Además, en graves ya no se apilan cinco low shelves de 0,5 a 4,3 dB entre 31 y 34 Hz para compensar el corte de 35 Hz: sale uno de +2,9 a +4,4 dB (o, al 100 % con 1/6, una campana de +2,5 dB a 30 Hz), y la segunda vez no pide nada.

**Aplicar dos veces, en general** (`esceq_bench --match`, 300 casos aleatorios de una a cuatro bandas en la referencia y hasta tres en la pista, la mitad estrechas, con 1/6, 1/3 y 1 octava, al 50 y al 100 %): la segunda vez sale algo en el 2,0 % de los casos, con 2,3 dB como mucho; antes, en el 9,3 %, con hasta 10,4 dB. Lo que queda viene del refino final y de la limpieza de la primera vez, que dejan un error que una banda nueva aún puede reducir más de 0,05 dB.

## Prueba nula: qué se puede medir con 8192 puntos

Dos ruidos rosas independientes del mismo espectro, medidos como en la sesión (analizador de 8192 puntos a 48 kHz cada 50 ms, promedio de 0,4 s y media en potencia del promedio), con el ajuste al 100 % y 8 bandas como mucho. Lo ideal es que no salga ninguna banda. En la tabla, el |T| máximo por debajo de 60 Hz, de 60 a 200 Hz y por encima, y las bandas que salen, con tres semillas a 10 s:

| Suavizado | Sin ancho mínimo en graves | Con 8 bins como mínimo |
|---|---|---|
| Ninguno o 1/12 | 0,8 / 1,5 / 0,9 dB; 3 a 8 bandas de hasta 2,3 dB | 0,6 / 0,9 / 0,8 dB; 0 a 6 bandas |
| 1/6 | 0,8 / 1,5 / 0,8 dB; 4 a 6 bandas | 0,6 / 0,9 / 0,6 dB; 0 a 4 bandas |
| 1/3 | 0,7 / 1,0 / 0,4 dB; 1 a 3 bandas, todas por debajo de 150 Hz | 0,6 / 0,9 / 0,4 dB; 0 o 1 banda (78 Hz, −0,9 dB) |
| 1 octava | 0,7 / 0,9 / 0,3 dB; 0 o 1 banda | 0,7 / 0,9 / 0,3 dB; 0 o 1 banda |

Con 30 s, a 1/3 de octava y con el mínimo, el máximo baja a 0,4 / 0,4 / 0,3 dB y no sale ninguna banda. Con 5 s, los graves llegan a ±1,2 a 1,3 dB y salen dos bandas falsas (−1,5 dB a 67 Hz y −0,7 dB a 400 Hz). Conclusiones:

- **1/12 de octava no es fiable**, en graves ni en ningún sitio: en la rejilla de la sesión (1/19 de octava entre puntos), una ventana de 1/12 solo tiene el propio punto, así que es lo mismo que no suavizar. Sale de las opciones del panel, que quedan en 1/6, 1/3 (por defecto) y 1 octava.
- **En graves manda la varianza de la medida.** Con pocos bins por ventana, la media de un ruido fluctúa más de lo que el ajuste tolera (0,5 dB). El ancho mínimo de 8 bins lo reduce sin tocar nada por encima de unos 200 Hz con 1/3. Por debajo de 60 Hz, el ajuste solo ve el balance ancho: es lo que la medida permite con 8192 puntos.
- **5 s útiles no bastan para los graves.** El bloque 6.2 pide 10 s antes de *Apply* (en vez de los 5 que decía el plan). Y como la fluctuación baja con la raíz del tiempo, seguir aprendiendo mejora la medida. A la cantidad por defecto (50 %), la fluctuación también se reduce a la mitad.

## Coste

`esceq_bench --match` (Release, Xeon a 2,1 GHz, un núcleo, sin otras cargas; el CI no mide tiempos):

| Objetivo | Algoritmo | Mediana (máximo) de 5 | Bandas | Evaluaciones | Iteraciones |
|---|---|---|---|---|---|
| 24 campanas de Q 4, de ±5 a ±9 dB, separadas 0,36 octavas desde 32 Hz, sin suavizar | Classic, de 44,1 a 192 kHz | 610 a 640 ms (hasta 0,71 s) | 23 | 37 200 a 37 500 | 580 |
| ídem | Matched | 590 a 650 ms (hasta 0,71 s) | 23 | 37 000 a 37 300 | 580 |
| Los cuatro filtros de arriba, 1/3 de octava | Classic | 13 a 35 ms | 4 | 1 060 a 2 300 | 70 a 180 |
| ídem | Matched | 39 a 40 ms | 4 | 2 300 | 180 |
| Las bandas y la referencia de `tools/snapshot`, con el notch, 1/3 de octava | Classic y Matched, a 48 kHz | 53 a 58 ms | 5 | 3 400 | 220 |

Con el ancho mínimo, las dos campanas más graves (32 y 41 Hz) se funden y salen 23 bandas. Sin él (el caso del test), salen 24.

Comparar la curva actual al mismo ancho (punto 8) no cambia el coste de los dos primeros objetivos, que casi no suavizan la curva de las bandas: con el código anterior, en la misma máquina, 580 a 630 ms y 14 a 36 ms. Con suavizado y bandas en la pista cuesta más, porque cada curva se suaviza en su ventana (de unos 13 puntos con 1/3 de octava, más en graves) y el ajuste hace más iteraciones: en los 300 casos aleatorios de arriba, la primera vez tarda 38 ms de media y 251 ms como mucho (antes, 20,5 y 140 ms), y con el notch, 53 ms (antes, 42). El objetivo del plan era menos de 2 s con 24 bandas. El tiempo crece con las evaluaciones: unos 17 µs por evaluación, contando el álgebra de cada paso aceptado (JᵀJ, de 73 × 73 con 24 bandas, sobre 384 puntos). Cada evaluación son 384 respuestas de una banda. En Debug, el test de 24 bandas tarda 3,2 s.

El límite del CI no depende de la máquina: el test de 24 bandas exige como mucho 60 000 evaluaciones y 800 iteraciones (un 50 % y un 40 % por encima de lo medido). Si un cambio del algoritmo lo encarece, falla.

## Aprendizaje y propuesta (bloque 6.2)

- **Los dos espectros se miden igual.** La referencia de otra instancia es lo que publica su sesión (`core::SpectrumSession`, cada 50 ms, su salida). La pista (la cola "pre", antes del ecualizador) y el sidechain se miden en la interfaz con las mismas piezas: `dsp::SpectrumAnalyzer` de 8192 puntos con la inclinación de 4,5 dB/oct y `dsp::SpectrumAverager` de 0,4 s (`ui::MatchController`).
- **Se suma en potencia, ponderado por el tiempo** (`core::MatchLearner`): cada vez que la referencia publica un espectro nuevo y las dos tienen señal (algún punto por encima de −90 dB en el último medio segundo, la regla de la sesión), se suman los dos promedios en potencia, por el tiempo que representa el espectro nuevo. Para otra instancia, es lo que ha avanzado su reloj de audio (`InstanceSpectrum::audioSeconds`, que solo avanza cuando le llega audio: con el transporte parado no se aprende); para el sidechain, el tiempo entre dos refrescos de la interfaz con audio. Un paso nunca pesa más de 0,25 s. La media es la de la señal, porque el promedio es lineal en potencia. Sin olvido: lo aprendido se borra al cambiar de referencia o con *Learn*.
- **La resolución** es la mayor de las dos frecuencias de muestreo entre 8192 (cada instancia publica la suya).
- **La propuesta** se pide al hilo del ajuste de la instancia (`core::MatchWorker`) en cuanto hay 10 s útiles, 150 ms después del último cambio de un ajuste o de una banda, y mientras se aprende, cada vez que se suma un segundo útil. Una petición nueva cancela la anterior en su iteración siguiente, y solo se usa la propuesta de la última petición, si nada ha cambiado desde entonces.

`tests/plugin/match_test.cpp` lo prueba de extremo a extremo con dos procesadores en una sesión con hilo manual: ruido rosa en la pista y el mismo ruido por una curva conocida en la referencia (un low shelf de +4 dB a 120 Hz y campanas de −3 dB a 500 Hz, +4 dB a 2,5 kHz y −3,5 dB a 7 kHz), 10 s simulados y *Apply* al 100 % con 1/3 de octava. Con la referencia como otra instancia salen 4 bandas, y la curva aplicada queda a 0,035 dB de la conocida como mucho, de 50 Hz a 12 kHz, salvo un nivel de −0,03 dB (el nivel no se iguala); con la misma referencia por el sidechain, 4 bandas y 0,041 dB. Antes de comparar la curva actual al mismo ancho eran 0,13 y 0,12 dB. El test pide 1 dB. Es mucho mejor que la prueba nula porque la pista y la referencia son el mismo ruido: la fluctuación de la medida se anula al restar.
