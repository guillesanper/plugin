# esceQ: plan del proyecto

Plugin de ecualización paramétrica y dinámica, libre, pensado para funcionar bien en FL Studio y con un flujo de trabajo comparable al de los ecualizadores comerciales de referencia (FabFilter Pro-Q).

Estado: **fases 0 a 5 completadas; falta publicar la 0.5.** La 0.1.0 está cerrada en `CHANGELOG.md` (2026-11-12) y el CI la compila, valida y empaqueta en Windows, macOS y Linux, pero aún falta subir la etiqueta `v0.1.0`, que publica la release en GitHub. De la fase 5 están hechas la fase lineal, la corrección en agudos (algoritmo Matched, con el estado en la versión 3: sección 4.1) y, adelantada de la fase 6, la detección de colisiones entre instancias. También están hechas la compensación automática de ganancia (sección 5.5, [ADR 0015](docs/decisiones/0015-compensacion-automatica-de-ganancia.md)), los tiempos automáticos en la dinámica (sección 5.4, [ADR 0016](docs/decisiones/0016-tiempos-automaticos-en-la-dinamica.md)), la escala de notas (sección 5.1, [ADR 0017](docs/decisiones/0017-escala-de-notas.md)), el analizador ampliado (sección 5.2, [ADR 0018](docs/decisiones/0018-analizador-ampliado.md)) y las bandas desde el espectro (sección 5.3, [ADR 0019](docs/decisiones/0019-bandas-desde-el-espectro.md)). La optimización SIMD se ha medido y no compensa, así que no se ha hecho ([ADR 0020](docs/decisiones/0020-sin-simd.md)). macOS y Linux están completos ([ADR 0022](docs/decisiones/0022-compatibilidad-en-macos-y-linux.md)): Linux se compila en Ubuntu 22.04 y carga desde Ubuntu 22.04 y Debian 12, y los tres formatos de macOS se comprueban (universales, macOS 11, firma ad hoc) en el CI; el CI está estable en los tres sistemas ([ADR 0021](docs/decisiones/0021-aviso-de-latencia-sin-temporizador.md)). Falta probarlos en un Mac y en un Linux con hosts reales, y publicar la 0.5 cuando se decida. La fase 6 está en marcha (sección 7): igualar a una referencia (5.6, [ADR 0023](docs/decisiones/0023-igualar-a-una-referencia.md), aceptada, bloques 6.1 a 6.3), dinámica espectral (5.7, [ADR 0024](docs/decisiones/0024-dinamica-espectral.md), aceptada, 6.4 a 6.6) y saturación (5.8, 6.7 a 6.9). La igualación está completa: hechos el 6.1, el ajuste en `dsp` ([docs/dsp/igualacion.md](docs/dsp/igualacion.md)), el 6.2, aprender y aplicar sin ventana (`core/match.h` y `ui/logic/match.h`), y el 6.3, la interfaz (el panel *Match*, [docs/ui.md](docs/ui.md)), con un defecto del ajuste pendiente (sección 5.6: una banda estrecha que ya hay, como un notch, se "rellena"). De la dinámica espectral están hechos el 6.4, el motor en `dsp` (`dsp/spectral_dynamics.h`, [docs/dsp/dinamica-espectral.md](docs/dsp/dinamica-espectral.md), con la ADR 0024 aceptada), y el 6.5, su integración: el parámetro `bNN_spectral` (388 parámetros) y la dinámica espectral en `dsp::PhaseEqualizer`, con el cálculo de cada trama repartido en el salto siguiente y la latencia (53,3 ms a 48 kHz) ligada al interruptor ([ADR 0025](docs/decisiones/0025-dinamica-espectral-en-el-procesador.md)). Ya suena en el plugin, pero aún sin interfaz: solo se puede encender desde la automatización del host. El siguiente es el 6.6, la interfaz. Sigue pendiente comprobar en FL Studio, con una mezcla real, el enrutado del sidechain.

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
| 3 | macOS 11 o posterior (arm64 + x64) | VST3, AU, CLAP | FL Studio en macOS carga los tres |
| 4 | Linux x64 (glibc 2.35 o posterior) | VST3, CLAP (LV2, aplazado: [ADR 0022](docs/decisiones/0022-compatibilidad-en-macos-y-linux.md)) | FL no corre nativo, pero amplía la comunidad (Bitwig, Reaper, Ardour) |

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

**Medido (fase 5).** Una mezcla típica de 8 bandas cuesta del 0,55 al 0,95 % de un núcleo a 44,1 y 48 kHz y del 2,4 al 3,9 % a 192 kHz, en los cuatro modos; el tamaño de bloque del host apenas influye. Lo caro son las cascadas IIR largas (24 cortes de 96 dB/oct: 6 % a 48 kHz, 22 % a 192 kHz), limitadas por la latencia de la recurrencia, y las 24 bandas dinámicas (5 % y de 15 a 22 %), que se van en `log10`, la curva de ganancia y el rediseño por muestra. SSE2 ahorra un 18–25 % en las cascadas estáticas y nada en la dinámica, y en arm64 no daría el mismo resultado que en x86-64: **no se usa SIMD** ([ADR 0020](docs/decisiones/0020-sin-simd.md)). Se revisará con el sobremuestreo (sección 5.8, bloque 6.8), con `esceq_bench`. La dinámica espectral (bloques 6.4 y 6.5) no lo necesita: sus bucles por bin están escritos sin ramas para que el compilador los vectorice, y cuesta del 3,9 al 4,2 % con 24 bandas a 48 kHz (GCC; 3,1 % con Clang, que vectoriza todos), y del 3,4 al 3,9 % con el ecualizador; su cálculo se reparte en el salto siguiente, para que ninguna llamada haga una trama entera ([ADR 0025](docs/decisiones/0025-dinamica-espectral-en-el-procesador.md)). `esceq_bench` (`tools/bench`) repite las medidas.

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
| Analizador ampliado ✅ | 5.2 | 5 | Ninguno (preferencias) | No |
| Bandas desde el espectro ✅ | 5.3 | 5 | Ninguno | No |
| Tiempos automáticos en la dinámica ✅ | 5.4 | 5 | 24 (uno por banda) | No |
| Compensación automática de ganancia ✅ | 5.5 | 5 | 1 global | No |
| Igualar a una referencia (planificada) | 5.6 | 6 | Ninguno (crea bandas normales; nada nuevo en el estado) | No |
| Dinámica espectral (en marcha: falta la interfaz) | 5.7 | 6 | 24 (uno por banda, en los presets) | Sí, mientras alguna banda tenga el interruptor: 2560 muestras a 48 kHz (53,3 ms) |
| Saturación (planificada) | 5.8 | 6 | 3 globales (en los presets) | Solo en fase lineal y fuera de Off (los filtros de sobremuestreo) |

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

