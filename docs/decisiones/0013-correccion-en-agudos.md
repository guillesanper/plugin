# 0013. Corrección en agudos: algoritmo de filtro Matched, detrás de una versión guardada

**Estado:** Aceptada (noviembre de 2026)

## Contexto

Los filtros de esceQ son la transformada bilineal de un prototipo analógico (SVF trapezoidal, [filtros.md](../dsp/filtros.md)). La bilineal comprime el eje de frecuencias hacia Nyquist: a 44,1 y 48 kHz, una campana a 15 kHz queda más estrecha que la analógica, un shelf de agudos cae antes de tiempo y un corte de agudos se hunde hacia el cero que la bilineal pone en Nyquist. La sección 4.1 del [plan](../../PLAN.md) dejaba la corrección para la fase 5, con dos candidatas: coeficientes ajustados a la respuesta analógica (Vicanek, Orfanidis) o sobremuestreo ×2.

Corregirlo cambia el sonido, y el contrato 3 de la sección 2.1 del plan exige que una configuración guardada suene igual tras actualizar. Había que decidir:

1. Cómo se calculan los coeficientes nuevos.
2. Cómo conviven con los de la 0.1 y qué garantiza que estos no cambian.
3. Qué pasa al cambiar de uno a otro con el audio en marcha.
4. Dónde se guarda cuál usa cada instancia, y quién lo elige.

## Decisión

1. **Un algoritmo nuevo, `dsp::FilterAlgorithm::Matched`, que iguala la magnitud del prototipo analógico (M. Vicanek, *Matched Second Order Digital Filters*).** Los polos van por invariancia al impulso, `z = e^{s·T}`, sin deformación de frecuencias. El numerador se elige para que la magnitud digital sea la analógica en continua, en Nyquist y en un tercer punto ωm: la frecuencia de la sección, sin pasar de 0,9π. El resultado se convierte de vuelta a un SVF, que es la misma topología de siempre. Se modula igual de bien, y `response()` sigue dando la respuesta exacta, así que la curva sigue siendo lo que suena. El método de Vicanek, aplicado tal cual, no tiene solución en algunos casos, y al medir se encontraron errores de hasta 60 dB. Se completa así (derivación y medidas en [filtros.md](../dsp/filtros.md)):
   - **Atenuaciones como inversa del realce.** En campanas, shelves y tilt, la atenuación de g dB es la inversa exacta del realce de g dB: `H(−g) = 1/H(g)`. Una atenuación estrecha cerca de Nyquist tiene los polos casi en el círculo unidad y a veces no existe ningún numerador real que iguale los tres puntos; su inversa sí. Se conserva además una propiedad de Classic: un realce seguido de la atenuación simétrica se cancela exactamente. También la de la fase lineal con dinámica ([ADR 0012](0012-modos-de-fase.md)), que necesita `H(−g) = 1/H(g)` exacto.
   - **El high shelf sale del low shelf**, `HS(g) = 10^(g/20)/LS(g)`, porque sus polos pueden quedar por encima de Nyquist, donde la invariancia al impulso los dobla.
   - **Shelves con Q > 0,7071 en dos secciones.** Un solo numerador no reproduce a la vez el pico y la caída resonantes. Se factoriza `LS = (E/D)·(N/E)` con unos ceros auxiliares E de Q 0,7071: N/E es una campana relativa, que vale exactamente 1 con Q = 0,7071, de modo que no hay salto al cruzar ese Q. Si los ceros se acercan a Nyquist, N/E se funde con la unidad entre 0,9π y 0,98π. E/D se ajusta a lo que falta para el prototipo entero.
   - **Ceros en el eje imaginario al círculo unidad**, en la misma frecuencia: el notch sigue anulando exactamente su frecuencia, y los cortes de graves conservan sus ceros en continua (12 dB/oct por sección).
   - **Primer orden** (tilt, cortes impares): igual en continua, en Nyquist y en el pivote o la frecuencia de corte, con el polo como incógnita (Vicanek, *Matched One-Pole Digital Shelving Filters*).

   Se descartaron:
   - *Sobremuestreo ×2:* reduce la deformación sin quitarla, añade latencia o fase de los filtros de diezmado, y duplica el coste de cada banda.
   - *Orfanidis (ganancia en Nyquist prescrita):* resuelve la campana, pero no cubre shelves, cortes ni tilt, y habría hecho falta otro método para ellos.
   - *Transformada z adaptada (polos y ceros por invariancia):* sin control de la magnitud cerca de Nyquist, que es justo el problema.
   - *Vicanek sin los complementos de arriba:* hasta 60 dB de error en atenuaciones estrechas en agudos y 22 dB en shelves resonantes.
