# esceQ

Ecualizador paramétrico y dinámico, libre, pensado para funcionar bien en FL Studio y en cualquier host VST3.

Ecualizador de 24 bandas con curva interactiva, analizador de espectro ajustable y con vista ampliada, dinámica por banda con sidechain externo, tiempos automáticos y dinámica espectral, procesado por banda en estéreo, L, R, Mid o Side, cuatro modos de fase (IIR y fase lineal en tres resoluciones), filtros fieles al analógico también en agudos, deshacer y rehacer, y presets. Formatos VST3, CLAP y AU. La hoja de ruta está en [PLAN.md](PLAN.md) y los cambios de cada versión en [CHANGELOG.md](CHANGELOG.md).

![esceQ](docs/img/esceq.png)

## Instalar

Descarga la última versión en [Releases](https://github.com/guillesanper/plugin/releases).

| Sistema | Archivo | Cómo |
|---|---|---|
| Windows (x64) | `esceQ-X.Y.Z-windows-x64-setup.exe` | Ejecuta el instalador: deja el VST3 en `C:\Program Files\Common Files\VST3` y el CLAP en `C:\Program Files\Common Files\CLAP`. Como no está firmado, Windows SmartScreen avisa: "Más información" → "Ejecutar de todas formas". En FL Studio: *Options → Manage plugins → Find installed plugins* |
| macOS 11 o posterior (Apple Silicon e Intel) | `esceQ-X.Y.Z-macos-universal.zip` | Copia `esceQ.vst3`, `esceQ.component` y `esceQ.clap` a las carpetas de `~/Library/Audio/Plug-Ins/`. No están firmados con un certificado de Apple: `INSTALL.txt` explica cómo permitir su carga |
| Linux x64 (Ubuntu 22.04, Debian 12, Fedora 36 o posteriores) | `esceQ-X.Y.Z-linux-x64.tar.gz` | Copia `esceQ.vst3` a `~/.vst3/` y `esceQ.clap` a `~/.clap/` |

FL Studio carga el VST3 por defecto; el CLAP también funciona (FL Studio 2024.1 o posterior).

## Uso

| Acción | Cómo |
|---|---|
| Añadir una banda | Doble clic en un hueco de la curva |
| Atacar una resonancia que se ve | Con el ratón en un hueco, se marca el pico del espectro más cercano: arrastrar desde ahí crea una campana en él, con su Q; con Alt al soltar, dinámica (ver abajo) |
| Mover frecuencia y ganancia | Arrastrar el nodo (con Mayús, ajuste fino) |
| Cambiar el Q | Rueda del ratón sobre el nodo o con la banda seleccionada |
| Cambiar el tipo de filtro | Clic derecho en el nodo, o el panel inferior |
| Eliminar una banda | Doble clic en el nodo |
| Hacer dinámica una banda | Botón "Dynamic" del panel inferior, o clic derecho en el nodo |
| Que la dinámica elija sola su ataque y su relajación | Interruptor *Auto* bajo la fuente del detector, en el panel inferior (ver abajo) |
| Bajar solo las frecuencias que molestan dentro de una banda | Interruptor *Spectral*, junto a *Auto* (ver abajo; añade 53 ms de latencia) |
| Elegir el canal de una banda | Selector ST / L / R / M / S del panel inferior, o clic derecho en el nodo |
| Ajustar un potenciómetro | Arrastrar; Mayús para ajuste fino; rueda; doble clic o Ctrl/Cmd + clic para el valor por defecto |
| Escribir un valor | Clic en el número bajo el potenciómetro ("1.2k", "-3", "350 Hz") |
| Deshacer y rehacer | Flechas de la barra superior, o Ctrl/Cmd+Z y Ctrl/Cmd+Mayús+Z. Un arrastre es una sola acción |
| Cargar o guardar un preset | Nombre del preset en la barra superior; las flechas de los lados pasan al anterior o al siguiente |
| Elegir el modo de fase | Desplegable de la barra superior (ver abajo); su ayuda dice la latencia |
| Elegir la precisión en agudos | Botón de opciones (tres deslizadores) de la barra superior → *HF accuracy*: *Classic* o *Matched* (ver abajo). Se puede deshacer |
| Igualar la sonoridad al comparar con el bypass | Botón *Auto* de la barra superior, junto a la salida: encendido, dice la compensación ("Auto -2.3 dB"; ver abajo) |
| Leer y fijar frecuencias como notas | Botón de opciones → *Notes* → *Piano keyboard* muestra un teclado bajo la curva; la afinación, en *Tuning* (ver abajo) |
| Llevar una banda a una nota | Alt (Option en macOS) al arrastrar el nodo; Ctrl/Cmd + izquierda/derecha con la banda seleccionada avanza un semitono; o escribir la nota en el campo de frecuencia ("A3", "C#2", "Eb4") |
| Igualar la pista a otra | Botón de comparar → *Match to reference…*: elegir la referencia (otra instancia de esceQ o el sidechain), *Learn* mientras suenan las dos al menos 10 s, y *Apply* crea las bandas que faltan, en una sola acción de deshacer (ver abajo) |
| Ampliar la curva y el analizador | Botón de las cuatro esquinas, junto al selector del analizador, o F con el foco en la curva; Esc o el mismo botón vuelven (ver abajo) |
| Ajustar el analizador | Botón de opciones → *Analyzer*: resolución, caída, inclinación, mantener picos y congelar (ver abajo) |
| Cambiar el tamaño de la ventana | Botón de opciones → *Window size*: *Small*, *Medium* o *Large*; o arrastrar la esquina |
| Teclado | Tabulador recorre los controles; flechas para cambiar el valor; en la curva, izquierda y derecha eligen banda |

Tipos: campana, shelf de graves y de agudos, corte de graves y de agudos (6 a 96 dB/oct), notch, paso banda, paso todo y tilt. Todo es automatizable desde el host.

### Modos de fase

| Modo | Qué es | Latencia a 48, 96 o 192 kHz (a 44,1 kHz, un 9 % más) | Para qué |
|---|---|---|---|
| **IIR** | Filtros IIR: la fase cambia con la frecuencia, como en un ecualizador analógico | 0 | Mezcla, grabación, tiempo real |
| **Linear Low** | Fase lineal (FIR de 2048 coeficientes a 48 kHz) | 26,7 ms | Fase lineal con poca latencia; poca resolución en graves |
| **Linear Medium** | FIR de 8192 coeficientes | 96 ms | Buses y mastering |
| **Linear High** | FIR de 32768 coeficientes | 362,7 ms | Mastering y render: máxima resolución en graves |

En fase lineal todas las frecuencias se retrasan lo mismo y la forma de onda no se deforma; a cambio hay latencia (el host la compensa) y algo de preeco. La curva es la misma en los cuatro modos, y la dinámica funciona en todos. Cambiar de modo silencia la salida un momento. Elegir un preset no cambia el modo. Detalles en [docs/dsp/fase-lineal.md](docs/dsp/fase-lineal.md).

### Precisión en agudos

Los filtros digitales clásicos se deforman cerca de la mitad de la frecuencia de muestreo: a 44,1 o 48 kHz, una campana a 15 kHz sale más estrecha que la de un ecualizador analógico, un shelf de agudos cae antes de tiempo y un corte de agudos se hunde de más. esceQ tiene dos algoritmos de filtro, que se eligen en el menú de opciones (*HF accuracy*):

| Algoritmo | Qué es | Cuándo se usa |
|---|---|---|
| **Matched** | La magnitud de cada filtro se ajusta a la del analógico, también cerca de Nyquist. Con bandas entre 1 y 16 kHz a 44,1 kHz, el peor error frente al analógico baja de 10,7 a 1,75 dB en campanas y de 87 a 3,8 dB en cortes de agudos | Por defecto en cada instancia nueva |
| **Classic** | El de la 0.1 | Los proyectos guardados con versiones anteriores, que así suenan exactamente igual |

Por debajo de unos 2 kHz (a 48 kHz) los dos suenan prácticamente igual. Se guarda con el proyecto, cambiarlo se puede deshacer y los presets no lo cambian. La curva se dibuja siempre con el algoritmo elegido. Detalles en [docs/dsp/filtros.md](docs/dsp/filtros.md).

### Compensación automática de ganancia

Al realzar, todo suena más fuerte, y lo más fuerte parece mejor: comparar con el bypass engaña. Con el botón *Auto* (parámetro automatizable `auto_gain`), esceQ baja o sube la salida lo justo para que la sonoridad no cambie al ecualizar, y el botón dice cuánto ("Auto -2.3 dB"). La compensación sale de la curva, no de medir el audio: es la que deja igual la sonoridad de ruido rosa medida como en ITU-R BS.1770, así que no bombea con la música. Solo compensa la ganancia estática de las bandas, no la dinámica (compensarla anularía su efecto), se limita a ±12 dB, se suma a la ganancia de salida y entra con una rampa sin chasquidos, en cualquier modo de fase. Con el bypass puesto no actúa. Está apagada por defecto y en los proyectos anteriores, y elegir un preset no la cambia. Detalles en [docs/dsp/compensacion-de-ganancia.md](docs/dsp/compensacion-de-ganancia.md).

### Escala de notas

Para ver qué nota es una resonancia o llevar una banda justo a la fundamental de un bajo o de un bombo. En el botón de opciones → *Notes*, *Piano keyboard* muestra bajo la curva un teclado de piano a lo largo del eje de frecuencias, de E0 (20,6 Hz) a D#10 (19,9 kHz), con la octava en cada do. Al pasar el ratón por una tecla se ilumina y la curva marca su frecuencia ("A3 · 220 Hz"). Con el teclado visible, la etiqueta de un nodo dice también la nota más cercana y cuánto se desvía, de −50 a +50 cents ("220 Hz · A3 +0 ct"), y la etiqueta del potenciómetro de frecuencia del panel dice esa nota.

Con Alt (Option en macOS) al arrastrar un nodo, la frecuencia salta a la nota temperada más cercana; Ctrl/Cmd + izquierda/derecha avanza un semitono (con Mayús, en pasos finos); y en el campo de frecuencia se puede escribir una nota en inglés, con `#` para el sostenido y `b` para el bemol: "A3", "C#2", "Eb4". La afinación es A4 = 440 Hz; *Tuning* ofrece las habituales (415, 432, 442, 443, 444 Hz) y *Other...* cualquiera de 400 a 480 Hz, en pasos de 0,1. El teclado y la afinación se guardan con el proyecto, no con el preset, y no cambian el sonido: la frecuencia de la banda se sigue guardando como número.

### Analizador ampliado

Para trabajar el espectro con detalle. El botón de las cuatro esquinas, a la derecha del selector del analizador (o la tecla F con el foco en la curva), oculta el panel de la banda y el medidor de salida: la curva y el analizador ocupan toda la ventana menos la barra superior. Combinado con el tamaño *Large* o arrastrando la esquina, ocupa lo que quieras. Las bandas se siguen editando en la curva (arrastre, rueda, menú contextual), y la etiqueta de un nodo dice también su canal y su dinámica (umbral, rango y tiempos). Esc o el mismo botón vuelven a la vista normal, igual que estaba. No es la pantalla completa del sistema, que dentro de un host no es fiable.

En el botón de opciones → *Analyzer*, disponibles también sin ampliar:

| Ajuste | Valores | Por defecto |
|---|---|---|
| *Resolution* | FFT de 4096, 8192 o 16384 puntos: más puntos, más detalle en graves y una respuesta más lenta | 4096 |
| *Fall* | Lo que tarda en bajar: *Slow* (1 s), *Medium* (0,35 s) o *Fast* (0,12 s); la subida es siempre rápida | *Medium* |
| *Tilt* | Inclinación: 0, 3, 4,5 o 6 dB/oct (con 4,5, la música suele verse plana) | 4,5 dB/oct |
| *Peak hold* | Una línea fina con el máximo de cada frecuencia; un clic en la escala de dB, a la derecha de la curva, la borra | Apagado |
| *Freeze* | Detiene el analizador para mirarlo con calma ("Analyzer frozen" en la curva) | Apagado |

Los valores por defecto son los del analizador de siempre. Los cuatro primeros se guardan con el proyecto, no con el preset; ni la vista ampliada ni la congelación se guardan: un proyecto abre siempre en la vista normal y con el analizador en marcha. Nada de esto cambia el sonido.

### Bandas desde el espectro

La forma más rápida de atacar una resonancia que se ve. Con el analizador encendido y el ratón en un hueco de la curva, se marca el pico del espectro más cercano, con su frecuencia, su nota y su nivel. Al arrastrar desde ahí se crea una campana en la frecuencia del pico, con el Q de su ancho (a −3 dB de su máximo, de 0,5 a 30), y la ganancia sigue al ratón: hacia abajo, recorte. Con Alt (Option en macOS) al soltar, la banda queda dinámica: no actúa hasta que la resonancia pasa de un umbral 3 dB por debajo del nivel del pico, y entonces baja como mucho lo arrastrado. Un clic sin arrastre no crea nada, y todo el gesto se deshace de una vez. Se usa el espectro post (el pre en pre + post), promediado durante un segundo para marcar lo que se mantiene, no el azar del ruido; con el analizador congelado se escoge con calma. Las bandas que crea son bandas normales: no hay parámetros nuevos ni cambia nada de lo que se guarda. Detalles en [docs/dsp/picos.md](docs/dsp/picos.md).

**Dinámica:** las campanas, los shelves y el tilt pueden ser dinámicos. Cuando el nivel en su zona del espectro supera el umbral, la banda baja (rango negativo: de-esser, control de resonancias) o sube (rango positivo) hasta el rango indicado, con ataque, relajación y rodilla ajustables. La curva muestra en tiempo real la ganancia que se está aplicando, con la estática punteada como referencia. El detector escucha la entrada del plugin o, con "External", el sidechain.

**Tiempos automáticos:** con el interruptor *Auto* de la dinámica (parámetro `bNN_autotime`, uno por banda), la banda elige sola su ataque y su relajación según su frecuencia: en graves, un ataque de dos periodos para que la ganancia no siga la forma de onda (no distorsiona); en agudos, medio milisegundo para atrapar las sibilantes. La relajación depende de la señal: un golpe aislado se suelta deprisa, y una nota larga se suelta despacio para que no bombee. Encendido, los potenciómetros de ataque y relajación dicen "Auto" y conservan su valor para cuando se apague. Está apagado por defecto y en los proyectos anteriores; los presets lo guardan. Detalles en [docs/dsp/dinamica.md](docs/dsp/dinamica.md).

**Dinámica espectral:** con el interruptor *Spectral* de la dinámica (parámetro `bNN_spectral`, uno por banda), la banda deja de subir y bajar entera: dentro de su zona solo se mueven las frecuencias que pasan del umbral, cada una lo suyo. Sirve para resonancias que cambian de sitio (los armónicos de una voz, las notas que retumban en una guitarra acústica) sin apagar lo que las rodea. Usa los mismos controles de la dinámica y necesita que esté encendida. La curva sombrea bajo la banda la forma de la reducción, y el medidor dice la de la frecuencia que más baja. Mientras alguna banda lo tenga encendido, el plugin añade 53,3 ms de latencia (58 ms a 44,1 kHz), que el host compensa; encender la primera banda espectral, o apagar la última, silencia la salida un momento, como cambiar de modo de fase, así que conviene no automatizarlo. Está apagado por defecto y en los proyectos anteriores; los presets lo guardan. Detalles en [docs/dsp/dinamica-espectral.md](docs/dsp/dinamica-espectral.md).

**Canal:** cada banda actúa en estéreo, solo en el canal izquierdo o el derecho, o solo en Mid (lo que está en el centro) o Side (lo que está a los lados). Lo que la banda no toca pasa intacto. Una banda dinámica escucha ese mismo canal. El nodo de una banda que no es estéreo lleva su letra (L, R, M, S).

**Presets:** vienen cinco de fábrica (De-esser, Resonance Control, Low-End Cleanup, Vocal y Gentle Master) como punto de partida. Los tuyos se guardan con "Save as..." como archivos `.esceq` en `%APPDATA%\esceQ\Presets` (Windows), `~/Library/Application Support/esceQ/Presets` (macOS) o `~/.config/esceQ/Presets` (Linux); "Open presets folder" abre esa carpeta. Un punto junto al nombre indica que has modificado el preset. El analizador (modo y ajustes), el rango, el tamaño de la ventana y la escala de notas se guardan con el proyecto, no con el preset.

**Sidechain en FL Studio** (pasos aún sin verificar en FL Studio): en el mezclador, haz clic derecho en la flecha de envío de la pista que debe controlar el EQ y elige "Sidechain to this track" en la pista donde está esceQ. Luego, en los ajustes del envoltorio del plugin (rueda dentada, pestaña *Processing*), asigna esa entrada al puerto de sidechain. Si eliges "External" sin nada conectado, el panel lo avisa con "No sidechain".

### Igualar a una referencia

Para que una pista tenga el balance de otra, o igualar dos tomas. En el menú del botón de comparar, *Match to reference…* abre el panel *Match* sobre la esquina de la curva. Se elige la referencia: otra instancia de esceQ del proyecto (se iguala a cómo suena, con su ecualizador) o la entrada de sidechain, para una pista sin esceQ o un bus. *Learn* escucha las dos mientras suenan, y con 10 s útiles la curva muestra los dos espectros, el objetivo (discontinuo) y la propuesta, con los nodos de las bandas nuevas en fantasma. *Amount* dice cuánto de la diferencia se iguala; *Smoothing*, el detalle (1/3 de octava por defecto); *Max bands*, cuántas bandas como mucho, y *Range*, entre qué frecuencias. *Apply* crea campanas y shelves normales en las bandas libres, de una vez (se deshace de una vez), y aplicar otra vez no crea más. Se mide la entrada de la pista, antes del ecualizador, y el nivel no se iguala. Lo aprendido se pierde al cerrar el proyecto. Detalles en [docs/dsp/igualacion.md](docs/dsp/igualacion.md).

## Compilar

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Detalles, requisitos y reglas de contribución en [CONTRIBUTING.md](CONTRIBUTING.md).

## Documentación

- [DEV.md](DEV.md): cómo montar el entorno de desarrollo, paso a paso.
- [PLAN.md](PLAN.md): objetivos, decisiones y hoja de ruta.
- [docs/arquitectura.md](docs/arquitectura.md): módulos, contratos y modelo de hilos.
- [docs/ui.md](docs/ui.md): sistema visual, controles y accesibilidad.
- [docs/dsp/filtros.md](docs/dsp/filtros.md): cómo están hechos los filtros.
- [docs/dsp/ecualizador.md](docs/dsp/ecualizador.md): suavizado, fundidos y analizador.
- [docs/decisiones/0009-preferencias-deshacer-y-presets.md](docs/decisiones/0009-preferencias-deshacer-y-presets.md): estado guardado, deshacer y presets.
- [docs/dsp/dinamica.md](docs/dsp/dinamica.md): detector, envolvente y curva de ganancia de las bandas dinámicas.
- [docs/dsp/dinamica-espectral.md](docs/dsp/dinamica-espectral.md): la dinámica espectral (STFT, detector por frecuencia y máscaras).
- [docs/decisiones/](docs/decisiones/README.md): registro de decisiones de diseño.
- [CHANGELOG.md](CHANGELOG.md): cambios por versión.

## Licencia

El plugin se distribuye bajo [AGPL-3.0-or-later](LICENSE). La biblioteca de `dsp/` se distribuye bajo [MIT](dsp/LICENSE). La interfaz incluye la tipografía [Inter](https://rsms.me/inter/), bajo [SIL Open Font License 1.1](ui/assets/fonts/OFL.txt).