**Hecho** (noviembre de 2026): [ADR 0018](docs/decisiones/0018-analizador-ampliado.md). Cada criterio de aceptación tiene su test (`tests/ui/layout_test.cpp`, `tests/ui/options_menu_test.cpp`, `tests/ui/meter_logic_test.cpp`, `tests/ui/curve_mapping_test.cpp` y `tests/core/state_test.cpp`), salvo el de coste, que se ha medido. Lo que cambió respecto a lo previsto, con sus motivos y medidas en la ADR: los ajustes del analizador van en el menú de opciones (sección *Analyzer*) y no en un menú propio junto al selector, porque en la barra solo cabía un botón, el de ampliar, y eso estrechando el rango (de 92 a 72 px) y el bypass (de 76 a 64) sin tocar la ventana mínima; la etiqueta con el canal y la dinámica aparece al señalar o arrastrar un nodo, no fija sobre el seleccionado, que tapaba los nodos vecinos; los picos se mantienen solo del espectro post; y con 16384 puntos no hace falta bajar el ritmo (0,36 ms por espectro). Las cuatro preferencias solo se escriben si no son las de por defecto, así que un proyecto que no las usa se guarda igual que antes. El coste de pintar la vista ampliada a 2400 × 1400 se ha medido con el renderizador por software de JUCE en Linux (20,6–21,3 ms, frente a 17,6–21,3 de la vista normal); falta medirlo en el portátil de referencia, que es donde se fija el límite de 4 ms.

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

**Hechas** (noviembre de 2026): [ADR 0019](docs/decisiones/0019-bandas-desde-el-espectro.md), con el algoritmo y sus medidas en [docs/dsp/picos.md](docs/dsp/picos.md). Cada criterio de aceptación tiene su test (`tests/ui/spectrum_peaks_test.cpp`, con formas exactas y con el analizador real, y `tests/plugin/history_test.cpp` para el deshacer, que manda al procesador la misma secuencia de gestos que la curva). Lo que cambió respecto a lo previsto, con sus motivos y medidas en la ADR: los picos no se buscan en los niveles que se pintan sino en un promedio de su potencia de 1 s (`SpectrumAverage`), porque en lo que se pinta el ruido rosa da de 1,4 a 8 picos de azar por fotograma y en el promedio ninguno (0,09 con 16384 puntos, bajo 80 Hz); la detección quita la inclinación del analizador, así que el nivel del pico es el de la señal (y el umbral de Alt tiene sentido) y la meseta de un seno en agudos no se tuerce; con Alt, la ganancia arrastrada pasa a ser el rango de la dinámica y la estática vuelve a 0 dB (el plan solo fijaba el umbral); y el marcador lleva la nota siempre, también con el teclado oculto. El criterio de 1/48 de octava se cumple con el analizador desde 475 Hz con 4096 puntos (119 Hz con 16384); más abajo manda la resolución del analizador. El Q de una resonancia en ruido, medido por el analizador, queda en el ±25 % con Q de 6 a 12 desde 1 kHz y sale hasta un 30 % bajo con Q 16, por el ensanchamiento del analizador, que no se corrige porque empeora el conjunto.

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

**Hecho** (noviembre de 2026), con un defecto pendiente (al final de esta sección): decisiones en la [ADR 0023](docs/decisiones/0023-igualar-a-una-referencia.md), aceptada con las medidas del bloque 6.1. Es el primer trabajo de la fase 6, en tres bloques (al final de esta sección), los tres hechos: el 6.1, el ajuste en `dsp` ([docs/dsp/igualacion.md](docs/dsp/igualacion.md)); el 6.2, aprender y aplicar sin ventana (de extremo a extremo, la curva aplicada queda a 0,13 dB de la conocida), y el 6.3, la interfaz ([docs/ui.md](docs/ui.md), «Igualar a una referencia: el panel Match»). El 6.1 cambió el centrado del objetivo, lo de fuera del rango, la parada, el suavizado en graves, las opciones de suavizado y los segundos útiles (lo que sigue ya lo recoge). Decidido con el usuario: el panel se abre desde el menú del botón de comparar (la barra superior no tiene sitio), la pista se mide **antes** del ecualizador, entran las dos referencias (otra instancia y el sidechain) y los nombres son *Match*, *Learn* y *Apply*.

**Qué es.** Medir el espectro medio de la pista y el de una referencia, y crear las bandas que acercan una a otra: "que esta guitarra tenga el balance de aquella", o igualar el sonido de dos tomas. El resultado son bandas normales, editables, que se ven y se pueden deshacer.

**Referencias**

- **Otra instancia de esceQ**, a través de la sesión de espectros que ya usan las colisiones ([ADR 0011](docs/decisiones/0011-espectros-compartidos.md)), con sus mismas limitaciones: mismo proceso y mismo formato (una VST3 no ve a una CLAP). Se usa lo que esa instancia publica, su **salida**: la referencia tal como suena en la mezcla, con su propio ecualizador.
- **La entrada de sidechain**, si el host la tiene conectada. Sirve para igualar a una pista que no lleva esceQ, o a un bus.

Cargar un archivo de audio queda fuera: exige decodificar, elegir formatos y leer en otro hilo. Se puede añadir después como otra fuente del mismo aprendizaje.

**Qué se mide de la pista: su entrada, antes del ecualizador.** El aprendizaje no depende de las bandas que haya, ni de que se muevan mientras se aprende, y lo que se ajusta es lo que falta: la curva objetivo menos la curva estática actual. Así, aplicar dos veces seguidas no crea bandas la segunda vez, y las bandas que ya había se conservan.

#### Comportamiento

1. **Abrir.** En el menú del botón de comparar, la entrada *Match to reference…* (debajo de las instancias) abre el panel *Match*, una tarjeta sobre la esquina superior derecha de la curva, también en la vista ampliada (5.2). Se cierra con su botón × o con Esc (si el foco está en el panel). Cerrarlo, o cerrar la ventana, no borra lo aprendido: vive en el procesador mientras el plugin está abierto, como la selección de instancias. No se guarda con el proyecto.
2. **Elegir la referencia**, en un desplegable: las otras instancias de la sesión con su nombre (el de la pista o "esceQ N"), marcadas "silent" si no les llega audio; y *Sidechain*, desactivado con "(not connected)" si el host no lo conecta. La propia instancia no aparece. Por defecto, la primera instancia con señal. Cambiar de referencia borra lo aprendido.
3. ***Learn*** empieza a aprender desde cero y el botón pasa a *Stop*. Mientras suena, se acumula el espectro medio en potencia de la pista y de la referencia, sin olvido, y solo en los instantes en que **las dos** tienen señal (el mismo umbral que la sesión usa para "silent"). Un indicador muestra los segundos útiles ("3.2 s / 10 s"). Con el transporte parado no avanza. Se sigue aprendiendo hasta *Stop*: más tiempo, medida más estable. Cerrar el panel o la ventana del plugin equivale a *Stop* (la pista se mide en la interfaz), y lo aprendido se conserva.
   - Si la instancia de referencia desaparece (se borra su pista), el aprendizaje se para y se conserva lo aprendido, con el aviso "Reference removed".
   - Si se desconecta el sidechain, igual: se para, con "Sidechain disconnected".
