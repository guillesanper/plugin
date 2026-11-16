# esceQ: plan del proyecto

Plugin de ecualización paramétrica y dinámica, libre, pensado para funcionar bien en FL Studio y con un flujo de trabajo comparable al de los ecualizadores comerciales de referencia (FabFilter Pro-Q).

Estado: **fases 0 a 4 completadas; fase 5 en curso.** La 0.1.0 está cerrada en `CHANGELOG.md` (2026-11-12) y el CI la compila, valida y empaqueta en Windows, macOS y Linux, pero aún falta subir la etiqueta `v0.1.0`, que publica la release en GitHub. De la fase 5 están hechas la fase lineal, la corrección en agudos (algoritmo Matched, con el estado en la versión 3: sección 4.1) y, adelantada de la fase 6, la detección de colisiones entre instancias. También están hechas la compensación automática de ganancia (sección 5.5, [ADR 0015](docs/decisiones/0015-compensacion-automatica-de-ganancia.md)) los tiempos automáticos en la dinámica (sección 5.4, [ADR 0016](docs/decisiones/0016-tiempos-automaticos-en-la-dinamica.md)) y la escala de notas (sección 5.1, [ADR 0017](docs/decisiones/0017-escala-de-notas.md)). Lo siguiente es el analizador ampliado (sección 5.2). Sigue pendiente comprobar en FL Studio, con una mezcla real, el enrutado del sidechain.

---

## 1. Objetivo y alcance

### Qué queremos construir

- EQ paramétrico de hasta 24 bandas con interfaz de arrastre directo sobre la curva.
- Cada banda puede ser **dinámica**: su ganancia se mueve según el nivel de la señal en esa zona del espectro (compresión o expansión por banda).
- Analizador de espectro en tiempo real (pre, post y sidechain).
- Procesado por banda en estéreo, L, R, Mid o Side.
- Sidechain externo.
- Modos de fase: IIR (latencia cero) y fase lineal en tres resoluciones, Low, Medium y High (hechos en la fase 5, ver [ADR 0012](docs/decisiones/0012-modos-de-fase.md)).
- Pendiente, especificado en la sección 5: escala de notas, analizador ampliado, bandas desde el espectro, tiempos automáticos en la dinámica, compensación automática de ganancia, igualar a una referencia, dinámica espectral y saturación.

### Qué no es

- No es un clon. No se copia el nombre, la interfaz, los gráficos ni los presets de ningún producto comercial. "Pro-Q" y "FabFilter" son marcas registradas: solo se pueden citar de forma comparativa, nunca en el nombre ni en la identidad del plugin.
- No se hace ingeniería inversa de binarios. Todo el DSP sale de literatura pública.

### Plataforma objetivo

| Prioridad | Plataforma | Formato | Motivo |
|---|---|---|---|
| 1 | Windows x64 | VST3 | Es donde está la mayoría de usuarios de FL Studio y el formato que FL trata por defecto |
| 2 | Windows x64 | CLAP | FL Studio lo soporta desde 2024.1; si hay VST3 y CLAP del mismo plugin, FL abre VST3 salvo que el usuario elija CLAP |
| 3 | macOS (arm64 + x64) | VST3, AU, CLAP | FL Studio en macOS carga los tres |
| 4 | Linux | VST3, CLAP, LV2 | FL no corre nativo, pero amplía la comunidad (Bitwig, Reaper, Ardour) |

---

## 2. Decisiones tomadas

| Tema | Decisión |
|---|---|
| Nombre | **esceQ** |
| Punto de partida | De cero. ZL Equalizer (AGPLv3, JUCE) se usa solo como referencia de arquitectura, sin copiar código |
| Framework | JUCE 9 (mismas condiciones de licencia que la 8), confinado a las capas de plugin e interfaz |
| Licencia | Plugin en AGPLv3; biblioteca DSP en MIT |
| Primera versión | EQ estático sólido primero (fase 2), dinámica justo después (fase 3) |
| Principio rector | **Diseñar y documentar de forma que un cambio no rompa nada** (ver 2.1) |

Nombre comprobado en noviembre de 2026: no se ha encontrado ningún plugin, producto ni marca registrada llamado "esceQ" (detalle y límites de la búsqueda en la [ADR 0010](docs/decisiones/0010-distribucion.md)).

### 2.1 Diseñar y documentar para que un cambio no rompa nada

El código va a cambiar mucho (nuevos filtros, nuevos modos, otra interfaz). El objetivo es que cualquier cambio sea local, y que si rompe algo, se note enseguida y no en el proyecto de un usuario.

**Diseño: que los cambios sean locales**

- **Módulos con dependencias en un solo sentido:** `dsp` ← `core` ← `plugin` ← `ui`. Ningún módulo conoce a los que tiene por encima. El CI compila `dsp` y `core` solos para que la regla no se rompa por descuido.
- **Interfaces pequeñas y explícitas** entre módulos: cada uno expone unas pocas cabeceras públicas; lo demás es interno y se puede reescribir sin tocar a nadie.
- **Una sola fuente de verdad para los parámetros:** una tabla única define identificador, rango, valor por defecto y unidades. El procesador, la interfaz, los presets y la documentación se generan o leen de ahí; añadir un parámetro es tocar un sitio.
- **Añadir sin modificar:** un tipo de filtro nuevo o un modo de fase nuevo se añade implementando una interfaz existente, no editando un `switch` repartido por diez archivos.
- **Lógica separada del pintado** en la interfaz (selección, conversión de arrastre a frecuencia y ganancia, escalas), para poder testearla sin ventana y rediseñar lo visual sin tocar el comportamiento.

**Contratos que no se pueden romper**

Son las tres cosas que, si cambian, estropean proyectos ya guardados por usuarios:

