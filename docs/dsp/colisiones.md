# Colisiones entre pistas

Cómo decide esceQ dónde "se pisan" dos pistas. Código en `dsp/include/esceq/dsp/spectrum_collision.h` (algoritmo), `core/include/esceq/core/spectrum_session.h` (de dónde salen los espectros) y `ui/logic/include/esceq/ui/session_overlay.h` (varias instancias a la vez). Las decisiones y sus motivos, en la [ADR 0011](../decisiones/0011-espectros-compartidos.md).

## De dónde salen los espectros

```
processBlock ─► mezcla a mono ─► cola sin bloqueo ─┐   (una por instancia, solo si alguien mira)
                                                    ▼
                    hilo de la sesión, cada 50 ms: FFT de 8192 (Hann) ─► 192 puntos logarítmicos de 20 Hz a 20 kHz
                                                    │                    (máximo de los bins en ±1/24 de octava,
                                                    │                     inclinación de 4,5 dB/oct, 0 dB en 1 kHz)
                                                    ▼
                                    promedio en potencia, τ = 0,4 s ─► publicado en la sesión
```

- **La misma escala que el analizador de la interfaz:** un seno de amplitud 1 se lee a 0 dB en 1 kHz, con la misma inclinación. Lo que se ve superpuesto es comparable con el analizador propio.
- **FFT de 8192 puntos** (5,9 Hz por bin a 48 kHz): con 4096, a 40 Hz dos bins están a un tercio de octava y los graves se emborronan. Es justo donde más colisiones hay (bombo y bajo).
- **Promedio en potencia de 0,4 s** (`SpectrumAverager`): interesa lo que suena de forma sostenida. Un golpe de bombo no debe marcar una colisión permanente, ni una nota que acaba de parar debe desaparecer de golpe. Promediar en potencia, y no en dB, da el nivel medio real.
- **Sin audio con nivel visible** durante 0,5 s (transporte parado, pista muda), la instancia deja de "tener señal" y su espectro se apaga con la misma constante de tiempo.

## Severidad de la colisión

Para la instancia propia `A` y otra `B`, con sus espectros suavizados en potencia a 1/3 de octava (`smoothOctaves`, una banda crítica aproximada: el oído no separa lo que cae dentro), en cada punto `f`:

```
severidad(f) = presencia_A(f) · presencia_B(f) · enmascaramiento(B(f) − A(f))
```

**Presencia** (`presence`): una señal está presente si supera los dos umbrales

- el suelo absoluto `floorDb` = −66 dB, y
- su propio máximo menos `relevanceRangeDb` = 30 dB,

con una rampa suave (*smoothstep*) de `presenceRampDb` = 6 dB por encima del mayor de los dos. El umbral relativo evita que la cola lejana de una pista (los agudos de un bajo a −50 dB de su cuerpo) cuente como colisión con lo que sí suena ahí.

**Enmascaramiento** (`maskingFactor`): función de la diferencia `d = B − A` en dB, con rampa suave entre `−maskingWindowDb` = −15 dB (0: `B` queda tan por debajo que no tapa a `A`) y 0 dB (1: `B` igual o más alta).

| `d = B − A` | Enmascaramiento |
|---|---|
| ≤ −15 dB | 0 |
| −7,5 dB | 0,5 |
| ≥ 0 dB | 1 |

La medida es **direccional**: dice cuánto tapa la otra pista a la propia. Si el bajo está 20 dB por encima de la guitarra en 100 Hz, la instancia de la guitarra marca colisión y la del bajo no. Para ecualizar por ranuras es lo útil: cada pista ve lo que le quita sitio.

**Varias instancias** (`computeCollisions`): se calcula la severidad con cada una; lo que se sombrea es el máximo en cada punto. Los tramos se buscan **con cada instancia por separado** (`findCollisionRegions`: puntos contiguos con severidad de al menos 0,35, uniendo los separados por menos de 1/6 de octava). Si se buscaran sobre el máximo, un bajo que choca hasta 300 Hz y unos teclados que chocan desde ahí formarían un solo tramo, con el nombre de uno y el pico del otro. Cada tramo pesa su severidad integrada en octavas: una zona ancha y moderada importa más que un pico estrecho, y es la que primero se etiqueta. En una meseta de severidad máxima, la frecuencia de la etiqueta es el centro de la meseta.

## Cómo se dibuja

- Los espectros de las instancias elegidas, como líneas del color de su pista (o uno propio si el host no lo da). Atenuados si no tienen señal.
- La severidad, como un velo rojo (`colour::danger`) por columnas, más intenso en una franja en el borde inferior.
- Hasta cuatro etiquetas ("Bass · 120 Hz"), de la zona más severa a la menos, en una fila arriba. Una etiqueta que se solaparía con otra más importante no se pone (`placeLabels`). Los tramos de menos de 1/6 de octava se sombrean pero no se etiquetan.

## Límites

- Es una indicación visual del enmascaramiento, no un modelo psicoacústico completo: no tiene en cuenta el enmascaramiento temporal, el ensanchamiento asimétrico de las bandas críticas hacia los agudos ni la diferencia entre ruido y tono como enmascarador.
- La rejilla es la del analizador (192 puntos, unos 19 por octava): una colisión más estrecha que eso solo se ve si un tercio de octava a su alrededor la acusa.
- Los umbrales son valores por defecto razonables, no parámetros: se ajustarán con uso real.

## Tests

- `tests/dsp/spectrum_collision_test.cpp`: suavizado (conservación de potencia, extremos), presencia, enmascaramiento, severidad con espectros sintéticos y con el analizador real (dos senos a la misma frecuencia chocan; separados, no), tramos y promedio temporal (constante de tiempo exacta).
- `tests/core/spectrum_session_test.cpp`: registro, numeración, ciclo de vida, nada se procesa sin mirar, calibración del espectro publicado, apagado sin audio, el hilo de fondo y un test de concurrencia para ThreadSanitizer.
- `tests/ui/session_overlay_test.cpp`: menú, atribución de zonas con varias instancias, texto y colocación de etiquetas.
- `tests/plugin/session_test.cpp`: el procesador publica su salida (con ecualizador, ganancia de salida y bypass), el nombre y el color de la pista, y que nada de esto entra en el estado guardado.