4. **Con 10 s útiles** (con 5, los graves de una medida de ruido fluctúan ±1,3 dB: [docs/dsp/igualacion.md](docs/dsp/igualacion.md)) se dibujan sobre la curva, en el panel abierto: el espectro medio de la pista y el de la referencia (este, desplazado para que las dos coincidan en la mediana de su diferencia, porque no se iguala el nivel), la **curva objetivo** (discontinua, más el nivel libre del ajuste) y la **curva propuesta**: la de las bandas actuales más las que se crearían, con sus nodos en fantasma. La propuesta se recalcula sola al cambiar un ajuste o una banda (150 ms después del último cambio), mientras el panel dice "Fitting…".
5. **Ajustes** del panel:

| Ajuste | Valores | Por defecto |
|---|---|---|
| *Amount* | 0 a 100 % | 50 % |
| *Smoothing* | 1/6, 1/3 o 1 octava (1/12 no es fiable: en la rejilla de la sesión es no suavizar) | 1/3 |
| *Max bands* | 1 a las bandas libres | 8 (o las libres, si son menos) |
| *Range* | Frecuencia mínima y máxima, de 20 Hz a 20 kHz; se escriben como el campo de frecuencia (también una nota, 5.1) | 30 Hz a 16 kHz |

   Viven con lo aprendido (en el procesador, sin guardarse). Una banda libre es una banda apagada.
6. ***Apply*** crea las bandas propuestas en las bandas libres, de la de menor número en adelante, en **una sola acción de deshacer**, y selecciona la primera. El panel sigue abierto, con una propuesta nueva (casi siempre vacía). Está desactivado, con el motivo en su ayuda, si: faltan segundos útiles ("Learn 10 s first"), no hay bandas libres ("No free bands"), se está calculando ("Fitting…") o la propuesta está vacía ("Nothing to match": ya se parecen).
7. Lo que crea son **campanas y shelves** (low y high shelf), en el canal Stereo, sin dinámica ni tiempos automáticos, con la pendiente por defecto. Todo lo demás de la banda reutilizada vuelve a su valor por defecto, como al crear una banda desde el espectro.

**Casos límite**

- Sin otras instancias ni sidechain conectado, el desplegable solo tiene *Sidechain (not connected)* y *Learn* está desactivado ("No reference").
- Las frecuencias en las que la pista o la referencia no tienen contenido (lo que las colisiones llaman "no presente": por debajo de −66 dB o a más de 30 dB de su máximo) no cuentan en el ajuste, aunque estén dentro del rango: no se intenta realzar 30 dB unos agudos que la pista no tiene.
- Modos de fase, algoritmo de filtro y bypass no afectan al aprendizaje (se mide la entrada). La propuesta se calcula con el algoritmo de filtro y la frecuencia de muestreo de la instancia, para que lo que se ve sea lo que suena.
- Las bandas en L, R, M o S cuentan en la curva actual con la misma regla que la compensación de ganancia ([ADR 0015](docs/decisiones/0015-compensacion-automatica-de-ganancia.md)): la media de la potencia de los dos canales. Solo cuenta su parte estática; las bandas dinámicas cuentan con su ganancia estática.
- Los espectros son mono (la mezcla de los canales, como la sesión): no se iguala la imagen estéreo.
- Con el analizador apagado se aprende igual: el panel tiene su propio cálculo.

#### Algoritmo

Referencias públicas: V. Välimäki y J. D. Reiss, *All About Audio Equalization: Solutions and Frontiers* (Applied Sciences, 2016), sobre el diseño de ecualizadores paramétricos por optimización a partir de una respuesta objetivo; K. Levenberg (1944) y D. Marquardt (1963) para el método de mínimos cuadrados no lineales, en la formulación de K. Madsen, H. B. Nielsen y O. Tingleff, *Methods for Non-Linear Least Squares Problems* (DTU, 2004). Detalle y medidas, en `docs/dsp/igualacion.md` (bloque 6.1).

**1. Aprendizaje** (`ui/logic`, en el hilo de mensajes, a 30 Hz):

- Los dos espectros se miden igual, para que sus sesgos se anulen al restarlos: `dsp::SpectrumAnalyzer` de 8192 puntos en la rejilla de la sesión (192 frecuencias logarítmicas de 20 Hz a 20 kHz) y su promedio exponencial de 0,4 s (`dsp::SpectrumAverager`). La referencia de otra instancia es el espectro que ya publica la sesión (`SpectrumSession::snapshot`, mientras el panel tiene un `Viewer`); la pista y el sidechain se calculan en el panel con esas mismas piezas, a partir de la cola "pre" que ya lee la curva y de una cola nueva de sidechain.
- Cada vez que la referencia publica un espectro nuevo (20 Hz) y las dos tienen señal, se suma la potencia de los dos promedios, punto a punto, ponderada por el tiempo transcurrido. Como el promedio exponencial es lineal en potencia, la media a largo plazo de lo que publica es la media de la señal: no hace falta otro cálculo en la sesión ni tocarla.
- La inclinación del analizador (4,5 dB/oct) está en los dos y se anula al restar. El nivel absoluto no importa (se centra en la mediana).
- Al ajuste le llegan también la resolución del análisis (la frecuencia de muestreo de cada espectro entre 8192; la de la pista y la de la referencia pueden no coincidir, y vale la mayor) y las bandas actuales, el algoritmo y la frecuencia de muestreo de la instancia.

**2. Objetivo** (`dsp`, puro; hecho en el bloque 6.1):

```
D(f) = suavizado(R)(f) − suavizado(P)(f)           R: referencia, P: pista; en dB, cada uno suavizado
                                                    en potencia (dsp::smoothOctaves) al ancho elegido,
                                                    con al menos 8 bins de la FFT por ventana en graves
w(f) = 1 dentro del rango y si las dos están presentes (dsp::presence ≥ 0,5, también a un ancho de
       ventana alrededor); 0 si no
T(f) = clamp( cantidad · (D(f) − mediana_w(D)), ±12 dB )   mediana ponderada por w en frecuencia logarítmica
N(f) = T(f) − C(f)                                  C: curva estática de las bandas actuales
```

- Centrado en la mediana, no en la media: se iguala lo que se aparta de lo más común, y una referencia con más graves y agudos da shelves, no un corte ancho de los medios.
- Fuera del rango, N es lo del borde, con un peso de 0,1: las bandas nuevas no crean nada ahí, pero un shelf puede seguir. El peso pasa de 0,1 a 1 en 1/6 de octava.
- El ajuste se evalúa en una rejilla de 384 puntos logarítmicos (el doble de densa que la de la sesión, interpolando en frecuencia logarítmica), para que una campana estrecha no caiga entre dos puntos.