1. **Identificadores de parámetro:** congelados desde la 0.1. Nunca se renombran ni se reutilizan; solo se añaden.
2. **Formato del estado guardado:** propio, con número de versión y migraciones explícitas de cada versión a la siguiente.
3. **Sonido de una configuración dada:** un preset antiguo debe sonar igual tras actualizar. Si una mejora cambia el sonido, va detrás de una versión de algoritmo guardada en el estado.

**Tests: la red que avisa**

| Qué protege | Test |
|---|---|
| El sonido | Respuesta en frecuencia contra la fórmula analítica, y comparación con salidas de referencia guardadas ("golden files") con tolerancia |
| Proyectos antiguos | Estados guardados de cada versión publicada se conservan en `tests/fixtures/` y deben cargar siempre |
| Parámetros | Test que falla si un identificador desaparece o cambia de rango |
| Tiempo real | RealtimeSanitizer: falla si el hilo de audio asigna memoria o bloquea |
| Compatibilidad con hosts | `pluginval` y `clap-validator` en cada push |
| Robustez | Bloques de 1 a 8192 muestras, cambios bruscos de parámetros, frecuencias de muestreo de 44,1 a 192 kHz |

Regla: no se fusiona nada con el CI en rojo, y cada bug arreglado deja un test que lo reproduce.

**Documentación: que el porqué no se pierda**

- `docs/arquitectura.md`: mapa de módulos, quién depende de quién, modelo de hilos. Se actualiza en el mismo commit que cambia la arquitectura.
- `docs/decisiones/` (ADR): una nota corta por decisión importante con contexto, opciones y motivo. Evita deshacer por error algo que se decidió por una buena razón.
- `docs/dsp/`: derivación y referencia de cada algoritmo, enlazada desde el código.
- Comentarios en las cabeceras públicas: qué garantiza cada función (seguridad de hilos, si asigna memoria, unidades y rangos).
- `CHANGELOG.md` y versionado semántico: un cambio que afecte a estado, parámetros o sonido se anuncia y sube la versión correspondiente.
- `CONTRIBUTING.md`: cómo compilar, testear y qué reglas respeta un cambio.

**Framework y licencia**

Consecuencia de lo anterior: como `dsp` y `core` no dependen de JUCE y el formato de estado es propio, sustituir el framework o la biblioteca gráfica afecta solo a `plugin` y `ui`. Para la licencia: identificador SPDX en cada archivo, ningún código AGPL/GPL ajeno dentro de `dsp` (que es MIT), y acuerdo de contribución antes de aceptar código de terceros si se quiere conservar la opción de relicenciar.

### 2.2 Alternativas descartadas

| Framework | Licencia | Por qué no ahora |
|---|---|---|
| CLAP nativo + clap-wrapper | MIT | Sin GUI propia: demasiado trabajo de fontanería para empezar. Es el candidato natural si algún día se abandona JUCE |
| DPF | ISC | GUI demasiado básica para esta interfaz |
| iPlug2 | Permisiva | Comunidad y documentación menores |
| nih-plug (Rust) | ISC | Sin AU; ecosistema de GUI menos maduro |

Notas de licencias: el SDK de VST3 es MIT desde la 3.8 (octubre de 2025) y CLAP es MIT, así que ninguno de los dos formatos condiciona la licencia del plugin.

---

## 3. Arquitectura

```
plugin/
├── dsp/            Biblioteca C++20 pura (MIT). Sin JUCE, sin asignaciones en tiempo real:
│                   filtros SVF, ecualizador, dinámica, FFT y analizador
├── core/           Tabla de parámetros, estado, presets, deshacer. Sin JUCE.
├── plugin/         Capa JUCE: AudioProcessor, parámetros del host, deshacer y presets
├── ui/
│   ├── logic/      Lógica de la interfaz sin JUCE (escalas, gestos, disposición), con tests
│   └── src/        Componentes JUCE: curva, nodos, analizador, panel de banda
├── tests/          Tests unitarios y de respuesta (Catch2)
│   └── fixtures/   Estados guardados y salidas de referencia de versiones anteriores
├── tools/          Captura del editor sin host y notas de la release
├── packaging/      Instalador de Windows e instrucciones de macOS y Linux
└── docs/           arquitectura.md, decisiones/ (ADR), dsp/, ui.md
```

Así está el repositorio desde la fase 4; el detalle de cada módulo está en [docs/arquitectura.md](docs/arquitectura.md). Desde la fase 5 está también la fase lineal (`dsp/`, con su hilo de diseño en `core/`). Las medidas de rendimiento (un `bench/`) aún no existen.

### Hilos y comunicación

- **Hilo de audio:** nunca asigna memoria, nunca bloquea, nunca hace E/S.
- **Parámetros:** atómicos, leídos una vez por bloque y suavizados por muestra o por sub-bloque.
- **Audio → interfaz** (analizador, medidores de reducción de ganancia): FIFO sin bloqueo de un productor y un consumidor.
- **Interfaz → audio:** solo a través de parámetros del host, para que automatización, deshacer y presets funcionen igual.

### Parámetros

Los hosts exigen una lista de parámetros fija. Se declaran las 24 bandas desde el principio (unas 15 propiedades por banda: activa, tipo, frecuencia, ganancia, Q, pendiente, canal, dinámica activa, umbral, rango, ataque, relajación, etc.), más los globales. Los identificadores de parámetro se congelan en la primera versión pública: cambiarlos rompe proyectos guardados.

### Particularidades de FL Studio

- FL entrega **bloques de tamaño variable**, a veces de muy pocas muestras, salvo que el usuario active "Use fixed size buffers". El motor debe funcionar con cualquier tamaño, incluido 1.
- La latencia (fase lineal, lookahead) se comunica al host para la compensación de retardo; cambiarla en caliente puede causar un clic, así que el cambio de modo de fase debe gestionarse con cuidado.
- El sidechain externo en FL se enruta desde el mezclador a una entrada auxiliar del plugin: hay que declarar el bus de sidechain como opcional y probar ese enrutado expresamente.
- Escalado de interfaz en pantallas HiDPI en Windows: probar al 100, 125, 150 y 200 %.

