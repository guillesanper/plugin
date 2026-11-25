# esceQ: plan del proyecto

Plugin de ecualización paramétrica y dinámica, libre, pensado para funcionar bien en FL Studio y con un flujo de trabajo comparable al de los ecualizadores comerciales de referencia (FabFilter Pro-Q).

Estado: **fases 0 a 5 completadas y la 0.5.0 lista para etiquetar.** La 0.5.0 está cerrada en `CHANGELOG.md` (2026-11-24) y será la primera versión pública: falta que el usuario suba la etiqueta `v0.5.0`, que compila, valida, empaqueta y publica la release en GitHub. La 0.1.0 se cerró (2026-11-12) pero no se publica: sus paquetes de macOS y Linux tenían los fallos de la [ADR 0022](docs/decisiones/0022-compatibilidad-en-macos-y-linux.md) (decidido con el usuario, nota en la [ADR 0010](docs/decisiones/0010-distribucion.md)); sus contratos se mantienen, y los de la 0.5.0 se comprueban con sus archivos `release_0.5*` en `tests/core/release_contract_test.cpp`. De la fase 5 están hechas la fase lineal, la corrección en agudos (algoritmo Matched, con el estado en la versión 3: sección 4.1) y, adelantada de la fase 6, la detección de colisiones entre instancias. También están hechas la compensación automática de ganancia (sección 5.5, [ADR 0015](docs/decisiones/0015-compensacion-automatica-de-ganancia.md)), los tiempos automáticos en la dinámica (sección 5.4, [ADR 0016](docs/decisiones/0016-tiempos-automaticos-en-la-dinamica.md)), la escala de notas (sección 5.1, [ADR 0017](docs/decisiones/0017-escala-de-notas.md)), el analizador ampliado (sección 5.2, [ADR 0018](docs/decisiones/0018-analizador-ampliado.md)) y las bandas desde el espectro (sección 5.3, [ADR 0019](docs/decisiones/0019-bandas-desde-el-espectro.md)). La optimización SIMD se ha medido y no compensa, así que no se ha hecho ([ADR 0020](docs/decisiones/0020-sin-simd.md)). macOS y Linux están completos ([ADR 0022](docs/decisiones/0022-compatibilidad-en-macos-y-linux.md)): Linux se compila en Ubuntu 22.04 y carga desde Ubuntu 22.04 y Debian 12, y los tres formatos de macOS se comprueban (universales, macOS 11, firma ad hoc) en el CI; el CI está estable en los tres sistemas ([ADR 0021](docs/decisiones/0021-aviso-de-latencia-sin-temporizador.md)). Falta probarlos en un Mac y en un Linux con hosts reales. La fase 6 está en marcha (sección 7): igualar a una referencia (5.6, [ADR 0023](docs/decisiones/0023-igualar-a-una-referencia.md), aceptada, bloques 6.1 a 6.3), dinámica espectral (5.7, [ADR 0024](docs/decisiones/0024-dinamica-espectral.md), aceptada, 6.4 a 6.6, completa) y saturación (5.8, [ADR 0026](docs/decisiones/0026-saturacion.md), aceptada, 6.7 a 6.9, completa). La igualación está completa: hechos el 6.1, el ajuste en `dsp` ([docs/dsp/igualacion.md](docs/dsp/igualacion.md)), el 6.2, aprender y aplicar sin ventana (`core/match.h` y `ui/logic/match.h`), y el 6.3, la interfaz (el panel *Match*, [docs/ui.md](docs/ui.md)); y corregido el defecto que salió en el 6.3 (una banda estrecha que ya había, como un notch, se "rellenaba"): la curva actual se compara al mismo ancho que el objetivo (sección 5.6, decidido con el usuario, nota en la ADR 0023). La dinámica espectral está completa: hechos el 6.4, el motor en `dsp` (`dsp/spectral_dynamics.h`, [docs/dsp/dinamica-espectral.md](docs/dsp/dinamica-espectral.md), con la ADR 0024 aceptada), el 6.5, su integración: el parámetro `bNN_spectral` (388 parámetros) y la dinámica espectral en `dsp::PhaseEqualizer`, con el cálculo de cada trama repartido en el salto siguiente y la latencia (53,3 ms a 48 kHz) ligada al interruptor ([ADR 0025](docs/decisiones/0025-dinamica-espectral-en-el-procesador.md)), y el 6.6, la interfaz: el interruptor *Spectral* junto a *Auto*, con la latencia en su ayuda, el medidor y el sombreado de la máscara bajo la curva ([docs/ui.md](docs/ui.md), nota en la ADR 0025). De la saturación está hecho el 6.7, el motor en `dsp` (`dsp/saturation.h`, [docs/dsp/saturacion.md](docs/dsp/saturacion.md)), con la ADR 0026 aceptada: sobremuestreo hasta unos 384 kHz (×8 a 44,1 y 48 kHz), elegido por el usuario con las medidas, con filtros de media banda IIR en el modo IIR y de fase lineal en los lineales, y sin ADAA; y el 6.8, su integración: los parámetros `sat_mode`, `sat_drive` y `sat_mix` (391 parámetros, en los presets) y la saturación al final de `dsp::PhaseEqualizer`, con los filtros del modo y su latencia (47 muestras a 48 kHz) solo en fase lineal y fuera de Off (nota en la ADR 0026); y el 6.9, su interfaz: el cajón plegable *Output* entre la curva y el medidor, con la saturación, la ganancia de salida y *Auto*, que dejan la barra superior ([ADR 0027](docs/decisiones/0027-cajon-de-la-etapa-de-salida.md), decidido con el usuario, [docs/ui.md](docs/ui.md)). Con eso, de la fase 6 solo queda el surround, sin planificar. Planificada la fase 7, la integración con el host (sección 5.9, [ADR 0028](docs/decisiones/0028-automatizacion-macros-y-sidechain.md), propuesta, bloques 7.1 a 7.6): automatización robusta con los controladores de FL, ocho macros y las mejoras del sidechain. Sigue pendiente comprobar en FL Studio, con una mezcla real, el enrutado del sidechain.

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