**3. Inicio voraz** (`dsp`): con el residuo r = N,

- se toma el punto del rango de mayor |r|·w; si es menor de 0,5 dB, se para;
- su lóbulo es el tramo alrededor con el mismo signo y |r| por encima de la mitad del máximo. Si el lóbulo llega a un extremo del rango, es un **shelf** en el punto medio de su flanco, con la ganancia del extremo y Q 0,707; si no, una **campana** en el máximo, con la ganancia del máximo y el Q del ancho del lóbulo a media altura (la fórmula de `qForBandwidth`, ADR 0019, que se lleva a `dsp`);
- se resta su curva y se repite hasta agotar las bandas.

**4. Refino conjunto** (Levenberg-Marquardt): todas las bandas propuestas a la vez, en sus variables naturales (log2 de la frecuencia, ganancia en dB, log del Q), y un nivel c casi libre, minimizando Σ w(f)·(N(f) + c − Σ Hk(f))² más dos penalizaciones pequeñas, de c y de las ganancias, con las curvas analíticas de `dsp` (`designBand` y `magnitudeDbAtTangent`, con las tangentes de la rejilla calculadas una vez). El jacobiano es por diferencias centrales, y solo de la banda que se mueve (la curva es una suma), así que cada iteración cuesta unas 6·384 evaluaciones por banda. Límites: frecuencia dentro del rango, ganancia de ±18 dB, Q de 0,3 a 16 en campanas y de 0,5 a 1 en shelves (proyectados en cada paso). Se refina tras añadir cada banda (como mucho 20 iteraciones) y al final (100).

**5. Parada y limpieza**: se para cuando ningún punto del rango se aparta 0,5 dB (el RMS dejaba sin igualar detalles estrechos), al llegar a *Max bands* o cuando una banda nueva quita menos de 0,05 dB de error RMS (√(E²antes − E²después)). Tras el refino se quitan las bandas de menos de 0,3 dB y se vuelve a refinar. Las bandas se ordenan por frecuencia.

**Determinista**: sin azar, sin hilos dentro del ajuste, orden fijo y `double`; la misma entrada da exactamente las mismas bandas con el mismo binario (entre sistemas, `libm` puede cambiar el último bit).

#### Dónde vive cada parte y en qué hilo

| Pieza | Módulo | Hilo |
|---|---|---|
| Objetivo, pesos, inicio voraz, Levenberg-Marquardt (`dsp/curve_match.h`) | `dsp` (MIT, puro) | El del ajuste |
| Hilo del ajuste: recibe una petición (espectros, ajustes, bandas actuales, frecuencia de muestreo, algoritmo), calcula, se puede cancelar y publica la última propuesta (`core::MatchWorker`) | `core` | Uno propio por instancia, creado con la primera petición y parado con la instancia; no el hilo de control, que no puede retrasar segundos el diseño de un FIR ni la compensación de ganancia |
| Aprendizaje: acumulación en potencia, puerta de señal y segundos útiles (`core::MatchLearner`), y los ajustes (`core::MatchSettings`) | `core`, junto al hilo del ajuste (`core/match.h`): el procesador los guarda mientras está abierto, y `plugin` no puede depender de `ui/logic` | Mensajes |
| Estado del panel, motivos de *Apply* desactivado, textos y cambios de los parámetros que crean las bandas (`ui/logic/match.h`) | `ui/logic` | Mensajes |
| Lo aprendido, los ajustes y el hilo del ajuste mientras el plugin está abierto; la cola del sidechain | `plugin` | Mensajes; la cola la escribe el de audio |
| Entrada del menú, panel, dibujo de espectros, objetivo y propuesta | `ui/src` | Mensajes |

**Tiempo real.** El hilo de audio solo hace algo nuevo con el sidechain como referencia y aprendiendo: escribe la mezcla mono del sidechain en una `dsp::SpscFifo` (como las colas del analizador), detrás de un atómico que se lee una vez por bloque. Sin eso, nada cambia en `processBlock`. El ajuste nunca toca el hilo de audio, ni la interfaz lo espera: comprueba la cancelación en cada iteración (unos milisegundos).

#### Contratos (sección 2.1)

- **Parámetros:** ninguno nuevo (siguen 364). Las bandas creadas usan los de siempre, así que se automatizan, se guardan y van en los presets.
- **Estado:** no cambia (versión 3). Ni lo aprendido ni los ajustes del panel se guardan: los espectros no se pueden rehacer sin el audio, y los identificadores de instancia cambian en cada sesión ([ADR 0011](docs/decisiones/0011-espectros-compartidos.md)). No hay preferencias `ui.*` nuevas.
- **Sonido:** no cambia nada para una configuración dada; *Apply* solo cambia parámetros.
- **Presets y latencia:** no cambian.
- **Deshacer:** *Apply* pone todos los parámetros de una vez (`applyValues`) y registra la acción en el momento, como al cargar un preset: una acción, que deshace todas las bandas creadas.

#### Aceptación

| Criterio | Test |
|---|---|
| Un objetivo generado con 3 campanas conocidas (+4 dB Q 1 a 200 Hz, −3 dB Q 2 a 2 kHz, +5 dB Q 0,7 a 8 kHz) se recupera con un error RMS menor de 0,5 dB y como mucho 6 bandas; un low shelf de +6 dB a 100 Hz, con una banda y menos de 0,3 dB | `tests/dsp/curve_match_test.cpp` |
| Un objetivo plano, o con un rizado aleatorio de ±0,3 dB, no crea bandas | ídem |
| Con la cantidad al 50 %, la curva propuesta es la mitad de la del 100 %, con ±0,2 dB en cada punto del rango | ídem |
| Con las bandas actuales iguales a lo que hace falta, la propuesta está vacía; aplicar dos veces no crea bandas la segunda | ídem y `tests/ui/match_test.cpp` |
| Las frecuencias sin contenido en una de las dos no cuentan: una referencia sin nada por encima de 10 kHz no crea bandas ahí | `tests/dsp/curve_match_test.cpp` |
| Determinista: la misma entrada da bit a bit las mismas bandas | ídem |
| 24 bandas: menos de 2 s en el portátil de referencia (medido con un caso nuevo de `esceq_bench`); en el CI, un límite de evaluaciones de la curva, que no depende de la máquina | ídem y `tools/bench` |
| Una petición nueva cancela la anterior en la siguiente iteración, y parar el hilo durante un ajuste vuelve tras esa iteración | `tests/core/match_test.cpp` (sin esperas por tiempo: el test fuerza el trabajo, con un modo manual como el de `ControlThread`) |
| Aprendizaje: el silencio de cualquiera de las dos no suma segundos; *Apply* no se activa antes de 10 s útiles | `tests/core/match_test.cpp` y `tests/ui/match_test.cpp` |
| De extremo a extremo: ruido rosa en la pista y el mismo ruido por una curva conocida (tres campanas y un shelf) en la referencia, 10 s simulados, *Apply* al 100 % con 1/3 de octava: la curva resultante queda a ±1 dB de la conocida de 50 Hz a 12 kHz | `tests/plugin/match_test.cpp`, con dos procesadores en una sesión con hilo manual |
| *Apply* y deshacer dejan todos los parámetros exactamente como estaban; rehacer vuelve al resultado; es una sola acción | `tests/plugin/history_test.cpp` |
| Sin aprender con el sidechain, la salida de `processBlock` es bit a bit la de ahora, y el hilo de audio no asigna ni bloquea (RealtimeSanitizer) | `tests/plugin/` y el job `rtsan` |
| La tarjeta del panel cabe en la curva en los tres tamaños, en el mínimo y en la vista ampliada, sin tapar la barra | `tests/ui/layout_test.cpp` |