---

## 4. DSP

### 4.1 Filtros

- **Topología:** filtro de variable de estado con integradores trapezoidales (TPT/SVF de Zavalishin y Simper). A diferencia del biquad en forma directa, se mantiene estable y sin chasquidos cuando sus parámetros cambian a velocidad de audio, que es justo lo que necesita un EQ dinámico.
- **Tipos:** bell, low/high shelf, low/high cut, notch, band-pass, tilt, all-pass.
- **Pendientes:** de 6 a 96 dB/oct encadenando secciones de primer y segundo orden (Butterworth para los cortes).
- **Deformación cerca de Nyquist ("cramping"):** la transformada bilineal estrecha las campanas en agudos. Opciones: coeficientes ajustados a la respuesta analógica (Vicanek, Orfanidis) o sobremuestreo ×2. Empezar sin corrección, medir, y añadir la de Vicanek en la fase 5.

**Hecho (fase 5).** El algoritmo Matched (Vicanek, con atenuaciones como inversa del realce, el high shelf a partir del low shelf y los shelves resonantes en dos secciones), junto al Classic de la 0.1, que no cambia ni un bit. Mejora el error frente al analógico de 10,7 a 1,75 dB en campanas a 44,1 kHz y de 87 a 3,8 dB en cortes de agudos. Cada instancia guarda su algoritmo en el estado, que pasa a la versión 3: lo guardado antes carga en Classic y suena igual, las instancias nuevas arrancan en Matched y los presets no lo cambian. Se elige en el menú de opciones de la barra superior (*HF accuracy*), con deshacer. Decisiones en las ADR [0013](docs/decisiones/0013-correccion-en-agudos.md) y [0014](docs/decisiones/0014-estado-v3-algoritmo-de-filtro.md); derivación y medidas en [docs/dsp/filtros.md](docs/dsp/filtros.md).
- **Curva dibujada:** se calcula analíticamente a partir de los coeficientes, no midiendo el audio.

### 4.2 Banda dinámica

Cadena por banda:

1. **Detector:** la señal (interna o sidechain) pasa por un band-pass centrado en la frecuencia de la banda, con su mismo Q.
2. **Envolvente:** seguidor de nivel con ataque y relajación (pico o RMS).
3. **Curva de ganancia:** umbral, rango máximo (positivo expande, negativo comprime) y rodilla suave. Opción de umbral automático, y tiempos automáticos (sección 5.4).
4. **Aplicación:** la ganancia resultante se suma a la ganancia estática de la banda.

Para campanas y shelves conviene la forma en paralelo `salida = entrada + (g − 1) · filtro(entrada)`: cambiar la ganancia es multiplicar por un escalar, sin recalcular coeficientes en cada muestra.

### 4.3 Analizador

FFT de 4096 a 8192 puntos, ventana de Hann, solapamiento del 75 %, inclinación configurable (4,5 dB/oct por defecto), suavizado por fracciones de octava, caída temporal ajustable. El cálculo de la FFT se hace en el hilo de la interfaz, no en el de audio.

### 4.4 Fase lineal (fase 5)

FIR generado a partir de la respuesta en magnitud y aplicado por convolución particionada con FFT. Latencia según resolución (baja, media, alta). Combinarlo con bandas dinámicas es el punto difícil: una vía es aplicar la parte dinámica como diferencia sobre el FIR estático. Hay que investigarlo antes de comprometerse.

**Hecho.** Cuatro modos: IIR (latencia cero) y Linear Low, Medium y High (FIR de 2048, 8192 y 32768 coeficientes a 48 kHz, con 26,7, 96 y 362,7 ms de latencia). La dinámica se aplica como `H(−g)·H(g+d)` sobre el FIR con la parte estática, que da exactamente la magnitud del modo IIR y es de fase lineal mientras la dinámica no actúa. Decisiones en la [ADR 0012](docs/decisiones/0012-modos-de-fase.md); funcionamiento, medidas y límites en [docs/dsp/fase-lineal.md](docs/dsp/fase-lineal.md).

### 4.5 Reglas de tiempo real

Sin `new`/`malloc`, sin mutex, sin llamadas al sistema en el hilo de audio; denormales desactivados; suavizado en todos los parámetros audibles; SIMD solo después de medir.

---

## 5. Funciones previstas

Funciones del flujo de trabajo de referencia que esceQ aún no tiene, comprobado en noviembre de 2026 contra el código. Hay ya: curva interactiva con analizador pre y post, dinámica por banda con sidechain, 24 bandas, canal por banda (L, R, M, S), cuatro modos de fase y colisiones entre instancias. Los nombres son propios: ninguno copia el de una función comercial.

Reglas comunes a todas:

- **Contratos (sección 2.1):** los parámetros nuevos se añaden al final de la tabla, y su valor por defecto deja el sonido como está: un proyecto o preset anterior suena idéntico, bit a bit, sin tocar nada. Las preferencias de la interfaz van en líneas `ui.*` del estado, que no sube de versión si solo se añaden líneas ([ADR 0009](docs/decisiones/0009-preferencias-deshacer-y-presets.md)).
- **Tiempo real (sección 4.5):** lo que corre en el hilo de audio no asigna ni bloquea, y se comprueba con RealtimeSanitizer. Lo que es análisis o diseño corre en la interfaz o en un hilo propio, como el diseño de la fase lineal.
- **Lógica sin JUCE:** los cálculos (notas, picos, ajuste de bandas, compensación) van en `dsp`, `core` o `ui/logic`, con tests. En `ui/src` solo se pinta.
- **Deshacer:** cada gesto que cambia varias bandas a la vez (tomar un pico, aplicar una igualación) es una sola acción.
- **Documentación:** las funciones con DSP nuevo llevan su ADR y su página en `docs/dsp/` antes de fusionarse; todas, su entrada en `CHANGELOG.md` y en la tabla de uso del README.

