# 0026. Saturación: sobremuestreo hasta unos 384 kHz con filtros de media banda (IIR o de fase lineal según el modo de fase), sin ADAA, compensación del drive en dB y mezcla dentro

**Estado:** Aceptada (noviembre de 2026), con las medidas del bloque 6.7 del [plan](../../PLAN.md). La elección del sobremuestreo la ha decidido el usuario con estas medidas. Falta la integración (bloque 6.8) y la interfaz (bloque 6.9).

## Contexto

La sección 5.8 del plan pide una etapa global de saturación suave tras el ecualizador (Soft, `tanh(d·x)`, y Warm, `tanh(d·x + b) − tanh(b)` con un bloqueador de continua), con tres criterios que deciden cómo se evita el aliasing:

- **Aliasing:** con un seno de 10 kHz, 12 dB de drive y 48 kHz, 80 dB por debajo de la fundamental.
- **Coste:** menos del 2 % de un núcleo a 48 kHz en estéreo.
- **Agudos:** con la mezcla al 0 %, menos de 0,1 dB de caída hasta 18 kHz.

El plan proponía sobremuestreo ×4 en dos etapas ×2 de media banda, y pedía medirlo antes frente a ADAA de primer orden (Parker, Zavalishin y Le Bivic, 2016; Bilbao y otros, 2017), sin sobremuestreo y con ×2. Había que decidir:

1. El método contra el aliasing y su factor.
2. Los filtros de cada modo de fase.
3. Los detalles que fijan el sonido: la polarización de Warm, el bloqueador, la compensación y las rampas.

## Medidas

Con `esceq_bench --saturation` (que implementa también las alternativas descartadas) en el equipo de desarrollo, Release, y los tests de `tests/dsp/saturation_test.cpp`. Detalle, método y tablas completas en [docs/dsp/saturacion.md](../dsp/saturacion.md).

| Alternativa (Soft, 12 dB de drive, 48 kHz) | Aliasing a −18 / −6 / 0 dBFS | Mezcla al 50 %, 18 kHz | % de un núcleo |
|---|---|---|---|
| Sin sobremuestreo | −34 / −15 / −10,5 dB | 0 | 0,15 |
| ADAA | −46 / −25 / −19 dB | −4,4 dB (la húmeda, −8,3) | 0,25–0,45 |
| ×2 IIR + ADAA | −135 / −81 / −55 dB | −1,1 dB (la húmeda, −1,6) | 0,6 |
| ×4 IIR | −131 / −103 / −58 dB | 0 | 0,7–1,0 |
| ×4 IIR + ADAA | −130 / −114 / −79 dB | −0,28 dB (la húmeda, −0,38) | 1,1–1,4 |
| ×8 IIR | −130 / −114 / −110 dB | 0 | 1,4–1,8 |

El criterio no dice a qué nivel se mide, y es lo que decide: con 12 dB de drive, a 0 dBFS `d·x` llega a 4 y la curva es casi una onda cuadrada. ×4 cumple hasta unos −4 dBFS de pico; a 0 dBFS se queda en −58 dB, y es la propia curva, no los filtros (un ×4 ideal da lo mismo). ADAA, sin sobremuestreo o con ×2, no cumple, y además es un filtro paso bajo para la señal con media muestra de retardo.

## Decisión

1. **Sobremuestreo hasta unos 384 kHz, sin ADAA:** ×8 hasta 50 kHz, ×4 hasta 100 kHz y ×2 por encima (con el `sampleRateFactor()` de la fase lineal), en etapas ×2 de media banda. Cumple el criterio a cualquier nivel (−110 dB a 0 dBFS, y −116 dB en fase lineal) y en todas las frecuencias de muestreo con el mismo aliasing y el mismo coste. Lo ha elegido el usuario entre:
   - ×4 a todas las frecuencias, como proponía el plan, con el criterio medido a −6 dBFS o menos: la mitad de coste a 48 kHz, pero −58 dB a 0 dBFS y el cuádruple de coste a 192 kHz (3,1 %).
   - ×4 escalado (siempre unos 192 kHz dentro): el coste de ×4 a cualquier frecuencia, con su aliasing.
   - ×4 con ADAA: −79 dB a 0 dBFS, sin llegar, por el coste de ×8 y perdiendo 0,38 dB a 18 kHz en la húmeda.