#### Bloques de implementación

Cada uno cabe en un chat, con sus tests, su documentación y el CI en verde.

1. **6.1 Ajuste** ✅ (`dsp/curve_match.h`, sin interfaz): objetivo, pesos, inicio voraz y Levenberg-Marquardt, `qForBandwidth` llevado a `dsp` (sin cambiar su resultado en `ui/logic`), los tests de `tests/dsp/curve_match_test.cpp`, el caso de `esceq_bench`, `docs/dsp/igualacion.md` con las medidas, y la ADR 0023 aceptada con lo que haya cambiado. Si las medidas piden otros límites (Q, iteraciones, umbrales), se cambian aquí y se dicen en la ADR. Hecho: 24 bandas en unos 0,7 s; lo que cambió está en la ADR, «Cambios al aceptarla».
2. **6.2 Aprender y aplicar, sin ventana** ✅: `core/match.h` (`MatchLearner`, `MatchSettings`, `MatchWorker`), `ui/logic/match.h` (aprendizaje, estado del panel, cambios de *Apply*), la cola del sidechain y lo aprendido en el procesador, *Apply* como una acción, y sus tests (`tests/core/match_test.cpp`, `tests/ui/match_test.cpp`, `tests/plugin/match_test.cpp`, `tests/plugin/history_test.cpp`); `docs/arquitectura.md` (módulos, hilos y comunicación). Hecho: con ruido rosa y el mismo ruido por tres campanas y un shelf, 10 s simulados y *Apply* al 100 % con 1/3 de octava, salen 4 bandas y la curva queda a 0,13 dB de la conocida de 50 Hz a 12 kHz, con las dos referencias ([docs/dsp/igualacion.md](docs/dsp/igualacion.md)). Lo nuevo respecto a lo planificado: la sesión publica la frecuencia de muestreo y un reloj de audio de cada instancia (lo aprendido se pondera por él, y con el transporte parado no avanza); mientras se aprende, la propuesta se vuelve a pedir cada segundo útil; `MatchReference` y la cola del sidechain (`MatchSidechainTap`) van también en `core/match.h`; y el *Apply* del procesador es `PluginProcessor::applyAction`. El audio de la pista lo da la interfaz al controlador (`ui::MatchController::pushTrack`), con lo que lee de la cola "pre", que sigue teniendo un solo lector.
3. **6.3 Interfaz** ✅: entrada del menú de comparar, el panel y su disposición en `ui/logic/layout`, el dibujo de los espectros, el objetivo y la propuesta, `docs/ui.md`, la tabla de uso del README y `CHANGELOG.md`. Hecho: *Match to reference…* bajo las instancias del menú de comparar; la tarjeta (`MatchPanel`, 280 × 240 px, `matchPanelLayout`) cabe en la curva en todos los tamaños, con teclado y en la vista ampliada (`tests/ui/layout_test.cpp`); los textos, el rango escrito como la frecuencia y lo que se dibuja, en `ui/logic/match.h` con sus tests; `tools/snapshot --match`. Lo que cambió respecto a lo planificado: la propuesta se dibuja como la curva total que se ve más las bandas nuevas, columna a columna (la `proposedDb` del ajuste usa la rejilla de 384 puntos y la regla de canales de la compensación, y no coincidía con la curva blanca: un notch estrecho salía casi plano, y una banda en M, distinta); los nodos en fantasma llevan el color de la banda libre que ocuparían; hay una leyenda; y la propuesta anterior se queda atenuada mientras se recalcula. La tarjeta tapa en S y M la zona de agudos por encima de unos 0 dB (filas a 4 px para tapar lo mínimo).

**Defecto pendiente, encontrado en el 6.3.** El objetivo T está suavizado al ancho elegido, pero la curva actual C no: N = T − C ve entera una banda estrecha que ya hay. Con la curva de `tools/snapshot` (un notch de Q 8 a 5,2 kHz), la propuesta es una campana de +17,7 dB y Q 16 a 5194 Hz que deshace el notch. Arreglarlo es cosa del ajuste (`dsp/curve_match.cpp`), no de la interfaz: lo natural es comparar con C suavizada al mismo ancho, pero hay que comprobar que aplicar dos veces sigue sin crear bandas (una campana estrecha propuesta, ya aplicada, se vería suavizada), medirlo y contarlo en la ADR 0023 («Cambios al aceptarla»). Mientras tanto, `--match` quita ese notch de la captura.

### 5.7 Dinámica espectral

**En marcha** (noviembre de 2026): decisiones en la [ADR 0024](docs/decisiones/0024-dinamica-espectral.md), aceptada con las medidas del bloque 6.4 (hecho: el motor, [docs/dsp/dinamica-espectral.md](docs/dsp/dinamica-espectral.md)), y en la [ADR 0025](docs/decisiones/0025-dinamica-espectral-en-el-procesador.md), del bloque 6.5 (hecho: la integración). Va después de la 5.6, en tres bloques (al final de esta sección). Decidido con el usuario: la latencia existe solo mientras haya una banda con el interruptor encendido (sin reservarla, y aunque esa banda no actúe), la STFT es de 2048 puntos a 48 kHz, sin selector, y el cálculo de cada trama se reparte en el salto siguiente, con un salto más de latencia (N + N/4: 53,3 ms a 48 kHz).

**Qué es.** Una variante de la banda dinámica que, dentro de su zona, reduce o realza **solo las frecuencias que pasan del umbral**, y no toda la banda. Controla resonancias que se mueven (una voz con armónicos que cambian, una guitarra acústica con notas que retumban) sin apagar el resto de esa zona. Es uno de los diferenciadores citados en la sección 8.

