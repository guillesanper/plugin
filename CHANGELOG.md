# Cambios

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/). El proyecto sigue [versionado semántico](https://semver.org/lang/es/): cualquier cambio que afecte al estado guardado, a los parámetros o al sonido de una configuración existente se anuncia aquí de forma destacada.

## Sin publicar

## [0.5.0] - 2026-11-24

Primera versión pública. La 0.1.0 se cerró pero no llegó a publicarse: sus paquetes de macOS y Linux no habrían cargado en muchos sistemas (ver «Cambiado»). Esta versión trae todo lo de la 0.1.0 y lo que se detalla debajo.

### Qué es

esceQ es un ecualizador paramétrico y dinámico, libre (AGPLv3), pensado para funcionar bien en FL Studio y en cualquier host VST3, CLAP o AU.

### Qué trae

- **Ecualizador de 24 bandas**, cada una con nueve tipos de filtro: campana, shelf de graves y de agudos, corte de graves y de agudos (de 6 a 96 dB/oct), notch, paso banda, paso todo y tilt, en estéreo, solo izquierdo, solo derecho, Mid o Side. Cambios de parámetros sin chasquidos.
- **Cuatro modos de fase:** *IIR*, sin latencia, y fase lineal en tres resoluciones (*Linear Low*, *Medium* y *High*, de 26,7 a 362,7 ms de latencia a 48 kHz, que el host compensa).
- **Filtros fieles al analógico también en agudos** (algoritmo *Matched*, por defecto en cada instancia nueva): a 44,1 o 48 kHz, las campanas y los shelves cerca de Nyquist ya no se estrechan ni caen antes de tiempo.
- **Dinámica por banda** en campanas, shelves y tilt: umbral, rango (negativo reduce, positivo realza), ataque, relajación y rodilla, con **sidechain externo** a elegir en cada banda, **tiempos automáticos** según la frecuencia y la señal, y **dinámica espectral**, que dentro de la banda solo reduce (o realza) las frecuencias que pasan del umbral: de-esser y control de resonancias que se mueven.
- **Saturación** global tras el ecualizador, *Soft* o *Warm*, con *Drive* y *Mix*, sin subir el nivel y con sobremuestreo contra el aliasing, en el cajón *Output* junto a la ganancia de salida.
- **Compensación automática de ganancia**, que mantiene la sonoridad al ecualizar para comparar con el bypass sin engañarse.
- **Curva interactiva:** doble clic para añadir una banda, arrastrar para moverla, rueda para el Q, clic derecho para el tipo, el canal y la dinámica; arrastrar desde un pico del espectro crea una campana en él. La curva muestra en tiempo real la ganancia dinámica.
- **Analizador de espectro** antes y después del ecualizador, ajustable (resolución, caída, inclinación, picos retenidos, congelar) y con una vista ampliada; **medidor de nivel de salida** con pico retenido y aviso de recorte.
- **Comparar con otras pistas** (el espectro de otras instancias de esceQ del proyecto, con las colisiones marcadas) e **igualar a una referencia** (otra instancia o el sidechain): esceQ propone y crea las bandas que faltan.
- **Escala de notas:** teclado de piano bajo la curva, la nota de cada banda, ajuste a la nota más cercana y frecuencias escritas como notas, con afinación ajustable.
- **Controles propios**, manejables con teclado y con nombres accesibles; **deshacer y rehacer** dentro del plugin.
- **Presets:** cinco de fábrica (De-esser, Resonance Control, Low-End Cleanup, Vocal y Gentle Master) y los tuyos, en una carpeta del sistema.
- **Ventana redimensionable**, con tres tamaños predefinidos (S, M, L) en el menú de opciones.
- 391 parámetros automatizables, con valores legibles en el host ("1.20 kHz", "+3.0 dB").

### Formatos y plataformas

- **Windows (x64):** VST3 y CLAP, con instalador.
- **macOS 11 o posterior (Apple Silicon e Intel):** VST3, AU y CLAP, binarios universales con firma ad hoc. No están firmados con un certificado de Apple ni notarizados: el archivo `INSTALL.txt` del paquete explica cómo permitir su carga.
- **Linux (x64):** VST3 y CLAP, para Ubuntu 22.04, Debian 12, Fedora 36 o posteriores (glibc 2.35).

### Compatibilidad con la 0.1.0

Los proyectos y presets guardados con la 0.1.0 (o con las compilaciones de desarrollo de entonces) abren con todos sus valores y **suenan exactamente igual**: abren en *IIR* y con el algoritmo *Classic*, y todo lo nuevo empieza apagado (los tiempos automáticos, la dinámica espectral, la saturación y la compensación automática). El estado guardado pasa de la versión 2 a la 3 (una línea más, `filter_algorithm`, y las preferencias nuevas de la interfaz), y la tabla de parámetros, de 338 a 391, todos añadidos al final: los 338 de la 0.1.0 no cambian. Una versión anterior abre un proyecto de la 0.5.0 con los valores que conoce, pero sin lo nuevo y siempre en *Classic*.

Desde esta versión quedan congelados, como los de la 0.1.0:

- los identificadores, rangos y posiciones de los 391 parámetros;
- el formato del estado guardado (versión 3; se siguen leyendo las versiones 1 y 2);
- el nombre y el sonido de los presets de fábrica;
- el sonido de un proyecto guardado;
- la identidad del plugin ante el host (VST3, AU y CLAP).

### Limitaciones conocidas

- La dinámica no tiene ratio ni anticipación (lookahead).
- El analizador muestra la media de los canales, y la curva total suma todas las bandas sin distinguir su canal.
- Comparar con otras pistas, e igualar a otra instancia, ven solo las instancias que el host carga en el mismo proceso y del mismo formato (VST3 con VST3, CLAP con CLAP).
- Sin surround: mono y estéreo.
- El enrutado del sidechain en FL Studio está descrito en el README, pero aún no se ha verificado paso a paso en FL Studio. Los paquetes de macOS y Linux se comprueban en el CI (cargan y pasan pluginval y clap-validator), pero aún no se han probado con hosts reales en esos sistemas.

### Añadido

- **Saturación.** Una etapa global de saturación tras el ecualizador y la dinámica, antes de la ganancia de salida, para dar color armónico, en el nuevo cajón *Output* (ver «Cambiado»): *Off*, *Soft* (simétrica, solo armónicos impares) o *Warm* (asimétrica, añade los pares), con *Drive* (0 a 24 dB, 6 por defecto) y *Mix* (0 a 100 %, 100 por defecto). El drive no sube el nivel: se compensa para que un seno a −18 dBFS salga igual (±0,0002 dB), así que se compara el color. Contra el aliasing, sobremuestreo hasta unos 384 kHz (×8 a 44,1 y 48 kHz, ×4 hasta 100 kHz, ×2 por encima): con un seno de 10 kHz y 12 dB de drive a 48 kHz, el aliasing queda más de 100 dB por debajo de la fundamental a cualquier nivel hasta 0 dBFS. La mezcla se hace dentro, sin filtro de peine (al 0 %, a menos de 0,001 dB de la entrada hasta 18 kHz), y un bloqueador quita la continua de *Warm*. Cambiar el modo, el drive o la mezcla, también automatizándolos, va con rampas de 20 ms, sin chasquidos. Con *Off*, *Drive* y *Mix* se atenúan: se ajustan, pero no suenan. Cuesta del 0,7 al 1,3 % de un núcleo a 48 kHz en estéreo. Ver la [ADR 0026](docs/decisiones/0026-saturacion.md) y [docs/dsp/saturacion.md](docs/dsp/saturacion.md).

  **Latencia:** en IIR, ninguna (encenderla o apagarla es un fundido de 20 ms). En fase lineal, mientras no esté en *Off*, **47 muestras más** (0,98 ms a 48 kHz, 1,07 a 44,1; 43 muestras a 96 kHz y 35 a 192), que el host compensa; encenderla o apagarla, también al automatizarla o al cargar un preset que la cambie, deja un breve silencio, como el cambio de modo de fase. La ayuda del selector lo dice, con la latencia de la frecuencia de muestreo actual.

  **Compatibilidad:** son 3 parámetros nuevos al final de la tabla (`sat_mode` "Saturation", `sat_drive` "Saturation Drive" y `sat_mix` "Saturation Mix"; 391 en total), y el estado sigue en la versión 3. **Empieza en *Off*: los proyectos y presets anteriores la cargan apagada y suenan exactamente igual, con la misma latencia** (bit a bit, en los cuatro modos de fase). Los presets la guardan.

- **Dinámica espectral.** Un interruptor *Spectral* en el panel de dinámica de cada banda, junto a *Auto* (parámetro automatizable `bNN_spectral`, "Band N Spectral"). Con él, dentro de la banda solo se reducen (o realzan) las frecuencias que pasan del umbral, y no la banda entera: controla resonancias que se mueven, como los armónicos de una voz o las notas que retumban en una guitarra acústica, sin apagar el resto de esa zona. Con un tono por encima del umbral y otro débil a 1/3 de octava, el fuerte baja lo que marca el rango y el débil no cambia (con la banda dinámica normal bajaría también). Usa los mismos controles de la banda (umbral, rango, rodilla, ataque, relajación, tiempos automáticos, sidechain y canal), en campanas, shelves y tilt; necesita la dinámica encendida. El medidor de la banda dice la reducción de la frecuencia que más baja, y la curva sombrea bajo la banda la forma de la reducción, frecuencia a frecuencia; la curva total baja solo donde actúa. Funciona en los cuatro modos de fase. Con 24 bandas espectrales cuesta del 3,4 al 3,9 % de un núcleo a 48 kHz. Ver la [ADR 0024](docs/decisiones/0024-dinamica-espectral.md), la [ADR 0025](docs/decisiones/0025-dinamica-espectral-en-el-procesador.md) y [docs/dsp/dinamica-espectral.md](docs/dsp/dinamica-espectral.md).

  **Latencia:** mientras alguna banda tenga el interruptor encendido, el plugin añade **53,3 ms** (2560 muestras a 48 kHz; 58 ms a 44,1; los mismos 53,3 ms a 96 y 192 kHz), sumados a los del modo de fase, aunque esa banda no actúe (por ejemplo, con la dinámica apagada: el interruptor se sigue viendo encendido). El host la compensa. Encender el de la primera banda, o apagar el de la última, cambia la latencia con un breve silencio, como al cambiar de modo de fase; también al automatizarlo o al cargar un preset que lo cambie. La ayuda del interruptor lo dice, con la latencia de la frecuencia de muestreo actual.

  **Compatibilidad:** son 24 parámetros nuevos, al final de la tabla (388 en total), y el estado sigue en la versión 3. **Están apagados por defecto: los proyectos y presets anteriores los cargan apagados y suenan exactamente igual, con la misma latencia** (bit a bit, en los cuatro modos). Los presets sí los guardan, como los tiempos automáticos. La columna izquierda de la tarjeta de dinámica es más ancha, para que quepan *Auto* y *Spectral* en una fila.

- **Igualar a una referencia.** En el menú del botón de comparar, *Match to reference…* abre el panel *Match*, una tarjeta sobre la esquina superior derecha de la curva (también en la vista ampliada). Se elige la referencia: otra instancia de esceQ del proyecto (su salida, con el nombre de su pista) o la entrada de sidechain. *Learn* mide el espectro medio de esta pista, antes del ecualizador, y el de la referencia, solo mientras suenan las dos; un indicador dice los segundos útiles ("3.2 s / 10 s"), y con el transporte parado no avanza. Con 10 s, la curva dibuja los dos espectros (la referencia desplazada a su mismo nivel), la curva objetivo (discontinua) y la propuesta, con los nodos de las bandas nuevas en fantasma. Ajustes: *Amount* (0 a 100 %, 50 por defecto), *Smoothing* (1/6, 1/3 o 1 octava), *Max bands* (de 1 a las bandas libres; 8) y *Range* (30 Hz a 16 kHz por defecto; se escribe como la frecuencia, también una nota). La propuesta se recalcula sola al cambiar un ajuste o una banda. *Apply* crea campanas y shelves normales, editables, en las bandas libres, en una sola acción de deshacer, y selecciona la primera; desactivado, su ayuda dice por qué ("Learn 10 s first", "No free bands", "Fitting…", "Nothing to match"). Cerrar el panel o la ventana para el aprendizaje y conserva lo aprendido mientras el plugin está abierto. Ver la [ADR 0023](docs/decisiones/0023-igualar-a-una-referencia.md) y [docs/dsp/igualacion.md](docs/dsp/igualacion.md).

  **Compatibilidad:** no hay parámetros ni preferencias nuevas, el estado sigue en la versión 3 y no cambia el sonido: *Apply* solo cambia parámetros de siempre. Lo aprendido y los ajustes del panel no se guardan con el proyecto. **Los proyectos anteriores se cargan igual, suenan igual y se ven igual.**

- **Bandas desde el espectro.** Con el analizador encendido y el ratón en un hueco de la curva, se marca el pico del espectro más cercano (a menos de 1/6 de octava), con su frecuencia, su nota y su nivel. Arrastrar desde él crea una campana en su frecuencia, con el Q de su ancho a −3 dB (de 0,5 a 30) y la ganancia que marque el arrastre vertical; al soltar queda seleccionada. Con Alt (Option en macOS) al soltar, la banda queda dinámica, con el umbral 3 dB por debajo del pico y la ganancia arrastrada como rango: el atajo para controlar resonancias. Un clic sin arrastre no crea nada (sigue deseleccionando), el doble clic sigue creando una banda en el cursor, y todo el gesto es una sola acción de deshacer. Si las 24 bandas están en uso no se marca ningún pico y el cursor dice "No free bands". Se usa el espectro post, o el pre en pre + post, promediado durante 1 s para no marcar el azar del ruido; funciona también con el analizador congelado. Ver la [ADR 0019](docs/decisiones/0019-bandas-desde-el-espectro.md) y [docs/dsp/picos.md](docs/dsp/picos.md).

  **Compatibilidad:** no hay parámetros ni preferencias nuevas, el estado sigue en la versión 3 y no cambia el sonido: las bandas que se crean así son bandas normales. **Los proyectos anteriores se cargan igual, suenan igual y se ven igual.**

- **Analizador ampliado.** Un botón nuevo en la barra superior, a la derecha del selector del analizador (o la tecla F con el foco en la curva), oculta el panel de la banda y el medidor: la curva y el analizador ocupan toda la ventana menos la barra superior. Las bandas se siguen editando en la curva, y su etiqueta flotante dice también el canal y la dinámica (umbral, rango, tiempos o "Auto times", y "Sidechain"). Esc o el mismo botón vuelven a la vista normal, idéntica a la de antes. En el botón de opciones, una sección nueva, *Analyzer*, con la resolución (FFT de 4096, 8192 o 16384 puntos), la caída (*Slow* 1 s, *Medium* 0,35 s, *Fast* 0,12 s), la inclinación (0, 3, 4,5 o 6 dB/oct), *Peak hold* (una línea con el máximo, que se borra con un clic en la escala de dB) y *Freeze* (detiene el analizador). Ver la [ADR 0018](docs/decisiones/0018-analizador-ampliado.md).

  **Compatibilidad:** no hay parámetros nuevos ni cambia el sonido. Los ajustes del analizador son preferencias de la interfaz, cuatro líneas nuevas del estado (`ui.analyzer_fft`, `ui.analyzer_fall`, `ui.analyzer_tilt`, `ui.analyzer_peaks`) que solo se escriben si no tienen su valor por defecto, que es el analizador de siempre: el estado sigue en la versión 3, **los proyectos anteriores se cargan igual y se ven igual**, y una versión anterior de esceQ ignora esas líneas. Los presets no las guardan. Ni la vista ampliada ni la congelación se guardan.

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

- **La ganancia de salida y *Auto* pasan de la barra superior al cajón *Output*,** entre la curva y el medidor de salida, junto con la saturación: el orden de la señal (saturación, salida, compensación y el medidor). La salida es ahora un potenciómetro (*Gain*) en vez de una barra; se maneja igual. El cajón se pliega con su flecha en una pestaña de 16 px, que marca con un punto si algo de dentro cambia el sonido (y su ayuda dice qué), para ganar ancho de curva; abierto, la curva es 168 px más estrecha. Se recuerda con el proyecto y se oculta en la vista ampliada. La barra superior gana sitio: el navegador de presets tiene su ancho máximo (320 px) en todos los tamaños. Ver la [ADR 0027](docs/decisiones/0027-cajon-de-la-etapa-de-salida.md).

  **Compatibilidad:** los parámetros no cambian (`output_gain` y `auto_gain` son los de siempre) y el sonido tampoco. El cajón abierto o plegado es una preferencia de la interfaz (`ui.output_stage`, que solo se escribe plegado): **los proyectos anteriores lo abren y se cargan y suenan igual.**

- **Linux: esceQ carga en Ubuntu 22.04, Debian 12, Fedora 36 o posteriores.** Hasta ahora el VST3 y el CLAP de Linux pedían una glibc 2.38 y la libstdc++ de GCC 13, y solo cargaban en distribuciones de 2024 en adelante. Ahora se compilan en Ubuntu 22.04 y piden glibc 2.35 o posterior; el CI lo comprueba en cada cambio. Las instrucciones del paquete dicen los requisitos de cada sistema. Ver la [ADR 0022](docs/decisiones/0022-compatibilidad-en-macos-y-linux.md).
- **macOS: esceQ carga desde macOS 11 en Apple Silicon.** En los paquetes de la 0.1.0, el binario de Apple Silicon pedía la versión de macOS del equipo que lo compiló (macOS 26) y no habría cargado en versiones anteriores, aunque la intención era macOS 11. Ahora los dos binarios, Apple Silicon e Intel, piden macOS 11, y el CI lo comprueba en los tres formatos.
- **macOS: el VST3, el AU y el CLAP van firmados ad hoc, enteros.** Antes solo el binario arm64 llevaba la firma ad hoc del enlazador. El CI comprueba además, en los tres formatos, las dos arquitecturas (Apple Silicon e Intel). Sigue sin haber firma con certificado ni notarización: hay que quitar la cuarentena a mano, como dicen las instrucciones.

  **Compatibilidad:** no cambia el procesado, el estado ni los parámetros: **los proyectos anteriores se cargan y suenan exactamente igual** en los tres sistemas.

- En la barra superior, el selector del rango de la curva pasa de 92 a 72 px y el botón *Bypass* de 76 a 64, para que quepa el botón de la vista ampliada sin cambiar la ventana mínima. Los textos caben enteros.
- Ctrl/Cmd + izquierda/derecha en la curva avanza la frecuencia de la banda seleccionada un semitono, hasta la nota siguiente o la anterior (antes, un 1 % del recorrido, unos 1,2 semitonos). Con Mayús sigue el paso fino de siempre.
- El selector del tamaño de la ventana (S, M, L) sale de la barra superior y pasa al menú de opciones (*Window size*), para dejar sitio al botón *Auto*. Los tamaños son los mismos, y la ventana se sigue pudiendo redimensionar desde la esquina.
- El ancho mínimo de la ventana pasa de 960 a 1120 px, y el tamaño S, de 960 × 580 a 1120 × 580, para que quepan el selector del modo de fase y el botón de opciones. Una ventana guardada más estrecha abre con el ancho mínimo.

- **El aviso al host de un cambio de latencia ya no usa un temporizador.** Al cambiar de modo de fase con el audio en marcha, el hilo de control ve la latencia nueva (como mucho 10 ms después) y avisa al hilo de mensajes, que se la dice al host; antes, un temporizador del hilo de mensajes la miraba 20 veces por segundo, siempre, y el host podía tardar hasta 50 ms en saberla. Una instancia quieta ya no despierta al hilo de mensajes. Ver la [ADR 0021](docs/decisiones/0021-aviso-de-latencia-sin-temporizador.md).

  **Compatibilidad:** no cambia el procesado, el estado ni los parámetros: **los proyectos anteriores se cargan y suenan exactamente igual.**

### Corregido

- **La igualación ya no rellena una banda estrecha que ya hay.** Con un notch en la pista (o una campana estrecha), *Match* proponía una campana de hasta +18 dB y Q 16 justo encima, que lo deshacía: el objetivo está suavizado y la curva de las bandas actuales no lo estaba. Ahora se comparan al mismo ancho. Una banda estrecha cuenta por lo que quita a ese ancho (con 1/3 de octava, un notch de Q 8 lleva junto a él una campana de unos +6 dB y Q 4,3), y las campanas propuestas no son más estrechas que el suavizado. Aplicar y volver a proponer casi nunca crea nada (en el 2 % de 300 casos de prueba, como mucho 2,3 dB; antes, en el 9 %, con hasta 10 dB). Igualar a una curva conocida es aún más preciso (a 0,04 dB, antes 0,13). Ver la [ADR 0023](docs/decisiones/0023-igualar-a-una-referencia.md) y [docs/dsp/igualacion.md](docs/dsp/igualacion.md).

  **Compatibilidad:** no cambia el procesado, el estado ni los parámetros; solo las bandas que propone *Match*.

- **Deshacer la carga de un preset ya no deshace también el último cambio.** Si se cargaba un preset (o *Default*) justo después de mover un control, antes de que el historial lo registrara (300 ms, o más con el ordenador cargado), los dos quedaban en una sola acción, y deshacer la carga devolvía también el control a su valor anterior. Ahora son dos acciones, como al cambiar el algoritmo de filtro.

  **Compatibilidad:** no cambia el procesado, el estado ni los parámetros.

### Rendimiento

- **Medido el coste del procesado, sin cambios.** Una instancia estéreo con una mezcla típica (8 bandas, 2 de ellas dinámicas) ocupa del 0,55 al 0,95 % de un núcleo a 44,1 y 48 kHz y del 2,4 al 3,9 % a 192 kHz, en cualquier modo de fase y con cualquier tamaño de bloque del host. Los casos extremos: 24 bandas dinámicas comprimiendo sin parar, un 5 % a 48 kHz y del 15 al 22 % a 192 kHz; 24 cortes de 96 dB/oct en IIR, un 6 y un 22 %. Optimizar con instrucciones SIMD ahorraría unas décimas en el uso normal, y no se hace ([ADR 0020](docs/decisiones/0020-sin-simd.md)). En fase lineal, el trabajo de la convolución se concentra en una llamada de cada pocas: en Linear High a 96 o 192 kHz conviene usar bloques del host de 128 o 256 muestras o más ([docs/dsp/fase-lineal.md](docs/dsp/fase-lineal.md)).

  **Compatibilidad:** no cambia el procesado, el estado ni los parámetros: **los proyectos anteriores se cargan y suenan exactamente igual.**

## [0.1.0] - 2026-11-12

No llegó a publicarse: sus paquetes de macOS y Linux tenían los fallos que corrige la 0.5.0. Sus contratos (abajo) se mantienen.

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
