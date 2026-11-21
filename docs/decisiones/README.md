# Registro de decisiones

Una nota corta por cada decisión de diseño importante: qué se decidió, por qué y qué alternativas había. Sirve para no deshacer por descuido algo que tenía un motivo.

- Las notas se numeran y **no se reescriben**. Si una decisión cambia, se añade una nota nueva que sustituye a la anterior y se marca la antigua como "Sustituida por NNNN".
- Plantilla: Estado · Contexto · Decisión · Consecuencias.

| N.º | Decisión | Estado |
|---|---|---|
| [0001](0001-juce-con-dsp-independiente.md) | JUCE como framework, con el DSP independiente de él | Aceptada |
| [0002](0002-licencias.md) | Plugin en AGPLv3, DSP en MIT | Aceptada |
| [0003](0003-formato-de-estado.md) | Formato de estado propio y versionado | Aceptada |
| [0004](0004-parametros-congelados.md) | Parámetros en una tabla única, con identificadores congelados | Aceptada |
| [0005](0005-bandas-y-normalizacion.md) | 24 bandas declaradas desde ya, y normalización definida en core | Aceptada |
| [0006](0006-dinamica-por-banda.md) | Dinámica por banda: detección en la entrada, curva 1:1 con rango y ganancia evaluada en cada muestra | Aceptada |
| [0007](0007-canal-por-banda.md) | Canal por banda: Mid/Side por diferencia, detección que sigue al canal | Aceptada |
| [0008](0008-sistema-visual.md) | Sistema visual propio: tema en código, controles propios y lógica fuera de la capa gráfica | Aceptada |
| [0009](0009-preferencias-deshacer-y-presets.md) | Estado versión 2 con preferencias de la interfaz, deshacer por instantáneas y presets en el formato de estado | Aceptada |
| [0010](0010-distribucion.md) | Distribución: CLAP con clap-juce-extensions, instalador de Windows y releases desde el CI | Aceptada |
| [0011](0011-espectros-compartidos.md) | Espectros compartidos entre instancias del mismo proceso y colisiones como enmascaramiento direccional | Aceptada |
| [0012](0012-modos-de-fase.md) | Modos de fase: IIR y fase lineal (FIR por muestreo en frecuencia, convolución particionada, dinámica como diferencia sobre el FIR, diseño en otro hilo) | Aceptada |
| [0013](0013-correccion-en-agudos.md) | Corrección en agudos: algoritmo de filtro Matched (Vicanek), con Classic intacto y la versión guardada por instancia | Aceptada |
| [0014](0014-estado-v3-algoritmo-de-filtro.md) | Estado versión 3: el algoritmo de filtro de la instancia, una línea del estado (no un parámetro) que se deshace y que los presets no cambian | Aceptada |
| [0015](0015-compensacion-automatica-de-ganancia.md) | Compensación automática de ganancia: curva estática ponderada con una rejilla refinada, calculada en un hilo de control, y un parámetro fuera de los presets | Aceptada |
| [0016](0016-tiempos-automaticos-en-la-dinamica.md) | Tiempos automáticos en la dinámica: ataque ligado al periodo, relajación de dos etapas (una lenta que promedia la rápida) y un interruptor por banda dentro de los presets | Aceptada |
| [0017](0017-escala-de-notas.md) | Escala de notas: lógica en ui/logic, dos preferencias de la interfaz (`ui.notes`, `ui.a4`) que solo se escriben si no son las de por defecto, y el teclado y la afinación en el menú de opciones | Aceptada |
| [0018](0018-analizador-ampliado.md) | Analizador ampliado: vista sin panel ni medidor con un botón junto al selector del analizador, ajustes en el menú de opciones y cuatro preferencias (`ui.analyzer_*`) que solo se escriben si no son las de por defecto | Aceptada |
| [0019](0019-bandas-desde-el-espectro.md) | Bandas desde el espectro: picos buscados en un promedio de potencia de 1 s y sin la inclinación, Alt convierte la ganancia arrastrada en rango, y el gesto entero es una acción de deshacer | Aceptada |
| [0020](0020-sin-simd.md) | Optimización SIMD: medido el hilo de audio, no compensa y no se optimiza; queda la herramienta de medida `esceq_bench` | Aceptada |
| [0021](0021-aviso-de-latencia-sin-temporizador.md) | Aviso de latencia sin temporizador (lo da el hilo de control) y pluginval 1.0.4 compilado en Linux con el arreglo de JUCE 9.0.2 para su host VST3 | Aceptada |
| [0022](0022-compatibilidad-en-macos-y-linux.md) | Compatibilidad en macOS y Linux: Linux compilado en un contenedor de Ubuntu 22.04 (glibc 2.35) y comprobado, paquetes de macOS firmados ad hoc y comprobados en los tres formatos, sin `.pkg` ni notarización, y LV2 aplazado | Aceptada |
| [0023](0023-igualar-a-una-referencia.md) | Igualar a una referencia: la entrada de la pista contra la salida de la referencia medidas igual, ajuste voraz y Levenberg-Marquardt de lo que falta sobre la curva actual en un hilo propio, nada guardado, y el panel en el menú de comparar | Aceptada |
| [0024](0024-dinamica-espectral.md) | Dinámica espectral: una STFT de 2048 puntos compartida tras el ecualizador, máscara por bin con los controles de la banda en lugar de su dinámica, y latencia solo mientras haya una banda espectral | Propuesta |