**Parámetro:** `bNN_spectral` ("Band N Spectral"), un interruptor por banda, apagado por defecto, al final de la tabla (índices 364 a 387; quedarían 388 parámetros). Va en los presets, como `bNN_autotime`: es parte del sonido de la banda. Solo actúa si la banda es dinámica (`bNN_dyn`) y en los tipos en que actúa la dinámica ([docs/dsp/dinamica.md](docs/dsp/dinamica.md)): campana, shelves y tilt (en tilt, en todo el espectro); en los demás no hace nada. Usa sus mismos umbral, rango, rodilla, ataque, relajación, tiempos automáticos, sidechain y canal: no hay controles nuevos.

#### Comportamiento

- En la tarjeta de dinámica del panel, un interruptor *Spectral* junto a *Auto* (tiempos automáticos); desactivado si la dinámica está apagada, pero con su estado a la vista (si está encendido, la latencia sigue: ADR 0025). Su ayuda dice la latencia que añade ("Spectral · adds 53.3 ms latency").
- Con la banda espectral, la **parte dinámica de toda la banda deja de actuar**: la hace la máscara, frecuencia a frecuencia. La parte estática (la ganancia de la banda) sigue igual, en el ecualizador normal.
- El medidor de dinámica del panel y la curva muestran la reducción **máxima** de la máscara dentro de la banda (la de la frecuencia que más baja), por los 24 atómicos que ya existen. La forma de la máscara se dibuja como un sombreado bajo la curva de la banda (192 puntos, a 30 Hz, por un `dsp::TripleBuffer`).
- Encender el interruptor de la primera banda, o apagar el de la última, cambia la latencia (apagar la banda o su dinámica, no): se gestiona como el cambio de modo de fase (fundido a silencio en 10 ms, cambio, silencio mientras se llena la latencia nueva, fundido de entrada), y el host recibe el aviso por el hilo de control ([ADR 0021](docs/decisiones/0021-aviso-de-latencia-sin-temporizador.md)). Encender o apagar otras bandas espectrales no cambia la latencia y solo hace el fundido de 10 ms de un cambio estructural. Automatizar el interruptor de la primera banda deja un hueco en cada cambio: lo dice su ayuda.
- Cargar un preset que enciende o apaga la dinámica espectral también cambia la latencia, con el mismo hueco.

#### Algoritmo

Referencias públicas: J. B. Allen, *Short Term Spectral Analysis, Synthesis, and Modification by Discrete Fourier Transform* (IEEE TASSP, 1977) y D. Griffin y J. Lim, *Signal Estimation from Modified Short-Time Fourier Transform* (IEEE TASSP, 1984), para la STFT con ventana raíz de Hann y su reconstrucción; O. Cappé, *Elimination of the Musical Noise Phenomenon with the Ephraim and Malah Noise Suppressor* (IEEE TSAP, 1994), sobre el ruido musical y el suavizado de las ganancias; y Giannoulis y otros (sección 9) para la envolvente y la curva de ganancia, que son las de la banda dinámica.

- **STFT** de N = 2048 puntos a 44,1 y 48 kHz (×2 a 88,2 y 96 kHz, ×4 a 176,4 y 192 kHz, para que la resolución en hercios y la latencia en milisegundos no cambien), salto de N/4 (75 % de solapamiento) y ventana raíz de Hann en el análisis y en la síntesis: la suma de los cuadrados solapados es constante y, sin modificar, la salida es la entrada retrasada.
- **Una STFT para todas las bandas**: por salto, una FFT compleja de la señal (L en la parte real y R en la imaginaria), una del detector (la entrada, o el sidechain con *External*; las dos si hay bandas de cada clase) y una inversa, sean cuantas sean las bandas espectrales y sus canales. M y S se obtienen de L y R en frecuencia (la FFT es lineal), sin más FFT. En mono cuesta lo mismo.
- **Detector por bin:** la energía del detector, con ventana de Hann, en 1/6 de octava alrededor del bin (3 bins como mínimo: es el suavizado en frecuencia contra el ruido musical), en el canal de la banda (Stereo: el máximo de L y R, detección enlazada como en la [ADR 0007](docs/decisiones/0007-canal-por-banda.md)), calibrada para que un seno lea su nivel de pico, como el detector de la banda dinámica. Envolvente por bin con el ataque y la relajación de la banda en tramas (un salto son 10,7 ms: los tiempos menores se redondean a un salto); con tiempos automáticos, los de la frecuencia de cada bin (ADR 0016), con ese mismo mínimo. Curva de ganancia de la sección 4.2 (umbral, rango, rodilla).
- **Máscara de la banda:** la ganancia de cada bin se pondera con la magnitud del filtro del detector de la banda (sección 4.2: paso banda en campanas, paso bajo o alto en shelves, 1 en tilt), normalizada a 1 en su máximo. No se suaviza la ganancia: diluía la reducción de un tono (−0,7 dB en vez de −6 a 5 kHz); el suavizado va en el nivel (arriba).
- **Varias bandas:** las máscaras del mismo dominio (L/R para Stereo, Left y Right; M/S para Mid y Side) se suman en dB y se aplican; si hay de los dos, primero L/R y luego M/S.
- **Dónde se aplica:** después del ecualizador (IIR o FIR, con la parte estática y la dinámica de las bandas no espectrales) y antes de la ganancia de salida. En fase lineal el detector se retrasa lo mismo que el FIR, como ya se hace con los detectores de la dinámica.
- **Cálculo repartido** (ADR 0025): cada trama se calcula en pasos (la FFT de la señal, cada fuente del detector, cada banda, cada máscara y la síntesis) durante el salto siguiente, tantos en cada llamada como la parte del salto que trae.
- **Latencia:** N + N/4 muestras (53,3 ms a 48 kHz, 58 a 44,1), sumada a la del modo de fase.

#### Dónde vive y en qué hilo

| Pieza | Módulo | Hilo |
|---|---|---|
| STFT, detector, envolventes, máscaras y su aplicación (`dsp/spectral_dynamics.h`), con la memoria reservada al preparar | `dsp` | Audio |
| Integración en `dsp::PhaseEqualizer`: latencia total y cambio con silencio | `dsp` | Audio, y el aviso en el de control |
| `bNN_spectral` a `dsp::BandSettings` | `core/band_model` | Cualquiera |
| Interruptor, medidor y sombreado de la máscara | `ui/logic` (textos, disposición) y `ui/src` | Mensajes |

#### Contratos

- **Parámetros:** 24 nuevos al final; apagados, el sonido es bit a bit el de ahora. Fixture con `ESCEQ_APPEND_PARAMETERS=1`.
- **Estado:** sin cambios de versión (los parámetros nuevos se leen como los demás; ausentes, apagados).
- **Presets:** el interruptor va en ellos; los de fábrica no cambian.
- **Latencia:** cambia solo al pasar de 0 a 1 bandas espectrales encendidas, o al revés.
- **Deshacer:** el interruptor es un parámetro más.

#### Aceptación