2. **Filtros:** en modo IIR, polifásicos IIR pasa todo (Valenzuela y Constantinides, 1983), sin latencia que comunicar (con 6 muestras de retardo de grupo en graves a 48 kHz); en los modos lineales, FIR de media banda de fase lineal (Parks-McClellan), con 47 muestras de latencia hasta 50 kHz (0,98 ms a 48 kHz), 43 hasta 100 kHz y 35 por encima. Los dos con −103 dB o más en la banda eliminada y planos hasta 0,41·fs (18 kHz a 44,1 kHz). Los coeficientes están fijos en el código.
3. **La mezcla se hace dentro**, con la seca sobremuestreada por los mismos filtros: no hay peine, y con la mezcla al 0 % la salida es la entrada por los filtros (0,000 dB a 18 kHz).
4. **Warm, con b = 0,25:** el segundo armónico, 15 dB por encima del tercero con poco drive (−30,5 y −47,4 dB con el drive por defecto, 6 dB, a −18 dBFS) y parejos con mucho.
5. **El bloqueador de continua (un polo a 5 Hz, en double) va siempre, en Soft y en Warm**, solo en la húmeda: así pasar de una curva a otra no lo enciende de golpe. En Soft no quita nada a una entrada sin continua.
6. **Compensación:** la ganancia que la curva da a un seno de −18 dBFS en valor eficaz (sin la continua), calculada con 256 puntos de un periodo en el hilo de audio cuando cambia el drive o la curva. Ese seno sale a +0,0002 dB con cualquier drive.
7. **Rampas de 20 ms**, con el drive y la compensación en dB: en lineal, su producto se pasaba de los dos extremos durante la rampa (una subida de varios dB al pasar de 6 a 24 dB). Encender y apagar es un fundido de 20 ms con la entrada tal cual; con Off, terminado el fundido, la etapa no se ejecuta.
8. **Una tanh propia, sin ramas**, en float: impar exacta, 1,1·10⁻⁷ de error como mucho y el mismo resultado en todos los sistemas, que el compilador vectoriza (con `-fno-trapping-math` en GCC solo para `saturation.cpp`, que no cambia ningún resultado). Con la de la biblioteca, los canales por separado y los FIR muestra a muestra, el motor costaba del 1,6 al 1,9 % en IIR y del 2,5 al 3 % en fase lineal a 48 kHz.

## Consecuencias

- Coste del motor a 48 kHz, en estéreo: del 0,7 al 1,3 % de un núcleo con GCC y del 0,9 al 1,2 % con Clang, en los dos modos y con los dos filtros; a 192 kHz, del 1 al 1,4 %. Con Off, nada.
- En fase lineal, encender la saturación (o apagarla) cambia la latencia en 47 muestras a 48 kHz: el bloque 6.8 lo hace como el cambio de modo de fase, con silencio. En IIR no hay latencia, pero los filtros cambian la fase en agudos mientras la saturación no esté en Off, aunque la mezcla esté al 0 %.
- La compensación iguala el nivel a −18 dBFS; con señales mucho más fuertes y mucho drive, la salida queda por debajo de la entrada.
- El bloqueador quita la continua y lo que hay por debajo de unos 5 Hz de la húmeda; la seca la conserva.
- Los coeficientes, b, el bloqueador, la compensación y la tanh fijan el sonido: una vez publicados, cambiarlos exige una versión de algoritmo en el estado (sección 2.1 del plan).
- Queda para el 6.8: los tres parámetros (388 a 390), el lugar en `PhaseEqualizer` (tras la dinámica espectral, antes de la ganancia de salida) y la latencia.