| Función | Sección | Fase | Parámetros nuevos | Latencia |
|---|---|---|---|---|
| Escala de notas ✅ | 5.1 | 5 | Ninguno (preferencias) | No |
| Analizador ampliado | 5.2 | 5 | Ninguno (preferencias) | No |
| Bandas desde el espectro | 5.3 | 5 | Ninguno | No |
| Tiempos automáticos en la dinámica ✅ | 5.4 | 5 | 24 (uno por banda) | No |
| Compensación automática de ganancia ✅ | 5.5 | 5 | 1 global | No |
| Igualar a una referencia | 5.6 | 6 | Ninguno (crea bandas normales) | No |
| Dinámica espectral | 5.7 | 6 | 24 (uno por banda) | Sí, mientras se use |
| Saturación | 5.8 | 6 | 3 globales | Solo con sobremuestreo en fase lineal |

### 5.1 Escala de notas

**Hecha** (noviembre de 2026): [ADR 0017](docs/decisiones/0017-escala-de-notas.md). Cada criterio de aceptación tiene su test (`tests/ui/notes_test.cpp`, `tests/ui/layout_test.cpp`, `tests/ui/options_menu_test.cpp` y `tests/core/state_test.cpp`). Lo que cambió respecto a lo previsto, con sus motivos y medidas en la ADR: el teclado se muestra desde el menú de opciones (*Notes* → *Piano keyboard*, con la afinación en el submenú *Tuning*) y no con un botón de la barra superior, que con la ventana mínima no tiene sitio; en el panel la nota sustituye a la etiqueta "FREQ" del potenciómetro, porque junto al valor no cabe en los tamaños S y M; Ctrl/Cmd + izquierda/derecha avanza un semitono siempre (con Mayús, el paso fino de antes); y las dos preferencias solo se escriben en el estado si no tienen su valor por defecto, así que un proyecto que no las usa se guarda igual que antes. Ajustar y escribir notas funciona también con el teclado oculto. Cada do lleva su octava si cabe: de C1 a C9 en los tamaños S y M, y también C10 en ventanas más anchas.

**Qué es.** Un teclado de piano a lo largo del eje de frecuencias, para leer y fijar frecuencias como notas: ver qué nota es una resonancia y llevar una banda justo a la fundamental de un bajo o de un bombo.

**Comportamiento**

- Franja de teclas de 14 px bajo la curva, de E0 (20,6 Hz) a D♯10 (19,9 kHz). Las teclas negras van más cortas, y cada do lleva su octava (C1 a C9). Se muestra u oculta con un botón de la barra superior. Por defecto está oculta.
- Al pasar el ratón por una tecla se ilumina y se dibuja su frecuencia como línea vertical en la curva.
- La etiqueta flotante del nodo añade la nota más cercana y la desviación en cents, de −50 a +50: "220 Hz · A3 +0 ct". También el campo de frecuencia del panel inferior.
- **Ajustar a semitonos:** con Alt (Option en macOS) pulsado al arrastrar un nodo, la frecuencia salta a la nota temperada más cercana. Lo mismo con Ctrl/Cmd + izquierda/derecha, que avanza un semitono. Hoy Alt no se usa en la curva.
- En el campo de frecuencia se puede escribir una nota: "A3", "C#2" o "Eb4" (en inglés, con sostenido `#` o bemol `b`).
- Afinación: A4 = 440 Hz por defecto, ajustable de 400 a 480 Hz en pasos de 0,1 Hz.

**Estado:** preferencias `ui.notes` (0 o 1) y `ui.a4` (en Hz). No hay parámetros, y la frecuencia de la banda sigue guardándose como número real: la nota solo se calcula al mostrarla.

**Lógica** (`ui/logic`): `noteForFrequency(f, a4) → {midi, cents}`, `frequencyForNote(midi, a4)`, `snapToSemitone(f, a4)`, `parseNote(texto)` y `noteName(midi)`.

**Aceptación**

- 440 Hz es MIDI 69 con 0 ct, y 261,626 Hz es C4 con menos de 0,1 ct de error. Con A4 = 432 Hz, 432 Hz es A4.
- En el límite de ±50 ct el redondeo es estable, siempre hacia la misma nota.
- `frequencyForNote(noteForFrequency(f))` recupera la nota.
- "Eb4" y "D#4" dan la misma frecuencia, y los textos inválidos se rechazan sin cambiar el valor.
- Con la franja oculta la disposición es la de ahora, píxel a píxel (test de `layout`).

### 5.2 Analizador ampliado

**Qué es.** La curva y el analizador ocupan toda la ventana del plugin, para trabajar el espectro con detalle. No es pantalla completa del sistema: dentro de un host no es fiable (FL Studio gestiona sus propias ventanas). Combinado con el tamaño L o con arrastrar la esquina, ocupa lo que el usuario quiera.

**Comportamiento**

- Botón de la barra superior o tecla F con el foco en la curva. Esc o el mismo botón vuelven.
- Se ocultan el panel de banda y el medidor de salida; la barra superior se queda. Las bandas se siguen editando en la curva (arrastre, rueda, menú contextual), y la etiqueta flotante del nodo seleccionado muestra también su tipo, su canal y su dinámica.
- **Ajustes del analizador**, en un menú junto al selector de modo, disponibles también sin ampliar:

| Ajuste | Valores | Por defecto |
|---|---|---|
| Resolución | FFT de 4096, 8192 o 16384 puntos | 4096 (la de ahora) |
| Caída | Constante de tiempo de bajada: lenta (1 s), media (0,35 s) o rápida (0,12 s); la subida sigue en 0,06 s | Media (la de ahora) |
| Inclinación | 0, 3, 4,5 o 6 dB/oct | 4,5 (la de ahora) |
| Mantener picos | Apagado o encendido (línea fina con el máximo; se borra con clic en la escala) | Apagado |
| Congelar | Detiene la actualización del analizador | Apagado |

  Los valores por defecto son los que hay hoy (`ui/src/CurveView.cpp` y `SpectrumBallistics`), para que nada cambie al actualizar. Con 16384 puntos la FFT se calcula a menos fotogramas por segundo si hace falta para no pasar del presupuesto de pintado.

**Estado:** `ui.analyzer_fft`, `ui.analyzer_fall`, `ui.analyzer_tilt` y `ui.analyzer_peaks`. Ni la ampliación ni la congelación se guardan: un proyecto siempre abre en la vista normal y en marcha.

**Aceptación**

- En `layout`, la vista ampliada es la ventana menos la barra superior, en los tres tamaños y en el mínimo.
- Al volver, la disposición es idéntica a la de antes.
- El coste de pintar en la vista ampliada a 2400 × 1400 no pasa de 4 ms por cuadro en el portátil de referencia.

### 5.3 Bandas desde el espectro

**Qué es.** Crear una banda señalando un pico del analizador y arrastrándolo: la forma más rápida de atacar una resonancia que se ve.

**Comportamiento**

- Con el analizador encendido y el ratón sobre la curva, lejos de cualquier nodo, se marca el pico del espectro más cercano en ±1/6 de octava. El marcador lleva su frecuencia, su nota (5.1) y su nivel.
- Al arrastrar desde el marcador se crea una campana en la frecuencia del pico:
  - La ganancia sigue al arrastre vertical; hacia abajo, recorte.
  - El Q sale del ancho del pico a −3 dB de su máximo, de 0,5 a 30.
  - Al soltar queda una banda normal, seleccionada.
  - Todo el gesto es una sola acción de deshacer.
- Un clic sin arrastre no crea nada, para que no aparezcan bandas por accidente. Hoy un clic en un hueco solo deselecciona, y lo sigue haciendo. El doble clic sigue creando una banda en el cursor, como ahora.
- Con Alt al soltar, la banda se crea dinámica, con el umbral 3 dB por debajo del nivel del pico. Es el atajo para controlar resonancias.
- Si las 24 bandas están en uso no se marca ningún pico, y el cursor lo indica con "No free bands".
- Se usa el espectro post, o el pre si el modo es pre + post. Con el analizador congelado (5.2) también funciona, y así se pueden escoger picos con calma.

**Detección** (`ui/logic`, sin JUCE): `findPeaks(niveles, frecuencias, opciones)` sobre los niveles ya suavizados que se pintan:

- máximo local con una prominencia de al menos 6 dB sobre el mínimo de su entorno de ±1/3 de octava;
- por encima de −60 dB;
- frecuencia refinada por interpolación parabólica entre puntos vecinos.

**Aceptación**

- Un seno en f da un pico en f ±1/48 de octava.
- Dos senos separados media octava dan dos picos.
- El rizado del ruido rosa por debajo de la prominencia no da ninguno.
- El Q estimado de una resonancia sintética de Q conocido queda dentro de un ±25 %.
- Un test de interfaz comprueba que crear una banda y deshacerla deja el estado exactamente como estaba.

### 5.4 Tiempos automáticos en la dinámica

**Hecha** (noviembre de 2026): [ADR 0016](docs/decisiones/0016-tiempos-automaticos-en-la-dinamica.md) y [docs/dsp/dinamica.md](docs/dsp/dinamica.md). Cada criterio de aceptación tiene su test (`tests/dsp/dynamics_test.cpp` y `tests/plugin/auto_timing_test.cpp`). Lo que cambió respecto a lo previsto, con sus medidas en la ADR: la relajación lenta no es un segundo detector de pico en paralelo (con el mismo ataque estaría siempre por encima del rápido y la rápida no haría nada), sino un filtro de un polo que promedia la envolvente rápida, y manda el mayor de los dos; el test del rizado se comprueba con el ataque fijo mínimo y 20 ms de relajación (con 100 ms el rizado ya es pequeño); y el interruptor va en el preset, a diferencia de `auto_gain`. El umbral automático sigue pendiente (sección 4.2). Medido: rizado de 0,15 dB a 80 Hz, reducción de ráfagas de 7 kHz en 2,5 ms, recuperación de 23 a 71 ms, y ningún coste extra de CPU.

**Qué es.** Que una banda dinámica elija sola su ataque y su relajación, según su frecuencia y la señal, para que funcione bien sin ajustar tiempos. Con ataques fijos, en graves la ganancia sigue la forma de onda y distorsiona; en agudos, un ataque lento deja pasar las sibilantes.

**Parámetro:** `bNN_autotime` ("Band N Auto Timing"), un interruptor por banda, apagado por defecto. Se añade al final de la tabla. Encendido, los potenciómetros de ataque y relajación se atenúan y muestran "Auto". Sus valores se conservan, y al apagar vuelven a mandar.

**Algoritmo** (a cerrar en una ADR, partiendo de esto):

- **Ataque ligado al periodo:** ataque = 2 periodos de la frecuencia de la banda, limitado a 0,5–50 ms. A 100 Hz da 20 ms; a 1 kHz, 2 ms; a 5 kHz, 0,5 ms.
- **Relajación de dos tiempos** (*program dependent*): una envolvente rápida (8 periodos, 20 ms como mínimo) y otra lenta (10 veces la rápida, 1 s como máximo). Se aplica la reducción mayor de las dos. Un transitorio aislado se recupera deprisa; una señal sostenida no bombea. Parte del detector de pico suavizado de Giannoulis y otros (sección 9), con dos constantes de relajación en paralelo.
- La frecuencia que manda es la de la banda, incluso si su ganancia cambia por la dinámica.
- Si la frecuencia cambia (automatización), los tiempos se recalculan por bloque, sin asignar memoria.