| Criterio | Test |
|---|---|
| Un tono dentro de la banda por encima del umbral baja lo que marca el rango, ±0,5 dB; otro tono en la misma banda, a 1/3 de octava y por debajo del umbral, no cambia más de 0,2 dB (y con la banda dinámica normal sí cambia: el test lo comprueba) | `tests/dsp/spectral_dynamics_test.cpp` |
| Por debajo del umbral, la salida es la entrada retrasada, con un error menor de −90 dB | ídem |
| Con el interruptor apagado en todas las bandas, la salida es bit a bit la de ahora, con la latencia de ahora, en los cuatro modos | ídem y `tests/plugin/phase_test.cpp` |
| Con bloques de 1 a 4096 muestras, la salida es bit a bit la misma | ídem |
| La latencia comunicada coincide con la medida, en los cuatro modos y a 44,1, 48, 96 y 192 kHz | `tests/plugin/spectral_test.cpp` (y `tests/dsp/phase_equalizer_test.cpp`, también en mono) |
| Al encender la primera banda espectral no hay chasquidos (el test de los fundidos del cambio de modo) | `tests/dsp/phase_equalizer_test.cpp` y `tests/plugin/spectral_test.cpp` |
| Ninguna llamada hace la trama entera: con bloques de 32 muestras a 48 kHz y 24 bandas, como mucho 2 de sus 32 pasos (ADR 0025) | `tests/dsp/spectral_dynamics_test.cpp` (recuentos, sin tiempos) |
| Sin asignaciones ni bloqueos en el hilo de audio | job `rtsan` |
| Coste a 48 kHz en estéreo, con 24 bandas espectrales en L/R y M/S: menos del 5 % de un núcleo (medido el motor: del 3,9 al 4,2 %, y del 3,1 al 3,3 % en otro momento de la misma máquina; con una, el 1,1 %, porque con 24 manda la curva de cada bin y no la FFT; con el ecualizador, del 3,4 al 3,9 % en los cuatro modos) | `esceq_bench --spectral` (el motor) y `esceq_bench --scenario espectral` (el ecualizador) |

#### Bloques de implementación

4. **6.4 Motor** ✅ (`dsp/spectral_dynamics.h`, sin parámetro): STFT, detector, máscaras, reconstrucción, sus tests (`tests/dsp/spectral_dynamics_test.cpp`), `esceq_bench --spectral` y [docs/dsp/dinamica-espectral.md](docs/dsp/dinamica-espectral.md); la ADR 0024 aceptada con las medidas. Medido: el tono baja −5,995 dB con un rango de −6 y el débil a 1/3 de octava no cambia (−0,0001 dB; con la banda dinámica normal, −3,19); por debajo del umbral, la salida es la entrada retrasada con −129 dB de error; la ganancia se mueve 0,59 dB de una trama a otra con ruido en el umbral; cambiar los ajustes de golpe deja los agudos espurios en −122 dB; la reducción empieza hasta 22 ms antes del sonido (media ventana). Lo que cambió al medir está en «Cambios al aceptarla» de la ADR: el suavizado de 1/6 de octava va en el nivel del detector y no en la ganancia, 3 FFT por salto en vez de 6, y la latencia se queda en N. Queda para el 6.5: el cálculo de una trama llega entero en la llamada que completa un salto (0,46 ms con 24 bandas a 48 kHz, unos 2,5 ms a 192 kHz); con bloques pequeños a 192 kHz esa llamada dura mucho más que el bloque, y repartirlo costaría un salto más de latencia, que decide el usuario.
5. **6.5 Integración** ✅: el parámetro `bNN_spectral` (índices 364 a 387, 388 en total, apagado por defecto, en los presets, el estado sin cambio de versión), `band_model` (`dsp::DynamicSettings::spectral`), `PhaseEqualizer` (la máscara en lugar de la dinámica de las bandas espectrales, la latencia sumada a la del modo y el cambio con silencio, el bypass con la señal seca retrasada, el detector retrasado en fase lineal y el medidor de la banda, el de la máscara), tests del plugin (`tests/plugin/spectral_test.cpp` y `phase_test.cpp`), `esceq_bench` (escenarios `espectral` y `mezcla-esp`, y la llamada más lenta), `docs/arquitectura.md`, `docs/dsp/fase-lineal.md` y `docs/dsp/dinamica-espectral.md`, y la [ADR 0025](docs/decisiones/0025-dinamica-espectral-en-el-procesador.md). Decidido con el usuario: el cálculo de cada trama se reparte en el salto siguiente (en pasos: la señal, cada fuente, cada banda, cada máscara y la síntesis), a cambio de un salto más de latencia (N + N/4: 53,3 ms a 48 kHz en vez de 42,7), y la latencia la enciende el interruptor, actúe o no la banda. Medido: con 24 bandas espectrales, la llamada más lenta pasa de 0,42–0,53 ms a 0,08 ms a 48 kHz con bloques de 32, y de 1,8–1,9 ms a 0,17 ms a 192 kHz con bloques de 64 (a 192 kHz con bloques de 32, algunas llamadas aún pasan de los 0,17 ms del bloque); el coste medio no cambia (3,4–3,9 % a 48 kHz en los cuatro modos, 14,5–16 % a 192 kHz; la mezcla con 2 bandas espectrales, 1,1–1,2 % a 48 kHz). Con los interruptores apagados, `PhaseEqualizer` es bit a bit el de antes, con la misma latencia, en los cuatro modos (comparado con el código anterior). El CHANGELOG lo recoge con la interfaz (6.6).
6. **6.6 Interfaz**: interruptor, medidor y sombreado de la máscara, `docs/ui.md`, README y `CHANGELOG.md`.

### 5.8 Saturación

**Planificada** (noviembre de 2026), la última de la fase 6. Decidido con el usuario: sigue en el alcance de la 1.0, y sus parámetros van en los presets. La elección entre sobremuestreo y ADAA se mide en el bloque 6.7 y se cierra en su ADR.

**Qué es.** Una etapa global de saturación suave tras el ecualizador, para dar color armónico. Va la última en prioridad: no es ecualización.

**Parámetros**, al final de la tabla, detrás de los de la 5.7 (índices 388 a 390), en los presets (un preset antiguo, que no los lleva, los deja en su valor por defecto: Off):

| Parámetro | Valores | Por defecto |
|---|---|---|
| `sat_mode` ("Saturation") | Off, Soft (simétrica: armónicos impares) o Warm (asimétrica: añade pares) | Off |
| `sat_drive` ("Saturation Drive") | 0 a 24 dB | 6 dB |
| `sat_mix` ("Saturation Mix") | 0 a 100 % | 100 % |

Con Off la etapa no se ejecuta, y la salida y la latencia son bit a bit las de ahora.

**Dónde va en la cadena:** después del ecualizador y de la dinámica espectral, antes de la ganancia de salida y de la compensación automática de ganancia (que no la tiene en cuenta: compensa la curva, y la saturación ya compensa su drive).

