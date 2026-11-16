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
