# 0016. Tiempos automáticos en la dinámica: ataque ligado al periodo, relajación de dos etapas y un interruptor por banda

**Estado:** Aceptada (noviembre de 2026)

## Contexto

La sección 5.4 del [plan](../../PLAN.md) pide que una banda dinámica pueda elegir sola su ataque y su relajación según su frecuencia y la señal. Con tiempos fijos, en graves la ganancia sigue la forma de onda (distorsiona) y en agudos un ataque lento deja pasar las sibilantes. El plan fijaba el parámetro (`bNN_autotime`, un interruptor por banda, apagado por defecto), las cifras de partida (ataque de 2 periodos, relajación rápida de 8 periodos y lenta de 10 veces la rápida) y los criterios de aceptación. Al implementarlo quedaban por decidir:

1. Cómo se combinan las dos relajaciones para que un transitorio se suelte deprisa y una señal sostenida no bombee.
2. Qué frecuencia manda y cuándo se recalculan los tiempos.
3. Qué pasa al encender o apagar el interruptor con la banda trabajando.
4. Cómo encaja el parámetro en la tabla, el estado y los presets.
5. Si el umbral automático (sección 4.2) entra en esta misma decisión.

## Decisión

1. **Una envolvente rápida y, detrás, una lenta que la promedia; manda la mayor.** La rápida es el detector de pico de siempre (Giannoulis y otros) con el ataque y la relajación rápida automáticos. La lenta es un filtro de un polo que sigue a la rápida con la constante de tiempo de la relajación lenta, **al subir y al bajar**. La salida es el máximo de las dos: el nivel más alto manda, que es la reducción mayor al comprimir (y el realce mayor al expandir). Un transitorio de 50 ms apenas carga la lenta (con 200 ms, en torno a una quinta parte) y la banda se recupera con la relajación rápida; una señal sostenida la carga del todo y, al parar, la reducción cae con la lenta.

   Se descartó *dos detectores de pico en paralelo con el mismo ataque y distinta relajación*, que es la lectura literal del plan: con la misma entrada y el mismo ataque, el de relajación lenta está siempre por encima del rápido, así que el máximo es siempre el lento y la relajación rápida no hace nada. Con ráfagas de 7 kHz, 12 dB por encima del umbral, se recuperaría al 90 % en unos 250 ms (`200 ms · ln 10^(10,8/20)`), y el criterio pide menos de 150.

   Los tiempos, en `dsp::autoEnvelopeTimes()`, son los del plan:

   | | Fórmula | 80 Hz | 1 kHz | 7 kHz |
   |---|---|---|---|---|
   | Ataque | 2 periodos, de 0,5 a 50 ms | 25 ms | 2 ms | 0,5 ms |
   | Relajación rápida | 8 periodos, 20 ms como mínimo | 100 ms | 20 ms | 20 ms |
   | Relajación lenta | 10 veces la rápida, 1 s como máximo | 1 s | 200 ms | 200 ms |

   Por debajo de 8 Hz la rápida pasaría de 1 s; la lenta nunca es más rápida que ella.

2. **Manda la frecuencia de la banda que suena, ya suavizada, y los tiempos se recalculan en cada tramo de 32 muestras**, donde el ecualizador ya suaviza frecuencia, ganancia y Q (`smoothBand`). La ganancia dinámica no influye: es la misma frecuencia que oye el detector. Recalcular son tres exponenciales, y solo cuando la frecuencia cambia. Al ir por tramos del ecualizador y no por bloques del host, el resultado no depende del tamaño de bloque.

3. **Encender o apagar los tiempos automáticos no hace saltar la envolvente.** Al poner la etapa lenta, empieza en el nivel actual; al quitarla, la envolvente sigue desde el mayor de los dos niveles y cae con la relajación fija. Medido con la banda comprimiendo y la envolvente cayendo: −90 dB de agudos espurios; si al quitarla se tomara solo la rápida, −42 dB (el test lo detecta). Encendidos, los parámetros de ataque y relajación no se usan, pero se conservan: al apagar vuelven a mandar.

4. **Un parámetro por banda, `bNN_autotime` ("Band N Auto Timing"), al final de la tabla, apagado por defecto y dentro de los presets.** Va en un bloque propio, como el canal (`core::autoTimingParameter()`). Con el interruptor apagado, el cálculo de la envolvente es exactamente el de antes: la etapa lenta es una rama que no se toma, y se comprobó que la salida de la dinámica no cambia ni una muestra (768 000 muestras, ocho bandas dinámicas de todos los tipos, con sidechain, canales y automatización). El estado sigue en la versión 3: un proyecto o preset anterior no lleva la línea, toma el valor por defecto y suena igual. A diferencia de `phase_mode` y `auto_gain`, **sí va en los presets**: es un ajuste de la banda como su ataque o su relajación, y un preset de de-esser o de control de graves debe poder llevarlo. Los presets anteriores, al no tenerlo, lo apagan, que es como sonaban.

5. **El umbral automático queda fuera.** No tiene un criterio de aceptación en el plan, y decidir el umbral a partir de la señal cambia qué hace la banda, no solo cómo reacciona: necesita su propio diseño (qué nivel se mide, durante cuánto, qué pasa en los silencios). Sigue pendiente en la sección 4.2.

## Consecuencias

- Contratos: 24 identificadores nuevos al final (`b01_autotime` … `b24_autotime`), en el fixture de parámetros. El estado sigue en la versión 3 y el contrato de la 0.1 no cambia.
- Medidas (tests en `tests/dsp/dynamics_test.cpp`): con un tono sostenido de 80 Hz, 14 dB por encima del umbral, el rizado de la ganancia es de 0,15 dB (lo mismo a 40 y a 200 Hz); con el ataque fijo mínimo y la relajación de 20 ms que van bien en agudos, 0,91 dB. Con ráfagas de 7 kHz, el 90 % de la reducción llega en 2,5 ms. Tras una ráfaga aislada de 50 ms, la banda se recupera al 90 % en 24 ms a 7 kHz, 23 ms a 1 kHz y 71 ms a 100 Hz. Tras 2 s de señal sostenida, 100 ms después de parar queda el 68 % de la reducción.
- Coste en el hilo de audio: una multiplicación, una suma y un máximo por muestra en cada banda con los tiempos automáticos. Con 24 bandas dinámicas en estéreo a 48 kHz (ruido, bloques de 64 y 512 muestras, GCC -O3) cuesta **menos** que los mismos tiempos sin la etapa lenta: del 5,9 al 5,1 % de un núcleo con señal constante y del 6,6 al 5,4 % con señal que sube y baja. La envolvente más lisa mueve la ganancia menos veces por encima de la resolución de 0,001 dB, y la banda se rediseña menos. Medido también con `processBlock` del procesador (Linux, x86-64, Release), las mismas 24 bandas frente a los tiempos por defecto (5 y 100 ms): del 12,6 al 9,5 % de un núcleo con bloques de 64 muestras y del 12,0 al 9,3 % con bloques de 512. El criterio del plan (no más de un 1 % extra) se cumple con margen: no hay coste extra.
- La reducción se retiene tras una señal larga: es lo que se busca (no bombea), pero una pausa breve tras una nota larga sale todavía algo reducida, durante la relajación lenta (hasta 1 s en graves).
- En fase lineal la dinámica va en el ecualizador "solo dinámica" ([ADR 0012](0012-modos-de-fase.md)), que es el mismo código: los tiempos automáticos funcionan igual en los cuatro modos.