**Algoritmo** (referencias: J. Parker, V. Zavalishin y E. Le Bivic, *Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution*, DAFx-16, y S. Bilbao, F. Esqueda, J. Parker y V. Välimäki, *Antiderivative Antialiasing for Memoryless Nonlinearities*, IEEE SPL, 2017, para el ADAA; P. P. Vaidyanathan, *Multirate Systems and Filter Banks*, 1993, y A. Valenzuela y A. Constantinides, *Digital Signal Processing Schemes for Efficient Interpolation and Decimation* (IEE Proc., 1983), para los filtros de media banda):

- Soft: `tanh(d·x)`. Warm: `tanh(d·x + b) − tanh(b)`, con una polarización b fija, seguida de un bloqueador de continua de un polo a 5 Hz.
- Contra el aliasing, sobremuestreo ×4 en dos etapas ×2 de media banda:
  - polifásicos IIR (pasa todo) en modo IIR, sin latencia comunicada;
  - FIR de fase lineal en los modos lineales, con su latencia (unas decenas de muestras) sumada mientras la saturación no esté en Off. Pasar de Off a otro modo en fase lineal cambia la latencia, con el hueco del cambio de modo.
- La alternativa, ADAA de primer orden (sin sobremuestreo, o con ×2), se mide antes de elegir: el bloque 6.7 compara aliasing, coste y respuesta en agudos, y lo decide en su ADR.
- **Compensación del drive:** la salida se divide por la ganancia que el drive da a un seno de −18 dBFS a 1 kHz, calculada numéricamente (un periodo) cuando cambia el drive, en el hilo de audio por bloque (no asigna; unos microsegundos) y aplicada con una rampa.
- **Mezcla:** la señal seca pasa por los mismos filtros de sobremuestreo, sin la no linealidad, para que mezclar no haga filtro de peine en agudos.

**Dónde vive:** `dsp/saturation.h` (curvas, sobremuestreo, bloqueador de continua), integrada en `dsp::PhaseEqualizer`; los parámetros en `core`; la interfaz en `ui/logic` y `ui/src`. **Dónde se muestra** (la barra superior está llena) es una pregunta para el usuario al empezar el bloque 6.9: una tarjeta en el panel cuando no hay banda seleccionada, el menú de opciones o un botón que abra un pequeño panel.

**Aceptación**

| Criterio | Test |
|---|---|
| Con Off, la salida es bit a bit la de ahora, con la latencia de ahora | `tests/dsp/saturation_test.cpp` y `tests/plugin/` |
| Con Soft, los armónicos pares quedan por debajo de −100 dB; con Warm, el segundo armónico queda por encima de −60 dB con 12 dB de drive | ídem |
| Con un seno de 10 kHz, drive de 12 dB y 48 kHz, el aliasing queda 80 dB por debajo de la fundamental | ídem |
| La continua a la salida queda por debajo de −100 dB tras 1 s | ídem |
| Con cualquier drive, un seno de −18 dBFS a 1 kHz sale al mismo nivel, ±0,5 dB | ídem |
| Con la mezcla al 0 %, la salida es la entrada por los filtros de sobremuestreo, a menos de −0,1 dB hasta 18 kHz | ídem |
| La latencia comunicada coincide con la medida en los cuatro modos de fase | `tests/plugin/` |
| Bloques de 1 a 4096 muestras, bit a bit iguales; sin asignaciones (`rtsan`) | ídem |
| Coste a 48 kHz en estéreo: menos del 2 % de un núcleo | `esceq_bench` |

**Bloques de implementación**

7. **6.7 Motor y ADR**: `dsp/saturation.h`, la medida de sobremuestreo frente a ADAA, sus tests, `docs/dsp/saturacion.md` y la ADR.
8. **6.8 Integración**: los tres parámetros, `PhaseEqualizer` y la latencia, tests del plugin y `esceq_bench`.
9. **6.9 Interfaz**: tras preguntar al usuario dónde va; `docs/ui.md`, README y `CHANGELOG.md`.

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
| macOS | Firmar y notarizar exige cuenta de desarrollador de Apple (99 USD/año). Sin ella el usuario tiene que saltarse Gatekeeper a mano; mientras tanto, paquetes firmados ad hoc y un `.zip`, sin `.pkg` ([ADR 0022](docs/decisiones/0022-compatibilidad-en-macos-y-linux.md)) |
| Linux | Se compila en un contenedor de Ubuntu 22.04 para que cargue desde Ubuntu 22.04, Debian 12 y Fedora 36; el CI comprueba las versiones de glibc y libstdc++ que pide ([ADR 0022](docs/decisiones/0022-compatibilidad-en-macos-y-linux.md)) |
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
| **5. Calidad** ✅ | Corrección en agudos (hecha, ver [ADR 0013](docs/decisiones/0013-correccion-en-agudos.md)), fase lineal (hecha, ver [ADR 0012](docs/decisiones/0012-modos-de-fase.md)), compensación automática de ganancia (hecha, ver [ADR 0015](docs/decisiones/0015-compensacion-automatica-de-ganancia.md)), tiempos automáticos en la dinámica (hechos, ver [ADR 0016](docs/decisiones/0016-tiempos-automaticos-en-la-dinamica.md)), escala de notas (hecha, ver [ADR 0017](docs/decisiones/0017-escala-de-notas.md)), analizador ampliado (hecho, ver [ADR 0018](docs/decisiones/0018-analizador-ampliado.md)), bandas desde el espectro (hechas, ver [ADR 0019](docs/decisiones/0019-bandas-desde-el-espectro.md)), optimización SIMD (medida: no compensa y no se hace, ver [ADR 0020](docs/decisiones/0020-sin-simd.md)), macOS y Linux (hechos, ver [ADR 0022](docs/decisiones/0022-compatibilidad-en-macos-y-linux.md)) | Versión 0.5 (falta publicarla) | 8 a 12 semanas |
| **6. Avanzado** | En este orden, cada bloque en un chat: igualar a una referencia (5.6, [ADR 0023](docs/decisiones/0023-igualar-a-una-referencia.md): 6.1 ajuste en `dsp` ✅, 6.2 aprender y aplicar sin ventana ✅, 6.3 interfaz ✅; pendiente, el defecto de las bandas estrechas que ya hay), dinámica espectral (5.7, [ADR 0024](docs/decisiones/0024-dinamica-espectral.md) y [0025](docs/decisiones/0025-dinamica-espectral-en-el-procesador.md): 6.4 motor ✅, 6.5 integración ✅, 6.6 interfaz), saturación (5.8: 6.7 motor y ADR, 6.8 integración, 6.9 interfaz) y surround (sin planificar). Hecha y adelantada: la detección de colisiones entre instancias (ver [ADR 0011](docs/decisiones/0011-espectros-compartidos.md)) | Versión 1.0 | Abierto |

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