2. **Classic, el algoritmo de la 0.1, intacto y comprobado bit a bit.** `FilterParams` tiene un campo `algorithm`, con `Classic` por defecto. El diseño se escribe una sola vez, como una tabla de prototipos analógicos, y cada algoritmo aporta su forma de digitalizarlos. Classic hace exactamente las mismas operaciones en el mismo orden que la 0.1. Un test lo compara con `==`, coeficiente a coeficiente y en 211.680 configuraciones, contra una copia literal del código de la 0.1 que no se modifica. Matched tiene su propio archivo de referencia, `tests/fixtures/dsp/respuesta_matched.txt`, con 1080 respuestas que ya no podrán cambiar.
3. **Cambiar de algoritmo es un cambio estructural de la banda:** hace el mismo fundido de 10 ms que cambiar de tipo o de canal. Los coeficientes de los dos algoritmos no se pueden interpolar con sentido, y un salto directo se oye en un realce fuerte en agudos.
4. **Una versión por instancia, guardada en el estado, que los presets no cambian; Matched en las instancias nuevas.** Decidido con el autor del proyecto, siguiendo el precedente del modo de fase ([ADR 0012](0012-modos-de-fase.md)):
   - Un estado guardado sin versión de algoritmo (todos los de la 0.1 y los de antes de este cambio) carga en Classic y suena bit a bit igual. Una instancia nueva arranca en Matched.
   - Cargar un preset, de fábrica o de usuario, no cambia el algoritmo de la instancia, como no cambia el modo de fase. Un preset de la 0.1 suena con Classic en un proyecto antiguo y con Matched en uno nuevo. El estado del proyecto, que es lo que tiene que sonar igual, no cambia.
   - Un menú de la interfaz permite cambiarlo, y el cambio se puede deshacer. Es la única forma de pasar a Matched un proyecto antiguo.

   Lleva el formato de estado a la versión 3, con su migración y sus fixtures. Se implementa en una entrega aparte, después del DSP.

## Consecuencias

- Peor error frente al analógico, de 20 Hz a 20 kHz, en el rango de uso habitual (de 1 a 16 kHz, Q de 0,3 a 8, ±18 dB). Por ejemplo, la campana pasa de 10,7 a 1,75 dB a 44,1 kHz y de 1,66 a 0,36 dB a 96 kHz, y los cortes de agudos, de 87 a 3,8 dB a 44,1 kHz. Tabla completa en [filtros.md](../dsp/filtros.md). Lejos de Nyquist, por debajo de fs/24, los dos algoritmos coinciden en 0,05 dB allí donde la banda suena.
- Lo que no se arregla: una campana o un shelf muy anchos centrados cerca de Nyquist siguen sin poder copiar la parte analógica que queda por encima de Nyquist. Un filtro digital de segundo orden no tiene a dónde llevarla. Ahí Matched sigue equivocándose en unos pocos dB a 44,1 kHz, aunque mucho menos que Classic.
- Coste: diseñar una banda pasa de unos 30 ns a entre 130 y 370 ns. Con parámetros fijos no se nota (24 campanas estéreo a 48 kHz: 0,8 % de un núcleo con los dos). Los shelves con Q > 0,7071 usan dos secciones: 24 de ellos, del 0,85 % al 1,8 %. Con las 24 bandas dinámicas, que rediseñan hasta una vez por muestra, del 4,3–4,7 % al 4,8–5,3 %.
- La fase lineal no necesita nada propio: su FIR se diseña con la magnitud de las bandas, que ya es la de Matched, y la dinámica sigue siendo exacta porque `H(−g) = 1/H(g)` se cumple con los dos algoritmos.
- Un algoritmo futuro se añade igual: un valor al final del `enum`, su digitalización, su archivo de referencia y su opción en el menú, sin tocar Classic ni Matched.