**Aceptación**

- Con un tono sostenido de 80 Hz por encima del umbral, el rizado de la ganancia dinámica queda por debajo de 0,5 dB. Con el ataque fijo mínimo, el test debe fallar, para comprobar que mide lo que debe.
- Con ráfagas de 7 kHz, la ganancia llega al 90 % de la reducción objetivo en menos de 5 ms.
- Tras una ráfaga aislada de 50 ms, la ganancia se recupera al 90 % en menos de 150 ms.
- Con el interruptor apagado, la salida es idéntica bit a bit a la de ahora.
- Siguen valiendo los tests de chasquidos y de tamaños de bloque de `docs/dsp/dinamica.md`.
- El coste extra con 24 bandas dinámicas en estéreo no pasa del 1 % de un núcleo.

El umbral automático de la sección 4.2 sigue pendiente aparte; se puede diseñar en la misma ADR.

### 5.5 Compensación automática de ganancia

**Hecha** (noviembre de 2026): [ADR 0015](docs/decisiones/0015-compensacion-automatica-de-ganancia.md) y [docs/dsp/compensacion-de-ganancia.md](docs/dsp/compensacion-de-ganancia.md). Cada criterio de aceptación tiene su test (`tests/dsp/auto_gain_test.cpp` y `tests/plugin/auto_gain_test.cpp`). Lo que cambió respecto a lo previsto, con sus medidas en la ADR: la integral usa una rejilla refinada alrededor de cada banda (una uniforme se equivocaba hasta en 3,8 dB con picos estrechos); el "hilo de control" es el de diseño de la fase lineal generalizado (`core::ControlThread`), con cálculo en el momento al renderizar; y el selector del tamaño de la ventana (S, M, L) pasó al menú de opciones para que el botón quepa en la barra.

**Qué es.** Mantener la sonoridad igual al ecualizar, para comparar con el bypass sin que "más fuerte" parezca "mejor".

**Parámetro:** `auto_gain` ("Auto Gain"), interruptor global, apagado por defecto, al final de la tabla. Actúa además de `output_gain`, que no cambia.

**Cálculo:** se hace al cambiar los parámetros, a partir de la curva estática analítica (la misma que se pinta), y no midiendo el audio, que bombearía con la música:

```
G = −10 · log10( Σ w(f) · |H(f)|²  /  Σ w(f) )      de 20 Hz a 20 kHz, en puntos logarítmicos
w(f) = densidad de ruido rosa × |K(f)|²             (ponderación K de ITU-R BS.1770)
```

- Solo cuenta la ganancia estática. La parte dinámica no se compensa: compensarla anularía su efecto.
- Con bandas en L, R, M o S se promedia la potencia de los dos canales de salida.
- La compensación se limita a ±12 dB.
- El hilo de control calcula la ganancia y la publica como un atómico. El hilo de audio la aplica con la rampa de ganancia que ya existe (`dsp/gain_ramp.h`), igual en los cuatro modos de fase.
- La barra superior muestra la compensación actual, por ejemplo "Auto −2,3 dB".

**Aceptación**

- Con la curva plana la compensación es exactamente 0 y la salida es bit a bit la de `auto_gain` apagado.
- Una campana de +6 dB, Q 1 a 1 kHz da el valor que sale de la integral numérica, con un error de ±0,05 dB.
- Ruido rosa por una curva cualquiera, con la compensación encendida, mide la misma sonoridad integrada (BS.1770) que sin EQ, con un margen de ±0,5 dB. Se prueba con al menos cinco curvas típicas.
- Al encender y apagar no hay chasquidos (test de la rampa).

### 5.6 Igualar a una referencia

**Qué es.** Medir el espectro medio de la pista y el de una referencia, y crear las bandas que acercan una a otra.

**Referencias:**

- otra instancia de esceQ, a través de la sesión de espectros que ya usan las colisiones ([ADR 0011](docs/decisiones/0011-espectros-compartidos.md)), con las mismas limitaciones: mismo proceso y mismo formato;
- la entrada de sidechain.

Cargar un archivo de audio queda fuera por ahora: exige decodificar y elegir formatos.

**Flujo**

1. Botón *Match* en la barra superior: abre un panel sobre la curva y elige la referencia.
2. *Learn* acumula, mientras suena, el espectro medio en potencia de la pista y de la referencia desde que se pulsa (sin olvido), y solo los bloques con presencia (los mismos umbrales que las colisiones). Hacen falta 5 s de señal útil; antes, *Apply* está desactivado y un indicador muestra el progreso.
3. Se dibujan los dos espectros y la curva objetivo. Ajustes:

| Ajuste | Valores | Por defecto |
|---|---|---|
| Cantidad | 0 a 100 % | 50 % |
| Suavizado | 1/12, 1/6, 1/3 o 1 octava | 1/3 |
| Bandas máximas | 1 a las que estén libres | 8 |
| Margen | Frecuencias mínima y máxima | 30 Hz a 16 kHz |

4. *Apply* crea las bandas en una sola acción de deshacer.

**El resultado son bandas normales** (campanas y shelves), no un filtro oculto. Así se pueden editar, automatizar y guardar en presets, y funcionan en todos los modos de fase y con la dinámica. No hacen falta parámetros nuevos, y el contrato de sonido no cambia.

**Ajuste** (`core`, en un hilo propio como el diseño de la fase lineal; se puede cancelar y nunca bloquea la interfaz):

1. Objetivo: T(f) = cantidad · (referencia − pista), suavizado y sin su media (no se iguala el nivel, solo el balance), limitado a ±12 dB y a cero fuera del margen.
2. Inicio voraz: una campana en el punto de mayor error, con el Q del ancho de ese lóbulo. Se repite hasta agotar las bandas, y se usa un shelf si el error de un extremo es monótono.
3. Refino conjunto de frecuencia, ganancia y Q por mínimos cuadrados (Levenberg-Marquardt) en frecuencia logarítmica, con las curvas analíticas de `dsp`.
4. Se para cuando el error RMS baja de 0,5 dB, o al agotar las bandas.

