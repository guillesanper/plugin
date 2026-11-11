# Plan: ecualizador dinámico open source

Plugin de ecualización paramétrica y dinámica, libre, pensado para funcionar bien en FL Studio y con un flujo de trabajo comparable al de los ecualizadores comerciales de referencia (FabFilter Pro-Q).

Estado: **borrador para debatir**. Las decisiones marcadas como *abiertas* en la sección 2 hay que cerrarlas antes de escribir código.

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

## 2. Decisiones abiertas (a debatir)

### 2.1 ¿Empezar de cero o partir de algo existente?

Ya existe un proyecto muy parecido a lo que describimos: **ZL Equalizer** (ZL-Audio), EQ dinámico libre, AGPLv3, hecho con JUCE, con versión 1.x publicada. Antes de invertir meses conviene decidir cuál es nuestra razón de ser:

| Opción | A favor | En contra |
|---|---|---|
| **A. De cero** | Aprendizaje completo, arquitectura y licencia a nuestra elección, identidad propia | Meses hasta igualar lo que ya existe |
| **B. Contribuir a ZL Equalizer** | Impacto inmediato, base madura | No es "nuestro" plugin; hay que aceptar su diseño |
| **C. Fork** | Arranque rápido | Heredamos AGPLv3 y una base de código grande que no conocemos; su logo y nombre no se pueden reutilizar |

**Recomendación:** A, pero leyendo ZL Equalizer como referencia de arquitectura y fijando un diferenciador claro. Candidatos a diferenciador:

1. Núcleo DSP como biblioteca independiente y bien testeada, reutilizable fuera del plugin.
2. Flujo de trabajo e interfaz más pulidos para producción en FL Studio.
3. Funciones que las alternativas libres cubren poco: fase lineal con bandas dinámicas, dinámica espectral, detección de colisiones entre pistas.

Si el objetivo principal es aprender DSP y desarrollo de plugins, A es la respuesta sin más discusión.

### 2.2 Framework

| Framework | Lenguaje | Licencia | Formatos | GUI | Valoración |
|---|---|---|---|---|---|
| **JUCE 8** | C++ | AGPLv3 o comercial | VST3, AU, AAX, LV2, standalone; CLAP con `clap-juce-extensions` | Completa (software, OpenGL, Direct2D) | El estándar de facto. Más documentación, ejemplos y herramientas que ningún otro |
| **CLAP nativo + clap-wrapper** | C/C++ | MIT | CLAP, y VST3/AU envolviendo | Ninguna: hay que traerla (VSTGUI, Visage, Dear ImGui, propia) | Máxima libertad de licencia; mucho más trabajo en GUI y en fontanería |
| **DPF** | C++ | ISC | LV2, VST2, VST3, CLAP, standalone | Básica (OpenGL, NanoVG) | Ligero y permisivo; GUI pobre para una interfaz tan rica como esta |
| **iPlug2** | C++ | Permisiva (tipo zlib) | VST3, AU, AAX, CLAP, Web | NanoVG o Skia | Buena opción intermedia; comunidad y documentación menores |
| **nih-plug** | Rust | ISC | VST3, CLAP | egui, iced, VIZIA | Muy agradable y seguro; sin AU, ecosistema de GUI menos maduro, desarrollo del framework lento |

**Recomendación:** JUCE 8 bajo AGPLv3, con una condición de arquitectura: el DSP vive en una biblioteca C++20 **sin ninguna dependencia de JUCE**. Así el framework queda confinado a la capa de plugin y de interfaz, y si algún día queremos cambiarlo o relicenciar el núcleo, se puede.

Motivos: la interfaz (curva interactiva, analizador, muchas bandas) es la mitad del proyecto y JUCE es donde menos hay que inventar; VST3 en FL Studio vía JUCE está probadísimo; y `pluginval` se integra directamente.

El coste: el plugin entero queda obligado a AGPLv3 (ver 2.3).

### 2.3 Licencia