**Medido (fase 5).** Una mezcla típica de 8 bandas cuesta del 0,55 al 0,95 % de un núcleo a 44,1 y 48 kHz y del 2,4 al 3,9 % a 192 kHz, en los cuatro modos; el tamaño de bloque del host apenas influye. Lo caro son las cascadas IIR largas (24 cortes de 96 dB/oct: 6 % a 48 kHz, 22 % a 192 kHz), limitadas por la latencia de la recurrencia, y las 24 bandas dinámicas (5 % y de 15 a 22 %), que se van en `log10`, la curva de ganancia y el rediseño por muestra. SSE2 ahorra un 18–25 % en las cascadas estáticas y nada en la dinámica, y en arm64 no daría el mismo resultado que en x86-64: **no se usa SIMD** ([ADR 0020](docs/decisiones/0020-sin-simd.md)). La saturación (bloque 6.7) tampoco: su curva (una tanh propia sin ramas) y sus FIR (convolución por bloques) los vectoriza el compilador, y cuesta del 0,7 al 1,3 % a 48 kHz y del 1 al 1,4 % a 192 kHz, con sobremuestreo ×8 a 48 kHz ([ADR 0026](docs/decisiones/0026-saturacion.md)); en el ecualizador (bloque 6.8), la mezcla de 8 bandas con saturación cuesta del 1,45 al 1,9 % a 48 kHz y del 2,8 al 4,4 % a 192 kHz en los cuatro modos (del 0,7 al 1,2 % más que sin ella). La dinámica espectral (bloques 6.4 y 6.5) no lo necesita: sus bucles por bin están escritos sin ramas para que el compilador los vectorice, y cuesta del 3,9 al 4,2 % con 24 bandas a 48 kHz (GCC; 3,1 % con Clang, que vectoriza todos), y del 3,4 al 3,9 % con el ecualizador; su cálculo se reparte en el salto siguiente, para que ninguna llamada haga una trama entera ([ADR 0025](docs/decisiones/0025-dinamica-espectral-en-el-procesador.md)). `esceq_bench` (`tools/bench`) repite las medidas.

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
| Igualar a una referencia ✅ | 5.6 | 6 | Ninguno (crea bandas normales; nada nuevo en el estado) | No |
| Dinámica espectral ✅ | 5.7 | 6 | 24 (uno por banda, en los presets) | Sí, mientras alguna banda tenga el interruptor: 2560 muestras a 48 kHz (53,3 ms) |
| Saturación ✅ | 5.8 | 6 | 3 globales (en los presets) | Solo en fase lineal y fuera de Off (los filtros de sobremuestreo: 47 muestras a 48 kHz, 0,98 ms) |
| Automatización, macros y sidechain | 5.9 | 7 | 81: 8 macros, 1 global y 3 por banda (en los presets); las asignaciones de las macros en el estado, versión 4 | No |

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

**Hecho** (noviembre de 2026), con el defecto que salió en el 6.3 corregido (al final de esta sección): decisiones en la [ADR 0023](docs/decisiones/0023-igualar-a-una-referencia.md), aceptada con las medidas del bloque 6.1. Es el primer trabajo de la fase 6, en tres bloques (al final de esta sección), los tres hechos: el 6.1, el ajuste en `dsp` ([docs/dsp/igualacion.md](docs/dsp/igualacion.md)); el 6.2, aprender y aplicar sin ventana (de extremo a extremo, la curva aplicada quedaba a 0,13 dB de la conocida; tras la corrección, a 0,035 dB), y el 6.3, la interfaz ([docs/ui.md](docs/ui.md), «Igualar a una referencia: el panel Match»). El 6.1 cambió el centrado del objetivo, lo de fuera del rango, la parada, el suavizado en graves, las opciones de suavizado y los segundos útiles (lo que sigue ya lo recoge). Decidido con el usuario: el panel se abre desde el menú del botón de comparar (la barra superior no tiene sitio), la pista se mide **antes** del ecualizador, entran las dos referencias (otra instancia y el sidechain) y los nombres son *Match*, *Learn* y *Apply*.

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
N(f) = T(f) − S(C)(f)                               C: curva estática de las bandas actuales; S: su promedio
                                                    en potencia con la ventana del suavizado (corrección)