**Aceptación**

- Un objetivo generado con 3 campanas conocidas se recupera con un error RMS menor de 0,5 dB y como mucho 6 bandas.
- Un objetivo plano no crea bandas.
- Con la cantidad al 50 % las ganancias son la mitad que al 100 %, con un margen de ±0,2 dB.
- El resultado es determinista: la misma entrada da las mismas bandas.
- Con un objetivo de 24 bandas el ajuste termina en menos de 2 s.

### 5.7 Dinámica espectral

**Qué es.** Una variante de la banda dinámica que, dentro de su zona, reduce o realza **solo las frecuencias que pasan del umbral**, y no toda la banda. Controla resonancias que se mueven sin apagar el resto de esa zona. Es uno de los diferenciadores citados en la sección 8.

**Parámetro:** `bNN_spectral` ("Band N Spectral"), un interruptor por banda, apagado por defecto, al final de la tabla. Solo actúa si la banda es dinámica. Usa sus mismos umbral, rango, rodilla, ataque, relajación, sidechain y canal: no hay controles nuevos.

**Algoritmo** (ADR propia antes de empezar):

- STFT en el canal de la banda (L, R, M o S):
  - FFT de 2048 puntos a 48 kHz, escalada con la frecuencia de muestreo;
  - solapamiento del 75 % y ventana raíz de Hann en análisis y síntesis, para una reconstrucción perfecta.
- Por cada bin:
  - nivel del detector (la entrada, o el sidechain con *External*);
  - envolvente con el ataque y la relajación de la banda, expresados en tramas;
  - la curva de ganancia de la sección 4.2.
- La ganancia de cada bin se pondera con la forma de la banda (1 en su centro, 0 fuera) y se suaviza en frecuencia a 1/6 de octava, para evitar el "ruido musical".
- **Máscaras:** las bandas espectrales de un mismo canal comparten una STFT y suman sus máscaras en dB. La parte estática de la banda sigue en el ecualizador normal (IIR o FIR). La máscara se aplica después.

**Latencia:** una trama, 2048 muestras, 42,7 ms a 48 kHz. Solo existe mientras haya al menos una banda espectral encendida, y en fase lineal se suma a la del FIR. Activar o desactivar cambia la latencia: se gestiona como el cambio de modo de fase, silenciando un momento mientras el host recalcula la compensación. Queda abierto en la ADR si conviene reservar la latencia siempre que el usuario lo elija, para automatizar sin cortes.

**Aceptación**

- Un tono dentro de la banda por encima del umbral baja lo que marca el rango, con un margen de ±0,5 dB. Otro tono en la misma banda, por debajo del umbral, no cambia más de 0,2 dB: es justo lo que la banda dinámica normal no consigue.
- Con la señal por debajo del umbral, la salida es la entrada retrasada, con un error menor de −90 dB.
- Con el interruptor apagado en todas las bandas, la salida es bit a bit la de ahora, con la latencia de ahora.
- Con bloques de 1 a 4096 muestras la salida es idéntica bit a bit.
- La latencia comunicada coincide con la medida.
- Coste con cuatro canales con bandas espectrales a 48 kHz: menos del 10 % de un núcleo. Casi todo es FFT, así que el coste crece con los canales y no con las bandas.

### 5.8 Saturación

**Qué es.** Una etapa global de saturación suave tras el ecualizador, para dar color armónico. Va la última en prioridad: no es ecualización.

**Parámetros**, al final de la tabla:

| Parámetro | Valores | Por defecto |
|---|---|---|
| `sat_mode` | Off, Soft (simétrica: armónicos impares) o Warm (asimétrica: añade pares) | Off |
| `sat_drive` | 0 a 24 dB | 6 dB |
| `sat_mix` | 0 a 100 % | 100 % |

Con Off la etapa no se ejecuta.

**Algoritmo**

- Soft: curva `tanh`. Warm: `tanh` con polarización, seguida de un bloqueador de continua.
- Contra el aliasing, sobremuestreo ×4 con filtros de media banda:
  - polifásicos IIR en modo IIR, sin latencia;
  - FIR de fase lineal en los modos lineales, con su latencia sumada.
- La ganancia se compensa para que el drive no cambie el nivel de un seno de −18 dBFS a 1 kHz, con un margen de ±0,5 dB.
- La alternativa al sobremuestreo, antialiasing por antiderivada (ADAA) de primer orden, se mide antes de elegir.

**Aceptación**

- Con Off, la salida es bit a bit la de ahora.
- Con Soft, los armónicos pares quedan por debajo de −100 dB; con Warm, el segundo armónico es visible.
- Con un seno de 10 kHz, drive de 12 dB y 48 kHz, el aliasing queda 80 dB por debajo de la fundamental.
- La continua a la salida queda por debajo de −100 dB.
- La latencia comunicada coincide con la medida en los cuatro modos de fase.

---

## 6. Herramientas, pruebas y distribución

| Área | Elección |
|---|---|
| Lenguaje | C++20 |
| Build | CMake (≥ 3.24); JUCE y dependencias fijadas por versión con CPM o submódulos |
| Tests | Catch2: respuesta en frecuencia contra la fórmula analítica, estabilidad bajo modulación, tests de nulo, bloques de tamaño 1 a 8192 |
| Validación de plugin | `pluginval` (nivel de exigencia 10) y `clap-validator` |
| Seguridad en tiempo real | RealtimeSanitizer de Clang en CI |
| CI | GitHub Actions con matriz Windows / macOS / Linux; artefactos por commit |
| Formato de código | clang-format y clang-tidy |
| Instalador Windows | Inno Setup; VST3 en `Common Files\VST3`, CLAP en `Common Files\CLAP` |
| macOS | Firmar y notarizar exige cuenta de desarrollador de Apple (99 USD/año). Sin ella el usuario tiene que saltarse Gatekeeper a mano |
| Windows | Sin certificado de firma aparecerá el aviso de SmartScreen en el instalador; el plugin en sí carga igual |