- Con JUCE sin pagar licencia comercial, el plugin debe ser **AGPLv3**.
- El SDK de VST3 es **MIT** desde la versión 3.8 (octubre de 2025), así que ya no impone GPLv3 por su lado. CLAP es MIT.
- Propuesta: plugin en AGPLv3; biblioteca DSP en el mismo repo pero con licencia propia más permisiva (MIT) si queremos que otros la reutilicen. Es compatible: código MIT puede integrarse en una obra AGPLv3.

*Abierto:* ¿nos importa que el núcleo DSP sea permisivo, o todo AGPLv3 y listo?

### 2.4 Nombre

Hace falta un nombre propio que no recuerde a marcas existentes. Pendiente de proponer y comprobar que no está usado en KVR, GitHub ni registros de marca.

### 2.5 Alcance del MVP

Propuesta de primera versión utilizable (ver fases 0 a 3 en la sección 6): 8 bandas, tipos bell / shelf / corte / notch, dinámica por banda, analizador, VST3 en Windows. Todo lo demás, después.

*Abierto:* ¿la dinámica entra en el MVP o primero se publica un EQ estático sólido? Recomiendo estático primero (fase 2) y dinámica justo después (fase 3): la dinámica depende de que los filtros modulen sin artefactos, y eso se valida mejor por separado.

---

## 3. Arquitectura

```
plugin/
├── dsp/            Biblioteca C++20 pura. Sin JUCE, sin asignaciones en tiempo real.
│   ├── filters/    SVF, cascadas, cálculo de coeficientes, respuesta en frecuencia
│   ├── dynamics/   Detector, seguidor de envolvente, curva de ganancia
│   ├── analysis/   FFT, ventanas, suavizado del espectro
│   └── phase/      Fase lineal (FIR / FFT)
├── plugin/         Capa JUCE: AudioProcessor, parámetros, estado, presets
├── ui/             Componentes: curva, nodos, analizador, panel de banda
├── tests/          Tests unitarios y de respuesta (Catch2)
├── bench/          Medidas de rendimiento
└── docs/           Notas de diseño y referencias DSP
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
| **0. Base** | Estructura del repo, CMake, JUCE, CI, plugin vacío que pasa audio | Carga en FL Studio y pasa `pluginval` | 1 a 2 semanas |
| **1. Núcleo de filtros** | SVF, todos los tipos y pendientes, cálculo de la curva, tests | Respuesta medida = respuesta teórica (±0,1 dB) | 2 a 3 semanas |
| **2. EQ estático** | 8 bandas, parámetros, estado, interfaz con curva y nodos arrastrables, analizador | Se puede mezclar una pista real con él | 4 a 6 semanas |
| **3. Dinámica** | Detector, envolvente, curva de ganancia, indicación visual de la ganancia dinámica, sidechain externo | Sin chasquidos bajo modulación rápida; de-esser y control de resonancias usables | 3 a 4 semanas |
| **4. Primera versión pública (0.1)** | 24 bandas, M/S y L/R por banda, presets, deshacer, instalador Windows, CLAP | Release en GitHub con binarios | 3 a 4 semanas |
| **5. Calidad** | Corrección en agudos, fase lineal, auto-gain, optimización SIMD, macOS y Linux | Versión 0.5 | 6 a 10 semanas |
| **6. Avanzado** | Dinámica espectral, EQ match, detección de colisiones entre instancias, surround | Versión 1.0 | Abierto |

---

## 7. Riesgos

| Riesgo | Mitigación |
|---|---|
| La interfaz consume más tiempo que el DSP | Empezar con una interfaz funcional y fea; pulir al final de cada fase, no antes |
| Artefactos al modular filtros | SVF desde el principio y tests específicos de modulación en la fase 1 |
| Fase lineal + dinámica resulta inviable con buena calidad | Está en la fase 5, fuera del camino crítico; se puede publicar sin ello |
| Duplicar un proyecto que ya existe | Cerrar el diferenciador (2.1) antes de la fase 2 |
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
