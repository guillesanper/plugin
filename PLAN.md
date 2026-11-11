# esceQ: plan del proyecto

Plugin de ecualización paramétrica y dinámica, libre, pensado para funcionar bien en FL Studio y con un flujo de trabajo comparable al de los ecualizadores comerciales de referencia (FabFilter Pro-Q).

Estado: **decisiones principales cerradas** (sección 2). Siguiente paso: fase 0.

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
| Framework | JUCE 8, confinado a las capas de plugin e interfaz |
| Licencia | Plugin en AGPLv3; biblioteca DSP en MIT |
| Primera versión | EQ estático sólido primero (fase 2), dinámica justo después (fase 3) |
| Principio rector | **Todo lo anterior debe poder cambiarse en el futuro** (ver 2.1) |

Pendiente: comprobar que "esceQ" no está en uso en KVR, GitHub ni registros de marca antes de la primera versión pública.

### 2.1 Diseñar para poder cambiar

Las decisiones de framework y licencia son las más caras de revertir. Reglas para que sigan siendo reversibles:

**Framework**
- `dsp/` es C++20 puro: no incluye ninguna cabecera de JUCE ni de ningún SDK de plugin. El CI lo compila y testea por separado, sin JUCE presente, para que la regla no se rompa por descuido.
- Entre el DSP y el framework hay una capa fina propia (`core/`): modelo de parámetros, estado y serialización definidos con tipos nuestros. La capa JUCE solo traduce entre ese modelo y el host.
- El formato del estado guardado es nuestro (versionado, independiente de las utilidades de JUCE), de modo que un proyecto guardado hoy se pueda abrir aunque mañana el plugin esté hecho con otro framework.
- Los identificadores de parámetro son estables y no dependen de cómo los numere el framework.
- La lógica de la interfaz (qué nodo está seleccionado, cómo se convierte un arrastre en frecuencia y ganancia, escalas de los ejes) va separada del pintado, para que cambiar de biblioteca gráfica suponga reescribir solo el dibujo.

**Licencia**
- Cambiar la licencia exige el permiso de todos los titulares de derechos. Mientras el autor sea uno, es trivial; con contribuciones externas deja de serlo.
- Por eso, desde el primer PR externo se pedirá un acuerdo de contribución (CLA) que permita relicenciar. Sin él, la licencia queda congelada de facto.
- El DSP en MIT ya puede usarse en cualquier contexto. Lo único atado a AGPLv3 es lo que enlaza con JUCE: si se sustituye JUCE o se adquiere su licencia comercial, el plugin puede cambiar de licencia.
- Cada archivo lleva su identificador SPDX para que quede claro qué licencia aplica a qué.
- No se copia código de proyectos AGPL/GPL (incluido ZL Equalizer) dentro de `dsp/`: contaminaría la parte MIT.

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
| Duplicar un proyecto que ya existe | Elegir un diferenciador antes de la fase 2: núcleo DSP reutilizable, flujo de trabajo en FL Studio, o funciones poco cubiertas (fase lineal con dinámica, dinámica espectral) |
| Quedar atados a JUCE o a AGPLv3 | Reglas de la sección 2.1, verificadas en CI |
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
