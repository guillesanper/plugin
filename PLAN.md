# esceQ: plan del proyecto

Plugin de ecualización paramétrica y dinámica, libre, pensado para funcionar bien en FL Studio y con un flujo de trabajo comparable al de los ecualizadores comerciales de referencia (FabFilter Pro-Q).

Estado: **fases 0 a 3 completadas; fase 4 lista para publicar.** Todo lo de la 0.1 está hecho y el CI lo compila, valida y empaqueta en Windows, macOS y Linux. Falta la revisión en FL Studio con una mezcla real (incluido el enrutado del sidechain) y subir la etiqueta `v0.1.0`, que publica la release.

---

## 1. Objetivo y alcance

### Qué queremos construir

- EQ paramétrico de hasta 24 bandas con interfaz de arrastre directo sobre la curva.
- Cada banda puede ser **dinámica**: su ganancia se mueve según el nivel de la señal en esa zona del espectro (compresión o expansión por banda).
- Analizador de espectro en tiempo real (pre, post y sidechain).
- Procesado por banda en estéreo, L, R, Mid o Side.
- Sidechain externo.
- Modos de fase: latencia cero (fase mínima) primero; fase lineal más adelante.

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
├── dsp/            Biblioteca C++20 pura. Sin JUCE, sin asignaciones en tiempo real.
│   ├── filters/    SVF, cascadas, cálculo de coeficientes, respuesta en frecuencia
│   ├── dynamics/   Detector, seguidor de envolvente, curva de ganancia
│   ├── analysis/   FFT, ventanas, suavizado del espectro
│   └── phase/      Fase lineal (FIR / FFT)
├── core/           Modelo de parámetros, estado y serialización. Sin JUCE.
├── plugin/         Capa JUCE: AudioProcessor, parámetros, estado, presets
├── ui/             Componentes: curva, nodos, analizador, panel de banda
├── tests/          Tests unitarios y de respuesta (Catch2)
├── bench/          Medidas de rendimiento
│   └── fixtures/   Estados guardados y salidas de referencia de versiones anteriores
└── docs/           arquitectura.md, decisiones/ (ADR), dsp/
```

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
- **Curva dibujada:** se calcula analíticamente a partir de los coeficientes, no midiendo el audio.

### 4.2 Banda dinámica

Cadena por banda:

1. **Detector:** la señal (interna o sidechain) pasa por un band-pass centrado en la frecuencia de la banda, con su mismo Q.
2. **Envolvente:** seguidor de nivel con ataque y relajación (pico o RMS).
3. **Curva de ganancia:** umbral, rango máximo (positivo expande, negativo comprime) y rodilla suave. Opción de umbral automático.
4. **Aplicación:** la ganancia resultante se suma a la ganancia estática de la banda.

Para campanas y shelves conviene la forma en paralelo `salida = entrada + (g − 1) · filtro(entrada)`: cambiar la ganancia es multiplicar por un escalar, sin recalcular coeficientes en cada muestra.

### 4.3 Analizador

FFT de 4096 a 8192 puntos, ventana de Hann, solapamiento del 75 %, inclinación configurable (4,5 dB/oct por defecto), suavizado por fracciones de octava, caída temporal ajustable. El cálculo de la FFT se hace en el hilo de la interfaz, no en el de audio.

### 4.4 Fase lineal (fase 5)

FIR generado a partir de la respuesta en magnitud y aplicado por convolución particionada con FFT. Latencia según resolución (baja, media, alta). Combinarlo con bandas dinámicas es el punto difícil: una vía es aplicar la parte dinámica como diferencia sobre el FIR estático. Hay que investigarlo antes de comprometerse.

### 4.5 Reglas de tiempo real

Sin `new`/`malloc`, sin mutex, sin llamadas al sistema en el hilo de audio; denormales desactivados; suavizado en todos los parámetros audibles; SIMD solo después de medir.

---

## 5. Herramientas, pruebas y distribución

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

## 6. Hoja de ruta

Estimaciones para una persona a tiempo parcial. Son orientativas.

| Fase | Contenido | Criterio de salida | Duración |
|---|---|---|---|
| **0. Base** ✅ | Estructura del repo, CMake, JUCE, CI, plugin vacío que pasa audio; `docs/arquitectura.md`, primeros ADR, `CONTRIBUTING.md` | Carga en FL Studio y pasa `pluginval` | 1 a 2 semanas |
| **1. Núcleo de filtros** ✅ | SVF, todos los tipos y pendientes, cálculo de la curva, tests | Respuesta medida = respuesta teórica (±0,1 dB) | 2 a 3 semanas |
| **2. EQ estático** ✅ | 24 bandas (adelantadas de la fase 4, ver ADR 0005), parámetros, estado, interfaz con curva y nodos arrastrables, analizador | Se puede mezclar una pista real con él | 4 a 6 semanas |
| **3. Dinámica** ✅ | Detector, envolvente, curva de ganancia, indicación visual de la ganancia dinámica, sidechain externo (ver ADR 0006) | Sin chasquidos bajo modulación rápida; de-esser y control de resonancias usables | 3 a 4 semanas |
| **4. Primera versión pública (0.1)** | M/S y L/R por banda, interfaz con controles propios, presets, deshacer, CLAP, instalador de Windows y releases desde el CI | Release en GitHub con binarios | 3 a 4 semanas |
| **5. Calidad** | Corrección en agudos, fase lineal, auto-gain, optimización SIMD, macOS y Linux | Versión 0.5 | 6 a 10 semanas |
| **6. Avanzado** | Dinámica espectral, EQ match, detección de colisiones entre instancias, surround | Versión 1.0 | Abierto |

---

## 7. Riesgos

| Riesgo | Mitigación |
|---|---|
| La interfaz consume más tiempo que el DSP | Empezar con una interfaz funcional y fea; pulir al final de cada fase, no antes |
| Artefactos al modular filtros | SVF desde el principio y tests específicos de modulación en la fase 1 |
| Fase lineal + dinámica resulta inviable con buena calidad | Está en la fase 5, fuera del camino crítico; se puede publicar sin ello |
| Duplicar un proyecto que ya existe | Elegir un diferenciador antes de la fase 2: núcleo DSP reutilizable, flujo de trabajo en FL Studio, o funciones poco cubiertas (fase lineal con dinámica, dinámica espectral) |
| Un cambio rompe proyectos guardados o altera el sonido | Contratos y tests de la sección 2.1, verificados en CI |
| Romper proyectos de usuarios al cambiar parámetros | Congelar identificadores y versionar el estado guardado desde la 0.1 |
| Problemas de marca | Nombre e identidad visual propios; ninguna referencia a marcas ajenas en el producto |

---

## 8. Referencias

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