Flujo de trabajo en git: una sola rama, `main`, con commits pequeños. CI en cada push.

---

## 7. Hoja de ruta

Estimaciones para una persona a tiempo parcial. Son orientativas.

| Fase | Contenido | Criterio de salida | Duración |
|---|---|---|---|
| **0. Base** ✅ | Estructura del repo, CMake, JUCE, CI, plugin vacío que pasa audio; `docs/arquitectura.md`, primeros ADR, `CONTRIBUTING.md` | Carga en FL Studio y pasa `pluginval` | 1 a 2 semanas |
| **1. Núcleo de filtros** ✅ | SVF, todos los tipos y pendientes, cálculo de la curva, tests | Respuesta medida = respuesta teórica (±0,1 dB) | 2 a 3 semanas |
| **2. EQ estático** ✅ | 24 bandas (adelantadas de la fase 4, ver ADR 0005), parámetros, estado, interfaz con curva y nodos arrastrables, analizador | Se puede mezclar una pista real con él | 4 a 6 semanas |
| **3. Dinámica** ✅ | Detector, envolvente, curva de ganancia, indicación visual de la ganancia dinámica, sidechain externo (ver ADR 0006) | Sin chasquidos bajo modulación rápida; de-esser y control de resonancias usables | 3 a 4 semanas |
| **4. Primera versión pública (0.1)** ✅ | M/S y L/R por banda, interfaz con controles propios, presets, deshacer, CLAP, instalador de Windows y releases desde el CI | Release en GitHub con binarios | 3 a 4 semanas |
| **5. Calidad** | Corrección en agudos (hecha, ver [ADR 0013](docs/decisiones/0013-correccion-en-agudos.md)), fase lineal (hecha, ver [ADR 0012](docs/decisiones/0012-modos-de-fase.md)), compensación automática de ganancia (hecha, ver [ADR 0015](docs/decisiones/0015-compensacion-automatica-de-ganancia.md)), tiempos automáticos en la dinámica (hechos, ver [ADR 0016](docs/decisiones/0016-tiempos-automaticos-en-la-dinamica.md)), escala de notas (hecha, ver [ADR 0017](docs/decisiones/0017-escala-de-notas.md)), analizador ampliado (5.2), bandas desde el espectro (5.3), optimización SIMD, macOS y Linux | Versión 0.5 | 8 a 12 semanas |
| **6. Avanzado** | Dinámica espectral (5.7), igualar a una referencia (5.6), detección de colisiones entre instancias (hecha, ver [ADR 0011](docs/decisiones/0011-espectros-compartidos.md)), saturación (5.8), surround | Versión 1.0 | Abierto |

---

## 8. Riesgos

| Riesgo | Mitigación |
|---|---|
| La interfaz consume más tiempo que el DSP | Empezar con una interfaz funcional y fea; pulir al final de cada fase, no antes |
| Artefactos al modular filtros | SVF desde el principio y tests específicos de modulación en la fase 1 |
| Fase lineal + dinámica resulta inviable con buena calidad | Resuelto en la fase 5: la dinámica se aplica como diferencia exacta sobre el FIR estático ([ADR 0012](docs/decisiones/0012-modos-de-fase.md)) |
| Duplicar un proyecto que ya existe | Elegir un diferenciador antes de la fase 2: núcleo DSP reutilizable, flujo de trabajo en FL Studio, o funciones poco cubiertas (fase lineal con dinámica, dinámica espectral) |
| Un cambio rompe proyectos guardados o altera el sonido | Contratos y tests de la sección 2.1, verificados en CI |
| Romper proyectos de usuarios al cambiar parámetros | Congelar identificadores y versionar el estado guardado desde la 0.1 |
| Problemas de marca | Nombre e identidad visual propios; ninguna referencia a marcas ajenas en el producto |

---

## 9. Referencias

**Formatos y frameworks**
- VST 3 SDK, licencia MIT desde la 3.8: https://steinbergmedia.github.io/vst3_dev_portal/pages/VST+3+Licensing/VST3+License
- CLAP: https://github.com/free-audio/clap y clap-wrapper: https://github.com/free-audio/clap-wrapper
- JUCE: https://juce.com — clap-juce-extensions: https://github.com/free-audio/clap-juce-extensions
- DPF: https://github.com/DISTRHO/DPF — iPlug2: https://github.com/iPlug2/iPlug2 — nih-plug: https://github.com/robbert-vdh/nih-plug
- Formatos soportados por FL Studio: https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins_supported.htm

**Proyectos de referencia**
- ZL Equalizer (AGPLv3): https://github.com/ZL-Audio/ZLEqualizer
- pluginval: https://github.com/Tracktion/pluginval
- Plantilla JUCE + CMake + CI (Pamplejuce): https://github.com/sudara/pamplejuce

**DSP**
- V. Zavalishin, *The Art of VA Filter Design*.
- A. Simper (Cytomic), notas técnicas sobre el SVF trapezoidal: https://cytomic.com/technical-papers/
- M. Vicanek, *Matched Second Order Digital Filters*: https://www.vicanek.de/articles.htm
- R. Bristow-Johnson, *Audio EQ Cookbook*: https://www.w3.org/TR/audio-eq-cookbook/
- D. Giannoulis, M. Massberg, J. Reiss, *Digital Dynamic Range Compressor Design* (JAES, 2012).
- Awesome Audio DSP: https://github.com/BillyDM/awesome-audio-dsp