```

- Centrado en la mediana, no en la media: se iguala lo que se aparta de lo más común, y una referencia con más graves y agudos da shelves, no un corte ancho de los medios.
- Fuera del rango, N es lo del borde, con un peso de 0,1: las bandas nuevas no crean nada ahí, pero un shelf puede seguir. El peso pasa de 0,1 a 1 en 1/6 de octava.
- El ajuste se evalúa en una rejilla de 384 puntos logarítmicos (el doble de densa que la de la sesión, interpolando en frecuencia logarítmica), para que una campana estrecha no caiga entre dos puntos.

**3. Inicio voraz** (`dsp`): con el residuo r = N,

- se toma el punto del rango de mayor |r|·w; si es menor de 0,5 dB, se para;
- su lóbulo es el tramo alrededor con el mismo signo y |r| por encima de la mitad del máximo. Si el lóbulo llega a un extremo del rango, es un **shelf** en el punto medio de su flanco, con la ganancia del extremo y Q 0,707; si no, una **campana** en el máximo, con la ganancia del máximo y el Q del ancho del lóbulo a media altura (la fórmula de `qForBandwidth`, ADR 0019, que se lleva a `dsp`);
- se resta su curva y se repite hasta agotar las bandas.

**4. Refino conjunto** (Levenberg-Marquardt): todas las bandas propuestas a la vez, en sus variables naturales (log2 de la frecuencia, ganancia en dB, log del Q), y un nivel c casi libre, minimizando Σ w(f)·(N(f) + c − Σ Hk(f))² más dos penalizaciones pequeñas, de c y de las ganancias, con las curvas analíticas de `dsp` (`designBand` y `magnitudeDbAtTangent`, con las tangentes de la rejilla calculadas una vez). El jacobiano es por diferencias centrales, y solo de la banda que se mueve (la curva es una suma), así que cada iteración cuesta unas 6·384 evaluaciones por banda. Límites: frecuencia dentro del rango, ganancia de ±18 dB, Q de 0,3 a 16 en campanas (y, desde la corrección del defecto, no más que el de la ventana del suavizado) y de 0,5 a 1 en shelves (proyectados en cada paso). Desde esa corrección, lo que se minimiza es la diferencia entre T y la curva total suavizada, S(C + Σ Hk) (al final de esta sección). Se refina tras añadir cada banda (como mucho 20 iteraciones) y al final (100).

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
| Una banda estrecha que ya hay (el notch de `tools/snapshot`) cuenta por lo que quita al ancho del suavizado: lo propuesto no pasa del Q de la ventana, y aplicar dos veces no crea nada (corrección del defecto del 6.3) | `tests/dsp/curve_match_test.cpp` |
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
2. **6.2 Aprender y aplicar, sin ventana** ✅: `core/match.h` (`MatchLearner`, `MatchSettings`, `MatchWorker`), `ui/logic/match.h` (aprendizaje, estado del panel, cambios de *Apply*), la cola del sidechain y lo aprendido en el procesador, *Apply* como una acción, y sus tests (`tests/core/match_test.cpp`, `tests/ui/match_test.cpp`, `tests/plugin/match_test.cpp`, `tests/plugin/history_test.cpp`); `docs/arquitectura.md` (módulos, hilos y comunicación). Hecho: con ruido rosa y el mismo ruido por tres campanas y un shelf, 10 s simulados y *Apply* al 100 % con 1/3 de octava, salen 4 bandas y la curva quedaba a 0,13 dB de la conocida de 50 Hz a 12 kHz, con las dos referencias (a 0,035 dB tras la corrección del defecto, abajo) ([docs/dsp/igualacion.md](docs/dsp/igualacion.md)). Lo nuevo respecto a lo planificado: la sesión publica la frecuencia de muestreo y un reloj de audio de cada instancia (lo aprendido se pondera por él, y con el transporte parado no avanza); mientras se aprende, la propuesta se vuelve a pedir cada segundo útil; `MatchReference` y la cola del sidechain (`MatchSidechainTap`) van también en `core/match.h`; y el *Apply* del procesador es `PluginProcessor::applyAction`. El audio de la pista lo da la interfaz al controlador (`ui::MatchController::pushTrack`), con lo que lee de la cola "pre", que sigue teniendo un solo lector.
3. **6.3 Interfaz** ✅: entrada del menú de comparar, el panel y su disposición en `ui/logic/layout`, el dibujo de los espectros, el objetivo y la propuesta, `docs/ui.md`, la tabla de uso del README y `CHANGELOG.md`. Hecho: *Match to reference…* bajo las instancias del menú de comparar; la tarjeta (`MatchPanel`, 280 × 240 px, `matchPanelLayout`) cabe en la curva en todos los tamaños, con teclado y en la vista ampliada (`tests/ui/layout_test.cpp`); los textos, el rango escrito como la frecuencia y lo que se dibuja, en `ui/logic/match.h` con sus tests; `tools/snapshot --match`. Lo que cambió respecto a lo planificado: la propuesta se dibuja como la curva total que se ve más las bandas nuevas, columna a columna (la `proposedDb` del ajuste usa la rejilla de 384 puntos y la regla de canales de la compensación, y no coincidía con la curva blanca: un notch estrecho salía casi plano, y una banda en M, distinta); los nodos en fantasma llevan el color de la banda libre que ocuparían; hay una leyenda; y la propuesta anterior se queda atenuada mientras se recalcula. La tarjeta tapa en S y M la zona de agudos por encima de unos 0 dB (filas a 4 px para tapar lo mínimo).

**Corrección del defecto encontrado en el 6.3** ✅. El objetivo T estaba suavizado al ancho elegido, pero la curva actual C no: N = T − C veía entera una banda estrecha que ya había. Con la curva de `tools/snapshot` (un notch de Q 8 a 5,2 kHz), la propuesta era una campana de +17,7 dB y Q 16 a 5194 Hz que deshacía el notch. Corregido en el ajuste (`dsp/curve_match.cpp`), sin tocar la interfaz:

- El ajuste minimiza la diferencia entre T y la curva total suavizada en potencia con la misma ventana, S(C + Σ Hk), en vez de T − C − Σ Hk. Una banda estrecha que ya hay cuenta por la energía que quita a ese ancho; decidido con el usuario frente a no contarla (que pondría un corte ancho encima de un notch que quita una resonancia). Aplicar dos veces ve lo que dejó el primer ajuste.
- El Q de una campana propuesta no pasa del de una que abarca la ventana del suavizado (`dsp::matchMaxBellQ`: 4,3 con 1/3 de octava). El objetivo no ve nada más estrecho, y sin ese límite el ajuste suavizado dibujaba lo mismo con campanas estrechas y altas.
- Medido: junto al notch, una campana de +5,7 a 6,2 dB y Q 4,3 con 1/3 de octava (+10 dB y Q 8,6 con 1/6, +1,9 dB con 1 octava), y la segunda vez, nada. En 300 casos aleatorios (`esceq_bench --match`), la segunda vez propone algo en el 2,0 % (antes, el 9,3 %), con 2,3 dB como mucho (antes, 10,4). De extremo a extremo, 0,035 dB (antes, 0,13). Los demás criterios siguen cumpliéndose. El coste no cambia en los objetivos del bench sin bandas en la pista; con suavizado y bandas en la pista, 38 ms de media (antes, 20,5). `tools/snapshot --match` vuelve a llevar el notch. Detalle en [docs/dsp/igualacion.md](docs/dsp/igualacion.md), «Bandas estrechas que ya hay», y en la ADR 0023, «Cambios al aceptarla».

### 5.7 Dinámica espectral

**Hecha** (noviembre de 2026): decisiones en la [ADR 0024](docs/decisiones/0024-dinamica-espectral.md), aceptada con las medidas del bloque 6.4 (hecho: el motor, [docs/dsp/dinamica-espectral.md](docs/dsp/dinamica-espectral.md)), y en la [ADR 0025](docs/decisiones/0025-dinamica-espectral-en-el-procesador.md), del bloque 6.5 (hecho: la integración), con una nota del bloque 6.6 (hecho: la interfaz, [docs/ui.md](docs/ui.md)). Va después de la 5.6, en tres bloques (al final de esta sección). Decidido con el usuario: la latencia existe solo mientras haya una banda con el interruptor encendido (sin reservarla, y aunque esa banda no actúe), la STFT es de 2048 puntos a 48 kHz, sin selector, y el cálculo de cada trama se reparte en el salto siguiente, con un salto más de latencia (N + N/4: 53,3 ms a 48 kHz).

**Qué es.** Una variante de la banda dinámica que, dentro de su zona, reduce o realza **solo las frecuencias que pasan del umbral**, y no toda la banda. Controla resonancias que se mueven (una voz con armónicos que cambian, una guitarra acústica con notas que retumban) sin apagar el resto de esa zona. Es uno de los diferenciadores citados en la sección 8.

**Parámetro:** `bNN_spectral` ("Band N Spectral"), un interruptor por banda, apagado por defecto, al final de la tabla (índices 364 a 387; quedarían 388 parámetros). Va en los presets, como `bNN_autotime`: es parte del sonido de la banda. Solo actúa si la banda es dinámica (`bNN_dyn`) y en los tipos en que actúa la dinámica ([docs/dsp/dinamica.md](docs/dsp/dinamica.md)): campana, shelves y tilt (en tilt, en todo el espectro); en los demás no hace nada. Usa sus mismos umbral, rango, rodilla, ataque, relajación, tiempos automáticos, sidechain y canal: no hay controles nuevos.

#### Comportamiento

- En la tarjeta de dinámica del panel, un interruptor *Spectral* junto a *Auto* (tiempos automáticos); desactivado si la dinámica está apagada, pero con su estado a la vista (si está encendido, la latencia sigue: ADR 0025). Su ayuda dice la latencia que añade ("Spectral · adds 53.3 ms latency").
- Con la banda espectral, la **parte dinámica de toda la banda deja de actuar**: la hace la máscara, frecuencia a frecuencia. La parte estática (la ganancia de la banda) sigue igual, en el ecualizador normal.
- El medidor de dinámica del panel y el de junto al nodo muestran la reducción **máxima** de la máscara dentro de la banda (la de la frecuencia que más baja), por los 24 atómicos que ya existen. La forma de la máscara se dibuja como un sombreado bajo la curva de la banda (192 puntos, a 30 Hz, por un `dsp::TripleBuffer`). La curva de la banda es la estática y la curva total le suma la máscara: bajar la campana entera lo que baja su bin más reducido contradecía al sombreado (nota de la ADR 0025, bloque 6.6).
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
| Reducción de la máscara a 192 puntos y su publicación (`core/spectral_mask.h`) | `core` | Audio (publica) y mensajes (lee) |
| Interruptor, medidor y sombreado de la máscara | `ui/logic` (textos, disposición, máscara en las columnas de la curva) y `ui/src` | Mensajes |

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
| Interfaz: el interruptor, desactivado sin dinámica; su ayuda, con la latencia de la frecuencia de muestreo actual; la disposición sin solapes en todos los tamaños; la máscara reducida a 192 puntos sin perder la profundidad de un tono y llevada a las columnas de la curva; publicada 30 veces por segundo de audio, solo mientras suena; `processBlock` bit a bit igual con la máscara leída | `tests/ui/control_logic_test.cpp`, `layout_test.cpp`, `spectral_mask_test.cpp`, `curve_mapping_test.cpp`, `tests/core/spectral_mask_test.cpp` y `tests/plugin/spectral_test.cpp` |
| Coste a 48 kHz en estéreo, con 24 bandas espectrales en L/R y M/S: menos del 5 % de un núcleo (medido el motor: del 3,9 al 4,2 %, y del 3,1 al 3,3 % en otro momento de la misma máquina; con una, el 1,1 %, porque con 24 manda la curva de cada bin y no la FFT; con el ecualizador, del 3,4 al 3,9 % en los cuatro modos) | `esceq_bench --spectral` (el motor) y `esceq_bench --scenario espectral` (el ecualizador) |

#### Bloques de implementación

4. **6.4 Motor** ✅ (`dsp/spectral_dynamics.h`, sin parámetro): STFT, detector, máscaras, reconstrucción, sus tests (`tests/dsp/spectral_dynamics_test.cpp`), `esceq_bench --spectral` y [docs/dsp/dinamica-espectral.md](docs/dsp/dinamica-espectral.md); la ADR 0024 aceptada con las medidas. Medido: el tono baja −5,995 dB con un rango de −6 y el débil a 1/3 de octava no cambia (−0,0001 dB; con la banda dinámica normal, −3,19); por debajo del umbral, la salida es la entrada retrasada con −129 dB de error; la ganancia se mueve 0,59 dB de una trama a otra con ruido en el umbral; cambiar los ajustes de golpe deja los agudos espurios en −122 dB; la reducción empieza hasta 22 ms antes del sonido (media ventana). Lo que cambió al medir está en «Cambios al aceptarla» de la ADR: el suavizado de 1/6 de octava va en el nivel del detector y no en la ganancia, 3 FFT por salto en vez de 6, y la latencia se queda en N. Queda para el 6.5: el cálculo de una trama llega entero en la llamada que completa un salto (0,46 ms con 24 bandas a 48 kHz, unos 2,5 ms a 192 kHz); con bloques pequeños a 192 kHz esa llamada dura mucho más que el bloque, y repartirlo costaría un salto más de latencia, que decide el usuario.
5. **6.5 Integración** ✅: el parámetro `bNN_spectral` (índices 364 a 387, 388 en total, apagado por defecto, en los presets, el estado sin cambio de versión), `band_model` (`dsp::DynamicSettings::spectral`), `PhaseEqualizer` (la máscara en lugar de la dinámica de las bandas espectrales, la latencia sumada a la del modo y el cambio con silencio, el bypass con la señal seca retrasada, el detector retrasado en fase lineal y el medidor de la banda, el de la máscara), tests del plugin (`tests/plugin/spectral_test.cpp` y `phase_test.cpp`), `esceq_bench` (escenarios `espectral` y `mezcla-esp`, y la llamada más lenta), `docs/arquitectura.md`, `docs/dsp/fase-lineal.md` y `docs/dsp/dinamica-espectral.md`, y la [ADR 0025](docs/decisiones/0025-dinamica-espectral-en-el-procesador.md). Decidido con el usuario: el cálculo de cada trama se reparte en el salto siguiente (en pasos: la señal, cada fuente, cada banda, cada máscara y la síntesis), a cambio de un salto más de latencia (N + N/4: 53,3 ms a 48 kHz en vez de 42,7), y la latencia la enciende el interruptor, actúe o no la banda. Medido: con 24 bandas espectrales, la llamada más lenta pasa de 0,42–0,53 ms a 0,08 ms a 48 kHz con bloques de 32, y de 1,8–1,9 ms a 0,17 ms a 192 kHz con bloques de 64 (a 192 kHz con bloques de 32, algunas llamadas aún pasan de los 0,17 ms del bloque); el coste medio no cambia (3,4–3,9 % a 48 kHz en los cuatro modos, 14,5–16 % a 192 kHz; la mezcla con 2 bandas espectrales, 1,1–1,2 % a 48 kHz). Con los interruptores apagados, `PhaseEqualizer` es bit a bit el de antes, con la misma latencia, en los cuatro modos (comparado con el código anterior). El CHANGELOG lo recoge con la interfaz (6.6).
6. **6.6 Interfaz** ✅: el interruptor *Spectral* junto a *Auto* (desactivado sin dinámica o en un tipo que no la admite, pero con su estado a la vista; su ayuda dice la latencia con la frecuencia de muestreo actual y el hueco al automatizar la primera banda), el medidor y la etiqueta con la reducción máxima, el sombreado de la máscara bajo la curva estática de la banda con la curva total sumándola, "Spectral" en la etiqueta de la vista ampliada, `core/spectral_mask.h` (192 puntos, 30 veces por segundo de audio, por un `dsp::TripleBuffer`, sin asignar), `ui/logic` (`spectralSwitchEnabled`, `spectralTooltip`, la disposición y `ui/spectral_mask.h`) con sus tests, `esceq_snapshot --spectral`, `docs/ui.md` (con captura), `docs/arquitectura.md`, README, `CHANGELOG.md` y una nota en la ADR 0025. La columna lateral de la tarjeta de dinámica pasa de 104 a 156 px (las celdas de los potenciómetros, en el tamaño mínimo, de 88 a 81). Con la máscara publicada, `processBlock` es bit a bit el mismo se lea o no.

### 5.8 Saturación

**Hecho** (noviembre de 2026), la última de la fase 6, en tres bloques (al final de esta sección). Decidido con el usuario: sigue en el alcance de la 1.0, y sus parámetros van en los presets. La elección entre sobremuestreo y ADAA se ha medido en el bloque 6.7 y está en la [ADR 0026](docs/decisiones/0026-saturacion.md): sobremuestreo hasta unos 384 kHz, sin ADAA (lo que cambia respecto a lo de abajo está en el bloque 6.7).

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
- Contra el aliasing, sobremuestreo en etapas ×2 de media banda (×4 en la propuesta; ×8 hasta 50 kHz, ×4 hasta 100 kHz y ×2 por encima tras medirlo, bloque 6.7):
  - polifásicos IIR (pasa todo) en modo IIR, sin latencia comunicada;
  - FIR de fase lineal en los modos lineales, con su latencia (unas decenas de muestras) sumada mientras la saturación no esté en Off. Pasar de Off a otro modo en fase lineal cambia la latencia, con el hueco del cambio de modo.
- La alternativa, ADAA de primer orden (sin sobremuestreo, o con ×2), se mide antes de elegir: el bloque 6.7 compara aliasing, coste y respuesta en agudos, y lo decide en su ADR. Medido: descartada (bloque 6.7).
- **Compensación del drive:** la salida se divide por la ganancia que el drive da a un seno de −18 dBFS a 1 kHz, calculada numéricamente (un periodo) cuando cambia el drive, en el hilo de audio por bloque (no asigna; unos microsegundos) y aplicada con una rampa.
- **Mezcla:** la señal seca pasa por los mismos filtros de sobremuestreo, sin la no linealidad, para que mezclar no haga filtro de peine en agudos.

**Dónde vive:** `dsp/saturation.h` (curvas, sobremuestreo, bloqueador de continua), integrada en `dsp::PhaseEqualizer`; los parámetros en `core`; la interfaz en `ui/logic` y `ui/src`. **Dónde se muestra** (la barra superior está llena) se preguntó al usuario al empezar el bloque 6.9, con tres opciones (una tarjeta en el panel cuando no hay banda seleccionada, el menú de opciones o un botón que abra un pequeño panel); eligió otra, un cajón plegable (bloque 6.9, [ADR 0027](docs/decisiones/0027-cajon-de-la-etapa-de-salida.md)).

**Aceptación**

| Criterio | Test |
|---|---|
| Con Off, la salida es bit a bit la de ahora, con la latencia de ahora | `tests/dsp/saturation_test.cpp` y `tests/plugin/` |
| Con Soft, los armónicos pares quedan por debajo de −100 dB; con Warm, el segundo armónico queda por encima de −60 dB con 12 dB de drive | ídem |
| Con un seno de 10 kHz, drive de 12 dB y 48 kHz, el aliasing queda 80 dB por debajo de la fundamental (a cualquier nivel, hasta 0 dBFS: decidido en el bloque 6.7) | ídem |
| La continua a la salida queda por debajo de −100 dB tras 1 s | ídem |
| Con cualquier drive, un seno de −18 dBFS a 1 kHz sale al mismo nivel, ±0,5 dB | ídem |
| Con la mezcla al 0 %, la salida es la entrada por los filtros de sobremuestreo, a menos de −0,1 dB hasta 18 kHz | ídem |
| La latencia comunicada coincide con la medida en los cuatro modos de fase | `tests/plugin/` |
| Bloques de 1 a 4096 muestras, bit a bit iguales; sin asignaciones (`rtsan`) | ídem |
| Coste a 48 kHz en estéreo: menos del 2 % de un núcleo | `esceq_bench` |

**Bloques de implementación**

7. **6.7 Motor y ADR** ✅: `dsp/saturation.h` (`dsp::Saturation` y `dsp::Oversampler`, sin parámetros), la medida de sobremuestreo frente a ADAA, sus tests (`tests/dsp/saturation_test.cpp`), `esceq_bench --saturation`, [docs/dsp/saturacion.md](docs/dsp/saturacion.md) y la [ADR 0026](docs/decisiones/0026-saturacion.md), aceptada. Medido (Soft, 12 dB de drive, 48 kHz): el aliasing del seno de 10 kHz depende del nivel, que el criterio no fijaba; ×4 da −131, −103 y −58 dB a −18, −6 y 0 dBFS (lo limita la curva, no los filtros), ×8, −130, −114 y −110; ADAA sin sobremuestreo, −46, −25 y −19, y pierde 8,3 dB a 18 kHz; ×2 con ADAA, −81 a −6 dBFS y −1,6 dB a 18 kHz; ×4 con ADAA, −79 a 0 dBFS y −0,38 dB. Decidido con el usuario: sobremuestreo hasta unos 384 kHz (×8 hasta 50 kHz, ×4 hasta 100 kHz y ×2 por encima), que cumple a cualquier nivel con el mismo aliasing y coste en todas las frecuencias de muestreo. Filtros: IIR pasa todo (8 y 6 coeficientes, −103 y −105 dB) en el modo IIR, sin latencia; FIR de 71 y 31 coeficientes (−106 y −110 dB) en los lineales, con 47 muestras de latencia hasta 50 kHz, 43 hasta 100 y 35 por encima. Warm con b = 0,25 (segundo armónico a −30,5 dB con 6 dB de drive a −18 dBFS); el bloqueador de continua en los dos modos (−192 dB tras 1 s); la compensación deja el seno de −18 dBFS a +0,0002 dB; la mezcla al 0 % se aparta menos de 0,001 dB hasta 18 kHz; rampas de 20 ms, el drive y la compensación en dB (en lineal, el nivel se pasaba durante la rampa); bloques de 1 a 4096 bit a bit. Coste del motor a 48 kHz en estéreo: del 0,7 al 1,3 % (GCC) y del 0,9 al 1,2 % (Clang), con una tanh propia sin ramas, los canales en el mismo bucle y los FIR por bloques (con la tanh de la biblioteca y sin eso, del 1,6 al 3 %). El plugin no cambia: bit a bit el de antes.
8. **6.8 Integración** ✅: los tres parámetros (`sat_mode`, `sat_drive` y `sat_mix`, índices 388 a 390, 391 en total, en los presets, el estado sin cambio de versión; `core::saturationSettings()`, y la mezcla escrita "100%"), `PhaseEqualizer` (`setSaturation()`, la saturación tras la dinámica espectral y antes de la ganancia de salida, con los filtros del modo que suena, la latencia sumada en fase lineal fuera de Off y el cambio con el silencio del cambio de modo, y el bypass con la señal seca retrasada), la cola del procesador, tests (`tests/dsp/phase_equalizer_test.cpp`, `tests/plugin/saturation_test.cpp` y los de `core`), `esceq_bench` (escenarios `saturacion` y `mezcla-sat`), `docs/arquitectura.md`, `docs/dsp/saturacion.md`, `docs/dsp/fase-lineal.md` y una nota en la ADR 0026 (sin decisiones nuevas: en IIR, encender y apagar es el fundido del motor, sin silencio ni latencia; en fase lineal, la latencia la enciende `sat_mode` fuera de Off, con cualquier mezcla). Medido: con Off, `PhaseEqualizer` es bit a bit el de antes, con la misma latencia, en los cuatro modos (comparado con el código anterior: 24 configuraciones con cambios de modo y bypass, a 44,1, 48 y 96 kHz); la latencia comunicada es la medida en los cuatro modos a 44,1, 48, 96 y 192 kHz (47, 47, 43 y 35 muestras más en fase lineal, 0 en IIR); la salida es bit a bit la de una `Saturation` sobre la del ecualizador sin ella; con la automatización, Soft deja el segundo armónico en −153 dB y Warm lo sube a −26 dB, y bajar el drive de 18 a 3 dB baja el tercero de −14 a −37 dB, sin chasquidos. Coste de la mezcla de 8 bandas con saturación a 48 kHz: del 1,45 al 1,65 % (GCC) y del 1,7 al 1,9 % (Clang) en los cuatro modos, del 0,7 al 1,0 % más que sin ella; a 192 kHz, del 2,8 al 4,4 %. El CHANGELOG lo recoge con la interfaz (6.9).
9. **6.9 Interfaz** ✅: decidido con el usuario, inspirado en la pestaña de ajustes avanzados de FabFilter Pro-L 2 y aprovechando para liberar la barra superior: un cajón plegable **Output** entre la curva y el medidor, en el orden de la señal, con la saturación (selector *Off/Soft/Warm*, *Drive* y *Mix*) arriba y la ganancia de salida (*Gain*, ahora un potenciómetro) y *Auto* abajo, que salen de la barra; abierto, 160 px; plegado, una pestaña de 16 con un punto si algo de dentro cambia el sonido; abierto por defecto, guardado con el proyecto (`ui.output_stage 0`, solo plegado) y oculto en la vista ampliada ([ADR 0027](docs/decisiones/0027-cajon-de-la-etapa-de-salida.md), nota en la ADR 0026). En `ui/logic`, la disposición (`outputDrawerLayout`, `editorLayout(..., drawerOpen)`) y los textos (`output_stage.h`: la ayuda del selector con la latencia en fase lineal, "adds 0.98 ms latency" a 48 kHz, 1.07 a 44,1, 0.45 a 96 y 0.18 a 192, y nada en IIR; *Drive* y *Mix* sin efecto con Off; lo que marca la pestaña), con sus tests (`tests/ui/output_stage_test.cpp`, `tests/ui/layout_test.cpp` y `tests/core/state_test.cpp`); `ui/src/OutputDrawer`; `esceq_snapshot --saturation` y `--drawer-closed`; `docs/ui.md` (con capturas), README (sección «Saturación»), `CHANGELOG.md` (la saturación entera) y `docs/arquitectura.md`. Medido: el cajón cabe en todos los tamaños, con y sin teclado de notas (celdas de 66 px o más); la curva mide al menos 868 px de ancho en la ventana mínima con el cajón abierto; el navegador de presets tiene su máximo (320 px) en todos los tamaños; el coste de pintar la curva no cambia (L, escala 1, mediana: de 8,8 a 9,1 ms abierto y de 8,9 a 10,3 plegado, dentro del ruido). El procesador no cambia: la interfaz solo mueve parámetros.

### 5.9 Automatización, controladores del host, macros y sidechain

**Planificado** (noviembre de 2026), para la fase 7, en seis bloques (al final de esta sección). Decidido con el usuario: el alcance es que los parámetros de siempre funcionen bien con los automation clips y los controladores de FL Studio (*Peak Controller*, *Formula Controller*, *Fruity LFO* y cualquier *Link to controller*), ocho macros para que un controlador mueva varios destinos a la vez y cuatro mejoras del sidechain: el enrutado en FL comprobado, la ganancia y el medidor del sidechain, escuchar el detector y un filtro del detector propio por banda. Queda fuera un modulador interno (seguidor de envolvente o LFO dentro de esceQ): solapa con los controladores del host. Diseño propuesto en la [ADR 0028](docs/decisiones/0028-automatizacion-macros-y-sidechain.md).

**Lo que hay** (comprobado contra el código): los 391 parámetros son parámetros del host, con nombre legible ("Band 3 Gain") y texto en su unidad ("1.20 kHz"), y los controles de la interfaz abren y cierran gestos, así que un automation clip, *Last tweaked* o *Link to controller* ya los mueven. Lo que falla con un controlador moviendo un parámetro sin parar:

- **Deshacer pisa la automatización.** `EditHistory` guarda instantáneas del estado entero, y deshacer vuelve a poner todos los parámetros que difieran: también los que está moviendo un clip o un controlador, que además el host puede grabar como un cambio del usuario.
- **La fase lineal rediseña el FIR en cada cambio.** En tiempo real, el hilo de control diseña el último ajuste cada 10 ms (de 0,2 a 13 ms por diseño) y cada FIR entra con un fundido de un bloque de convolución. Al renderizar, `ControlThread::flush()` diseña en **cada bloque** con cambios: con una banda modulada en Linear High, el render puede ir más lento que el tiempo real.
- **El sidechain no está comprobado en FL.** El bus "Sidechain" está desactivado por defecto, y "No sidechain" sale solo con el bus desactivado: con el bus activo y sin nada enrutado, el aviso no sale y la banda no actúa.

**Reglas** (las de la sección 5 y, además):

- Las macros **no escriben** en los parámetros que mueven: el valor que suena es el del parámetro más la modulación, y el host sigue viendo y automatizando el de base. Así una macro y un automation clip del mismo parámetro se suman, deshacer no se entera de la modulación y un proyecto sin macros suena igual.
- Solo se modulan los parámetros continuos que no cambian la estructura ni la latencia: frecuencia, ganancia, Q, umbral, rango, ataque, relajación y rodilla de cada banda; drive y mezcla de la saturación; ganancia de salida; y los nuevos de esta sección (ganancia del sidechain, frecuencia y Q del detector). No se modulan los interruptores ni las listas (tipo, pendiente, canal, dinámica, sidechain, *Auto*, *Spectral*, modo de fase, modo de saturación, bypass).
- La modulación se suma en el espacio normalizado del host (0 a 1, con la misma curva que el potenciómetro): una profundidad del 50 % recorre medio potenciómetro, y en la frecuencia, las mismas octavas en graves que en agudos.

**Parámetros nuevos**, al final de la tabla (índices 391 a 471, 472 en total), con un valor por defecto que no cambia el sonido:

| Parámetro | Valores | Por defecto | En los presets |
|---|---|---|---|
| `macro_1` a `macro_8` ("Macro 1"…) | 0 a 100 % | 0 % | Sí |
| `sc_gain` ("Sidechain Gain") | −24 a +24 dB | 0 dB (sin multiplicar: bit a bit) | Sí |
| `bNN_det_own` ("Band N Detector Filter") | Linked (el filtro de la banda) u Own | Linked | Sí |
| `bNN_det_freq` ("Band N Detector Frequency") | 20 Hz a 20 kHz | 1 kHz | Sí |
| `bNN_det_q` ("Band N Detector Q") | 0,3 a 16 | 1 | Sí |

**Las asignaciones de las macros** (qué parámetro mueve cada una y con qué profundidad, de −100 a +100 %; hasta 8 destinos por macro) no son parámetros: el host no debe automatizarlas. Van en el estado, en líneas `macro.N.M <id del destino> <profundidad>`, y suben el estado a la **versión 4** (cambian el sonido sin ser parámetros, como `filter_algorithm` en la 3: [ADR 0014](docs/decisiones/0014-estado-v3-algoritmo-de-filtro.md)). Una versión anterior abre el proyecto sin las macros. Los presets de usuario las guardan (un preset es el estado); los de fábrica no llevan ninguna, y cargar un preset cambia las asignaciones por las suyas (a confirmar con el usuario en el bloque 7.2). Cambiar una asignación es una acción de deshacer.

**Escuchar el detector** no es un parámetro ni se guarda: un botón *Listen* en la tarjeta de dinámica de la banda seleccionada sustituye la salida por lo que oye su detector (la entrada o el sidechain, tras su filtro, en el canal de la banda), con la latencia de lo que suena, para que no salte. Solo una banda a la vez, se apaga al cerrar la ventana o al seleccionar otra banda, y no actúa al renderizar sin tiempo real.

**El filtro del detector propio** (`bNN_det_own` en Own): el detector deja de usar el filtro de la banda (campana, paso bajo o paso alto según el tipo) y escucha un paso banda en `det_freq` con `det_q`. Sirve para bajar una zona cuando otra pasa del umbral (los graves de la mezcla cuando el bombo golpea a 60 Hz, las sibilantes a 9 kHz cuando la banda actúa a 6 kHz). En una banda espectral no se aplica (la máscara detecta bin a bin dentro de la banda): el control se ve desactivado. La curva marca la zona del detector con un trazo fino bajo la banda seleccionada.

**Sidechain:**

- *Enrutado en FL:* comprobar con el usuario cómo ve FL el bus (VST3 y CLAP) y si debe estar activo por defecto; el aviso pasa a tener tres estados: "No sidechain" (bus desactivado), "Sidechain silent" (activo y en silencio más de 1 s) y nada (llega señal). El README, con los pasos y los nombres reales de los menús de FL.
- *Ganancia y medidor:* `sc_gain` se aplica al sidechain antes de todos los detectores (y no a lo que aprende *Match*). Un medidor del nivel del sidechain (pico, con la escala del umbral) en la tarjeta de dinámica cuando la fuente es *External*, para ajustar el umbral viendo lo que llega.

**Automatización robusta:**

- *Deshacer por diferencias:* cada acción guarda los parámetros que tocaron sus gestos (y el algoritmo de filtro, y las asignaciones de las macros), con su valor de antes y de después; deshacer y rehacer cambian solo esos. Un parámetro que mueve el host no se toca.
- *Diseño al ritmo del control:* al renderizar sin tiempo real, el FIR y la compensación automática se rediseñan como mucho una vez por cada 10 ms de audio (el periodo del hilo de control), contados en muestras: el resultado sigue sin depender de la velocidad del render.
- *Documentación:* una sección del README, *Automatizar y controlar desde FL Studio*: automation clips, *Last tweaked*, *Link to controller* con el *Peak Controller*, las macros, y qué no conviene automatizar (el modo de fase, la primera banda espectral y la saturación en fase lineal, que dejan un hueco).

**Dónde vive:** las macros (asignaciones, la suma y el estado) en `core/macros.h` y `core/state`; el deshacer por diferencias en `core/history.h`; el sidechain, el detector propio y *Listen* en `dsp` (`designDetector`, `Equalizer`, `PhaseEqualizer`); el procesador aplica las macros al traducir los parámetros (`pushParametersToEngine`); la interfaz en `ui/logic` y `ui/src`. **Dónde se muestran las macros** se pregunta al usuario al empezar el bloque 7.3, con opciones (una fila de ocho potenciómetros plegable bajo la curva, una pestaña del cajón *Output* o un panel desde la barra superior) y la asignación con clic derecho en cualquier potenciómetro (*Link to Macro*), con un anillo que muestra la modulación.

**Aceptación**

| Criterio | Test |
|---|---|
| Un proyecto o preset anterior, sin tocar nada, suena bit a bit igual y con la misma latencia (472 parámetros en su valor por defecto, sin asignaciones) | `tests/plugin/` y `tests/core/release_contract_test.cpp` |
| Con el host moviendo un parámetro (sin gestos), deshacer una acción del usuario no lo cambia, y rehacer tampoco | `tests/core/history_test.cpp` y `tests/plugin/history_test.cpp` |
| Render de 60 s a 48 kHz con la ganancia de una banda modulada a 2 Hz en Linear High: dos renders bit a bit iguales, y menos del doble de tiempo que sin modular | `tests/plugin/` y `esceq_bench` |
| Una macro al 100 % con profundidad +50 % sobre una ganancia de −12 a +12 dB la lleva a 0 dB desde −12; el valor del parámetro en el host no cambia | `tests/core/macros_test.cpp` y `tests/plugin/` |
| Dos destinos de una macro y un automation clip en uno de ellos se suman y se limitan al rango | ídem |
| Un estado v3 carga sin asignaciones; uno v4 vuelve a escribirse idéntico; una versión anterior lo abre con todos sus parámetros | `tests/core/state_test.cpp` |
| `sc_gain` a +6 dB adelanta la reducción como un umbral 6 dB más bajo, con la misma forma | `tests/dsp/dynamics_test.cpp` |
| Con el detector propio en 60 Hz y la banda en 100 Hz, un tono de 60 Hz baja la banda y uno de 100 Hz por debajo del umbral no | ídem |
| *Listen* da, con la latencia de lo que suena, la señal del detector de la banda (bit a bit la de un `Equalizer` de solo detector), y no actúa al renderizar | `tests/dsp/` y `tests/plugin/` |
| "No sidechain" / "Sidechain silent" / nada, según el bus y la señal, contados en bloques (sin tiempos) | `tests/ui/` y `tests/plugin/` |
| Bloques de 1 a 4096 muestras, bit a bit iguales con macros moviéndose; sin asignaciones (`rtsan`) | `tests/plugin/` |
| Comprobado por el usuario en FL Studio: automation clip, *Peak Controller* enlazado a una ganancia y a una macro, y el sidechain enrutado en VST3 y CLAP | Lista de pruebas manuales |

**Bloques de implementación**

1. **7.1 Automatización robusta**: deshacer por diferencias (`core::UndoHistory` y `EditHistory`), el diseño al ritmo del control al renderizar, el escenario de modulación de `esceq_bench`, sus tests, el README (*Automatizar y controlar desde FL Studio*, sin las macros) y la ADR 0028 aceptada en lo que toca a este bloque.
2. **7.2 Macros, sin ventana**: los ocho parámetros, `core/macros.h` (asignaciones, la suma normalizada y los destinos válidos), el estado en la versión 4 con su migración y sus fixtures, los presets, la modulación en `pushParametersToEngine` (sin coste si no hay asignaciones), las asignaciones en el deshacer, y sus tests.
3. **7.3 Macros, interfaz**: dónde van (decidido con el usuario), *Link to Macro* en el menú de cada potenciómetro y del nodo, el anillo de modulación, la curva con los valores que suenan (el nodo en el de base), los textos y la ayuda, `esceq_snapshot --macros`, `docs/ui.md`, README y CHANGELOG.
4. **7.4 Sidechain en FL**: comprobado con el usuario el enrutado en FL (VST3 y CLAP, mono y estéreo, activo por defecto o no), los tres estados del aviso, `sc_gain` y el medidor del sidechain, README con los pasos reales, y sus tests.
5. **7.5 Filtro del detector propio**: los 72 parámetros, el paso banda del detector en `dsp` (IIR y fase lineal, y desactivado en las bandas espectrales), la zona del detector en la curva, los controles en la tarjeta de dinámica (al pasar a Own, la interfaz copia la frecuencia y el Q de la banda, en la misma acción), `docs/dsp/dinamica.md`, y sus tests.
6. **7.6 Escuchar el detector**: *Listen* en `dsp` (la señal del detector con la latencia de lo que suena) y en el procesador (una banda, apagado al renderizar), el botón en la tarjeta de dinámica, la ayuda, y sus tests. El CHANGELOG de la fase.

Cada bloque deja el plugin publicable: los parámetros de un bloque no se añaden sin su efecto. Al terminar la fase, una versión (0.6.0) con su contrato `release_0.6*`.

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
| **4. Primera versión pública (0.1)** ✅ | M/S y L/R por banda, interfaz con controles propios, presets, deshacer, CLAP, instalador de Windows y releases desde el CI | Release en GitHub con binarios (la 0.1.0 no se publicó: su contenido sale en la 0.5.0) | 3 a 4 semanas |
| **5. Calidad** ✅ | Corrección en agudos (hecha, ver [ADR 0013](docs/decisiones/0013-correccion-en-agudos.md)), fase lineal (hecha, ver [ADR 0012](docs/decisiones/0012-modos-de-fase.md)), compensación automática de ganancia (hecha, ver [ADR 0015](docs/decisiones/0015-compensacion-automatica-de-ganancia.md)), tiempos automáticos en la dinámica (hechos, ver [ADR 0016](docs/decisiones/0016-tiempos-automaticos-en-la-dinamica.md)), escala de notas (hecha, ver [ADR 0017](docs/decisiones/0017-escala-de-notas.md)), analizador ampliado (hecho, ver [ADR 0018](docs/decisiones/0018-analizador-ampliado.md)), bandas desde el espectro (hechas, ver [ADR 0019](docs/decisiones/0019-bandas-desde-el-espectro.md)), optimización SIMD (medida: no compensa y no se hace, ver [ADR 0020](docs/decisiones/0020-sin-simd.md)), macOS y Linux (hechos, ver [ADR 0022](docs/decisiones/0022-compatibilidad-en-macos-y-linux.md)) | Versión 0.5.0, la primera pública (lista para etiquetar) | 8 a 12 semanas |
| **6. Avanzado** | En este orden, cada bloque en un chat: igualar a una referencia (5.6, [ADR 0023](docs/decisiones/0023-igualar-a-una-referencia.md): 6.1 ajuste en `dsp` ✅, 6.2 aprender y aplicar sin ventana ✅, 6.3 interfaz ✅, y corregido el defecto de las bandas estrechas que ya hay ✅), dinámica espectral (5.7, [ADR 0024](docs/decisiones/0024-dinamica-espectral.md) y [0025](docs/decisiones/0025-dinamica-espectral-en-el-procesador.md): 6.4 motor ✅, 6.5 integración ✅, 6.6 interfaz ✅), saturación (5.8, [ADR 0026](docs/decisiones/0026-saturacion.md) y [0027](docs/decisiones/0027-cajon-de-la-etapa-de-salida.md): 6.7 motor y ADR ✅, 6.8 integración ✅, 6.9 interfaz ✅) y surround (sin planificar). Hecha y adelantada: la detección de colisiones entre instancias (ver [ADR 0011](docs/decisiones/0011-espectros-compartidos.md)) | Versión 1.0 | Abierto |
| **7. Integración con el host** | Automatización y controladores del host (5.9, [ADR 0028](docs/decisiones/0028-automatizacion-macros-y-sidechain.md), propuesta): 7.1 automatización robusta, 7.2 macros sin ventana, 7.3 macros en la interfaz, 7.4 sidechain en FL, ganancia y medidor, 7.5 filtro del detector propio, 7.6 escuchar el detector; cada bloque en un chat | Versión 0.6.0; comprobado por el usuario en FL Studio | 6 bloques |

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
