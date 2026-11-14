# 0011. Espectros compartidos entre instancias y detección de colisiones

**Estado:** Aceptada (noviembre de 2026)

## Contexto

La fase 6 incluye la "detección de colisiones entre instancias": desde una instancia de esceQ se superpone el espectro de otras instancias de la misma sesión y se ve en tiempo real dónde se pisan las frecuencias entre pistas. Es la base de la ecualización "por ranuras": abrir hueco en una pista justo donde suena otra.

Había que decidir:

1. Cómo se comunican las instancias.
2. Quién calcula el espectro de cada instancia, y cuándo.
3. Qué señal se comparte.
4. Qué cuenta como colisión.
5. Qué se guarda con el proyecto.
6. Dónde vive cada pieza, respetando las dependencias de `docs/arquitectura.md`.

## Decisión

1. **Dentro del proceso, con un registro compartido** (`core::SpectrumSession::shared()`): las instancias que el host carga en el mismo proceso y desde el mismo binario se ven entre sí. Es lo que hacen los ecualizadores comerciales con esta función, y cubre el caso normal de FL Studio, Reaper, Ableton Live, Cubase o Studio One, que cargan los plugins en su proceso. Alternativas descartadas por ahora:
   - *Memoria compartida entre procesos:* funcionaría también con plugins "puenteados" o en procesos separados (el modo aislado de Bitwig, la opción de proceso separado de FL Studio), pero añade nombres de segmentos, limpieza de segmentos huérfanos tras un cierre inesperado, permisos y diferencias entre sistemas. Se puede añadir detrás de la misma interfaz (`snapshot`, `Member::feed`) sin tocar la interfaz gráfica.
   - *Red local (OSC, UDP):* demasiada infraestructura y superficie de ataque para comunicar plugins del mismo ordenador.

   Consecuencia aceptada: las instancias VST3 y las CLAP no se ven entre sí (son binarios distintos), ni las que el host aísla en otro proceso.

2. **Un hilo trabajador por sesión, no por instancia ni en el hilo de audio.** El hilo de audio solo mezcla a mono y escribe en una cola sin bloqueo (`dsp::SpscFifo`) de su instancia. Un único hilo de la sesión lee todas las colas 20 veces por segundo, calcula una FFT de 8192 puntos por instancia en una rejilla común de 192 frecuencias logarítmicas (de 20 Hz a 20 kHz, la misma para todas) y la promedia en el tiempo. Así el espectro de una instancia está disponible aunque su ventana esté cerrada, el hilo de mensajes no carga con FFT ajenas, y con 30 instancias el coste es de unas 30 FFT cada 50 ms en un hilo de fondo.

   **Nada se calcula si nadie mira.** La sesión cuenta los `Viewer` vivos (una vista con alguna instancia seleccionada). Con cero, `Member::feed` vuelve tras leer un atómico y el trabajador duerme en una variable de condición. Al empezar a mirar se descarta lo que quedara en las colas.

   **Ciclo de vida.** La sesión vive en un `std::shared_ptr` que retienen las instancias (`Member`) y las vistas (`Viewer`); `shared()` guarda solo un `weak_ptr`. La sesión, y con ella su hilo, se destruye con la última instancia del proceso, en el hilo que la destruye. Nunca en la descarga del binario: unir un hilo en un destructor estático durante la descarga de una DLL puede bloquear el proceso en Windows.

3. **La salida de la pista**: lo que sale del plugin, tras el ecualizador y la ganancia de salida (con bypass, la señal sin procesar). Es lo que llega al mezclador, y al ecualizar una pista su espectro en la sesión cambia: el resultado de abrir una ranura se ve enseguida en las demás instancias.

4. **Colisión = enmascaramiento sostenido de la propia pista por la otra** (`dsp/spectrum_collision.h`). En cada punto de la rejilla, con los espectros promediados en el tiempo (constante de 0,4 s, en potencia) y suavizados a 1/3 de octava (aproximadamente una banda crítica):

   `severidad = presencia(propia) × presencia(otra) × enmascaramiento(otra − propia)`

   - *Presencia:* la señal supera un suelo absoluto (−66 dB en la escala del analizador) **y** no está a más de 30 dB de su propio máximo. Así la cola lejana de una pista no compite con nada.
   - *Enmascaramiento:* 0 si la otra está 15 dB o más por debajo de la propia, 1 si está igual o por encima, con transición suave.

   Es la misma idea que las medidas de enmascaramiento de la literatura (la relación entre enmascarador y señal por banda crítica, por ejemplo en el trabajo de Hafezi y Reiss sobre ecualización multipista automática), simplificada para dibujar en tiempo real. Es direccional: cada instancia muestra **lo que le tapa a ella**; la otra instancia, al revés. Detalles y límites en [docs/dsp/colisiones.md](../dsp/colisiones.md).

5. **No se guarda nada.** La selección de instancias (`core::CompareSelection`) vive en el procesador mientras el plugin está abierto (sobrevive a cerrar y abrir la ventana), pero los identificadores de instancia cambian en cada sesión del host y no hay forma fiable de reconocer "la misma pista" al reabrir un proyecto. El formato de estado y los parámetros no cambian; ningún contrato público se toca.

6. **Dónde vive cada pieza:**

   | Pieza | Módulo | Por qué ahí |
   |---|---|---|
   | Severidad, tramos, suavizado y promedio | `dsp/spectrum_collision.h` | Algoritmo puro, sin hilos ni estado del plugin (MIT) |
   | Registro, trabajador y selección | `core/spectrum_session.h` | C++ puro con hilos estándar: se prueba sin JUCE, también con ThreadSanitizer |
   | Menú, colisiones de varias instancias, etiquetas | `ui/logic/session_overlay.h` | Lógica de la interfaz que se puede probar sin ventana |
   | Alimentar la sesión y nombre y color de la pista | `plugin/` (`processBlock`, `updateTrackProperties`) | Lo único que depende del host |
   | Botón, menú y dibujo | `ui/src/` | Solo pinta y traduce gestos |

## Consecuencias

- Coste en el hilo de audio sin usar la función: una lectura atómica por bloque. Usándola: una mezcla a mono y una copia a la cola, como el analizador propio.
- El nombre de cada instancia es el de su pista si el host lo dice (`updateTrackProperties`: VST3 y AU lo admiten, y FL Studio lo envía); si no, "esceQ 1", "esceQ 2"... con el menor número libre.
- El espectro de una instancia que deja de recibir audio se atenúa y se apaga en unos segundos, en lugar de quedarse congelado; el menú la marca como "silent".
- Una nueva vía de comunicación (entre procesos) se añadiría como otra implementación de la sesión; la interfaz y el algoritmo no cambiarían.
- Los umbrales de la detección son ajustes con valores por defecto (`dsp::CollisionSettings`), no parámetros del host. Exponerlos al usuario se decidirá con uso real.
