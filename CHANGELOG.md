# Cambios

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/). El proyecto sigue [versionado semántico](https://semver.org/lang/es/): cualquier cambio que afecte al estado guardado, a los parámetros o al sonido de una configuración existente se anuncia aquí de forma destacada.

## Sin publicar

### Añadido

- **Escala de notas.** En el botón de opciones, una sección nueva, *Notes*: *Piano keyboard* muestra bajo la curva un teclado de piano a lo largo del eje de frecuencias (de E0 a D#10, con la octava en cada do); al pasar el ratón por una tecla, la curva marca su frecuencia. Con el teclado visible, la etiqueta de cada nodo dice la nota más cercana y su desviación ("220 Hz · A3 +0 ct"), y la etiqueta del potenciómetro de frecuencia, esa nota. Con Alt (Option en macOS) al arrastrar un nodo, la frecuencia salta a la nota temperada más cercana, y en el campo de frecuencia se puede escribir una nota ("A3", "C#2", "Eb4"). La afinación es ajustable (*Tuning*: A4 de 400 a 480 Hz, en pasos de 0,1; 440 por defecto). Ver la [ADR 0017](docs/decisiones/0017-escala-de-notas.md).

  **Compatibilidad:** no hay parámetros nuevos ni cambia el sonido. El teclado y la afinación son preferencias de la interfaz, dos líneas nuevas del estado (`ui.notes`, `ui.a4`) que solo se escriben si no tienen su valor por defecto: el estado sigue en la versión 3, **los proyectos anteriores se cargan igual y se ven igual** (teclado oculto, A4 = 440 Hz), y una versión anterior de esceQ ignora esas líneas. Los presets no las guardan.

- **Tiempos automáticos en la dinámica.** Un interruptor *Auto* en el panel de dinámica de cada banda (parámetro automatizable `bNN_autotime`, "Band N Auto Timing") con el que la banda elige sola su ataque y su relajación según su frecuencia y la señal. El ataque es de dos periodos de la frecuencia de la banda (de 0,5 a 50 ms): en graves la ganancia ya no sigue la forma de onda (con un tono de 80 Hz, 0,15 dB de rizado) y en agudos atrapa las sibilantes (el 90 % de la reducción en 2,5 ms). La relajación depende de la señal: tras un golpe aislado se recupera deprisa (de 23 a 71 ms) y tras una nota larga se suelta despacio, sin bombear. Los tiempos siguen a la frecuencia aunque se automatice, y encenderlos o apagarlos no produce chasquidos. Encendidos, los potenciómetros de ataque y relajación dicen "Auto" y conservan su valor, que vuelve a mandar al apagar. Funciona igual en los cuatro modos de fase y no cuesta CPU extra (con las 24 bandas dinámicas, algo menos que con los tiempos fijos). Ver la [ADR 0016](docs/decisiones/0016-tiempos-automaticos-en-la-dinamica.md) y [docs/dsp/dinamica.md](docs/dsp/dinamica.md).

  **Compatibilidad:** son 24 parámetros nuevos, al final de la tabla, y el estado sigue en la versión 3. **Están apagados por defecto: los proyectos y presets anteriores los cargan apagados y suenan exactamente igual** (con el interruptor apagado, la dinámica da las mismas muestras, bit a bit). A diferencia de la compensación automática, los presets sí los guardan, como el ataque o la relajación.

- **Compensación automática de ganancia.** Un botón *Auto* en la barra superior, junto a la salida (parámetro automatizable `auto_gain`, "Auto Gain"), que mantiene la sonoridad igual al ecualizar, para comparar con el bypass sin que "más fuerte" parezca "mejor". La ganancia se calcula a partir de la curva (cuánto cambia la sonoridad de ruido rosa, ponderada como en ITU-R BS.1770) y no midiendo el audio, así que no bombea con la música. Solo compensa la parte estática de las bandas, no la dinámica; con bandas en L, R, M o S, la media de los dos canales. Se limita a ±12 dB, se suma a la ganancia de salida, entra con una rampa sin chasquidos y funciona igual en los cuatro modos de fase; con bypass no actúa. Encendido, el botón dice la compensación que aplica ("Auto -2.3 dB"). Al renderizar sin tiempo real, cada cambio se aplica en su bloque exacto. Ver la [ADR 0015](docs/decisiones/0015-compensacion-automatica-de-ganancia.md) y [docs/dsp/compensacion-de-ganancia.md](docs/dsp/compensacion-de-ganancia.md).

  **Compatibilidad:** es un parámetro nuevo, al final de la tabla, y el estado sigue en la versión 3. **Está apagada por defecto: los proyectos y presets anteriores la cargan apagada y suenan exactamente igual.** Elegir un preset no la enciende ni la apaga, y los presets no la guardan.

- **Precisión en agudos: filtros fieles al analógico cerca de Nyquist.** Un algoritmo de filtro nuevo, *Matched* (M. Vicanek), ajusta la magnitud de cada banda a la del filtro analógico. A 44,1 o 48 kHz, las campanas y los shelves en agudos ya no se estrechan ni caen antes de tiempo, y los cortes de agudos no se hunden de más: con bandas entre 1 y 16 kHz a 44,1 kHz, el peor error frente al analógico baja de 10,7 a 1,75 dB en campanas y de 87 a 3,8 dB en cortes de agudos. Se elige por instancia en el nuevo botón de opciones de la barra superior (*HF accuracy*: *Classic* o *Matched*), se guarda con el proyecto y se puede deshacer. Los presets no lo cambian. La curva se dibuja con el algoritmo elegido, y la dinámica y la fase lineal lo usan también. Ver la [ADR 0013](docs/decisiones/0013-correccion-en-agudos.md) y [docs/dsp/filtros.md](docs/dsp/filtros.md).

  **Compatibilidad: el estado guardado pasa a la versión 3** (una línea nueva, `filter_algorithm`; [ADR 0014](docs/decisiones/0014-estado-v3-algoritmo-de-filtro.md)). **Los proyectos guardados con versiones anteriores, incluida la 0.1, abren en *Classic* y suenan exactamente igual que antes;** solo cambian a *Matched* si se elige en el menú. Las instancias nuevas empiezan en *Matched*. Una versión anterior de esceQ abre un proyecto de la versión 3 con todos sus valores, pero suena siempre en *Classic*, el único algoritmo que tiene.

- **Cuatro modos de fase.** Desplegable en la barra superior y parámetro automatizable `phase_mode`:
  - *IIR*: los filtros IIR de siempre, latencia cero. Suena exactamente igual que la 0.1.
  - *Linear Low*, *Linear Medium* y *Linear High*: fase lineal con FIR simétricos de 2048, 8192 y 32768 coeficientes (a 48 kHz), con 26,7, 96 y 362,7 ms de latencia que se comunican al host. Más coeficientes, más resolución en graves.

  La curva es la misma en todos los modos, y la dinámica, el sidechain y el canal por banda (L, R, M, S) funcionan también en fase lineal. Cambiar de modo silencia la salida un momento mientras el host recalcula la compensación de latencia. Al renderizar sin tiempo real, cada cambio de ajustes se aplica en su bloque exacto. Los proyectos anteriores abren en *IIR*. El modo de fase es un parámetro más, sin cambio de formato en el estado, y elegir un preset no lo cambia. Ver [docs/dsp/fase-lineal.md](docs/dsp/fase-lineal.md) y la [ADR 0012](docs/decisiones/0012-modos-de-fase.md).

- **Comparar con otras pistas y detectar colisiones.** El botón *Compare* de la barra superior superpone el espectro de otras instancias de esceQ del proyecto, con el nombre y el color de su pista, y marca en rojo dónde tapan a la pista actual, con etiquetas como "Bass · 113 Hz". Facilita la ecualización por ranuras: abrir hueco en una pista justo donde suena otra, y ver el efecto al momento. Funciona con las instancias que el host carga en el mismo proceso y del mismo formato (VST3 con VST3, CLAP con CLAP). No cuesta nada mientras no se usa. No cambia el estado guardado, los parámetros ni el sonido.

### Cambiado

- Ctrl/Cmd + izquierda/derecha en la curva avanza la frecuencia de la banda seleccionada un semitono, hasta la nota siguiente o la anterior (antes, un 1 % del recorrido, unos 1,2 semitonos). Con Mayús sigue el paso fino de siempre.
- El selector del tamaño de la ventana (S, M, L) sale de la barra superior y pasa al menú de opciones (*Window size*), para dejar sitio al botón *Auto*. Los tamaños son los mismos, y la ventana se sigue pudiendo redimensionar desde la esquina.
- El ancho mínimo de la ventana pasa de 960 a 1120 px, y el tamaño S, de 960 × 580 a 1120 × 580, para que quepan el selector del modo de fase y el botón de opciones. Una ventana guardada más estrecha abre con el ancho mínimo.

## [0.1.0] - 2026-11-12

Primera versión pública.

### Qué es

esceQ es un ecualizador paramétrico y dinámico, libre (AGPLv3), pensado para funcionar bien en FL Studio y en cualquier host VST3, CLAP o AU.

### Qué trae

- **Ecualizador de 24 bandas**, cada una con nueve tipos de filtro: campana, shelf de graves y de agudos, corte de graves y de agudos (de 6 a 96 dB/oct), notch, paso banda, paso todo y tilt. Cambios de parámetros sin chasquidos y latencia cero.
- **Dinámica por banda** en campanas, shelves y tilt: umbral, rango (negativo reduce, positivo realza), ataque, relajación y rodilla. Sirve de de-esser y para controlar resonancias. El detector escucha la entrada del plugin o un **sidechain externo**, a elegir en cada banda.
- **Canal por banda:** estéreo, solo izquierdo, solo derecho, Mid o Side. La dinámica de la banda escucha ese mismo canal.
- **Curva interactiva:** doble clic para añadir una banda, arrastrar para moverla, rueda para el Q, clic derecho para el tipo, el canal y la dinámica. Etiqueta flotante con frecuencia, ganancia y Q; la curva muestra en tiempo real la ganancia dinámica.
- **Analizador de espectro** antes y después del ecualizador, y **medidor de nivel de salida** con pico retenido y aviso de recorte.
- **Controles propios:** potenciómetros con valor editable con un clic, ajuste fino con Mayús, rueda, y doble clic o Ctrl/Cmd + clic para volver al valor por defecto. Manejo con teclado y nombres accesibles.
- **Deshacer y rehacer** dentro del plugin: un arrastre entero es una sola acción.
- **Presets:** cinco de fábrica (De-esser, Resonance Control, Low-End Cleanup, Vocal y Gentle Master) y los tuyos, en una carpeta del sistema.
- **Ventana redimensionable**, con tres tamaños predefinidos (S, M, L). El tamaño, el modo del analizador y el rango de la curva se guardan con el proyecto.
- 338 parámetros automatizables, con valores legibles en el host ("1.20 kHz", "+3.0 dB").

### Formatos y plataformas

- **Windows (x64):** VST3 y CLAP, con instalador.
- **macOS (Apple Silicon e Intel):** VST3, AU y CLAP. Los binarios no están firmados con un certificado de Apple: el archivo `INSTALL.txt` del paquete explica cómo permitir su carga.
- **Linux (x64):** VST3 y CLAP.

### Compatibilidad a partir de esta versión

Desde la 0.1.0 quedan congelados, para que los proyectos guardados sigan abriendo y sonando igual en las versiones siguientes:

- los identificadores, rangos y posiciones de los 338 parámetros;
- el formato del estado guardado (versión 2; también se leen los estados de la versión 1, de las compilaciones de desarrollo);
- el nombre y el sonido de los presets de fábrica;
- la identidad del plugin ante el host (VST3, AU y CLAP).

### Limitaciones conocidas

- Fase mínima únicamente: sin fase lineal ni corrección de la respuesta cerca de Nyquist (previstas para la 0.5).
- La dinámica no tiene ratio ni anticipación (lookahead).
- El analizador muestra la media de los canales, y la curva total suma todas las bandas sin distinguir su canal.
- El enrutado del sidechain en FL Studio está descrito en el README, pero aún no se ha verificado paso a paso en FL Studio.
