# 0014. Estado versión 3: el algoritmo de filtro de la instancia, una línea del estado que se deshace

**Estado:** Aceptada (noviembre de 2026). Desarrolla el punto 4 de la [0013](0013-correccion-en-agudos.md) y amplía la [0009](0009-preferencias-deshacer-y-presets.md).

## Contexto

La [ADR 0013](0013-correccion-en-agudos.md) decidió, con el autor del proyecto, que cada instancia tiene una versión de algoritmo de filtro (`dsp::FilterAlgorithm`) guardada en el estado: lo guardado sin ella carga en Classic y suena bit a bit igual, una instancia nueva arranca en Matched, los presets no la cambian y un menú de la interfaz la elige, con deshacer. Al implementarlo quedaban por decidir:

1. Si es un parámetro del host o un dato del estado que no es parámetro.
2. Cómo se migra, y qué pasa con un valor que esta versión no conoce.
3. Cómo entra en el deshacer, que hasta ahora eran instantáneas de los parámetros.
4. Dónde va el menú, en una barra superior que ya estaba llena.

## Decisión

1. **Una línea del estado, `filter_algorithm N`, y no un parámetro ni una preferencia `ui.*`.** `N` es el valor numérico de `dsp::FilterAlgorithm` (contrato: 0 Classic, 1 Matched; los nuevos, al final). Va después de los parámetros y antes de las líneas `ui.*`, y vive en `core::PluginState::filterAlgorithm`. El modo de fase sí es un parámetro ([ADR 0012](0012-modos-de-fase.md)), pero aquí un parámetro encaja mal:
   - *Un parámetro tiene un solo valor por defecto, y aquí hacen falta dos:* Classic para lo que se guardó sin él y Matched para lo nuevo. Con el valor por defecto en Classic, para que los proyectos antiguos sonaran igual, la instancia nueva tendría que forzar Matched al crearse, y la orden "volver al valor por defecto" del host (o un host que restaure los parámetros por su cuenta) la devolvería a Classic sin que nadie lo pida. Con el valor por defecto en Matched, todos los proyectos antiguos cambiarían de sonido. El modo de fase no tiene este problema: IIR vale para lo antiguo y para lo nuevo.
   - *El host lo automatizaría y lo mostraría* en la lista de parámetros de cada pista. Cambiar de algoritmo es un fundido de 10 ms en todas las bandas: automatizarlo no tiene ningún uso.
   - *Ocuparía para siempre un identificador* de la tabla congelada (contrato 1 del plan).
   - *No es `ui.*`:* es sonido. Las preferencias `ui.*` se añaden sin subir de versión, y aquí la ausencia de la línea tiene que significar Classic en lo antiguo y no en lo nuevo: es un cambio de significado, que exige versión y migración ([ADR 0009](0009-preferencias-deshacer-y-presets.md)).

   El precio de no ser parámetro es que el host no se entera de los cambios. `PluginProcessor::setFilterAlgorithm` lo avisa con `updateHostDisplay(...withNonParameterStateChanged(true))`, para que marque el proyecto como modificado; cargar un estado no avisa, porque es el que acaba de dar el host. Y el deshacer tiene que incluirlo (punto 3).

2. **Formato del estado en la versión 3, con su migración.**
   - **Migración 2 → 3:** añade `filter_algorithm 0` al final de las líneas de un estado anterior. Un estado de las versiones 1 o 2, que son todos los de la 0.1, carga en Classic. Si una clave se repite cuenta la última, así que una línea `filter_algorithm` en un estado que dice ser anterior, que no escribió esceQ, queda anulada.
   - **En la versión 3,** la línea se escribe siempre. Si falta (un estado escrito a mano), toma el valor por defecto, Matched, como cualquier dato ausente.
   - **Un valor desconocido** (un algoritmo de una versión más nueva) **o ilegible carga en Matched,** el más reciente que esta versión conoce: lo más parecido a un algoritmo posterior, y sin dejar de cargar el proyecto. Tampoco se escribe nunca un valor que no exista.
   - **Compatibilidad hacia delante:** una versión anterior de esceQ ignora la línea (regla "lo desconocido se ignora") y suena en Classic, el único algoritmo que tiene. Un proyecto guardado en Matched suena distinto en la 0.1; es lo mismo que pasa con cualquier función nueva abierta en una versión que no la tiene.
   - **Fixtures nuevos:** `tests/fixtures/state/v3_matched.txt` (un proyecto nuevo, en Matched y en fase lineal Low) y `v3_classic.txt` (el proyecto de referencia de la 0.1 guardado otra vez: los mismos valores, en Classic). Los de las versiones 1 y 2 no se tocan y siguen cargando, ahora en Classic.

3. **El deshacer guarda instantáneas de los parámetros y del algoritmo.** `core::EditState` reúne los dos, y `core::UndoHistory` guarda instantáneas de `EditState` en lugar de solo los parámetros. Cambiar el algoritmo desde el menú (`PluginProcessor::changeFilterAlgorithm`) es una acción propia: si había un gesto esperando su registro (los 300 ms de agrupación), se registra antes por separado (`EditHistory::flushPending`), y deshacer el algoritmo no deshace el arrastre de justo antes. Elegir el que ya suena no crea una acción. Deshacer o rehacer aplica los parámetros y el algoritmo, y avisa al host si el algoritmo cambia. Cargar un proyecto sigue borrando el historial. Se descartaron:
   - *Un historial aparte para el algoritmo:* dos historiales no se pueden deshacer en el orden en que se hicieron las cosas.
   - *Convertirlo en un gesto de un parámetro oculto:* tendría todos los problemas del punto 1.

4. **Los presets no pueden cambiarlo, por construcción.** `core::serializePreset`, `core::applyPreset` y `serializeState` trabajan con `ParameterValues`, que no lo contiene. Un test comprueba que un preset de fábrica, uno de usuario o los valores por defecto no lo cambian en ninguno de los dos sentidos, que el archivo de un preset no lo lleva y que cambiarlo no marca el preset como modificado.

5. **Todas las bandas usan el de la instancia.** `core::bandSettings(values, band, algorithm)` lo pone en el `FilterParams` de cada banda. El procesador lo lee de un atómico una vez por bloque, junto a los parámetros, y solo vuelve a pasar las bandas al ecualizador si algo ha cambiado. La curva lo lee en cada refresco y dibuja con `ui::soundingBandSettings`, la misma traducción que el procesador más la ganancia dinámica, así que la curva sigue siendo lo que suena. La fase lineal lo hereda de los `FilterParams` de las bandas, y la detección de la dinámica también.

6. **Un botón de opciones en la barra superior, con un menú "HF accuracy".** Es un botón con icono (tres deslizadores), entre Compare y el modo de fase, que abre un menú con la sección *HF accuracy* y dos opciones: *Classic (as in esceQ 0.1)* y *Matched (analog-accurate highs)*, con la que suena marcada. Su ayuda dice cuál suena ("Options · HF accuracy: Matched"). Los textos y la correspondencia entre entradas y algoritmos están en `ui/logic` (`options_menu.h`), con tests. Se descartaron:
   - *Un desplegable "HF: Matched":* necesita unos 110 px, el botón 28. El menú, además, tiene sitio para otras opciones de la instancia.
   - *Meterlo en el desplegable del modo de fase:* ese control está enlazado a un parámetro y sus opciones son las del parámetro.
   - *Meterlo en el menú de presets:* daría a entender que es parte del preset, y es justo lo contrario.

   El botón y su separación ocupan 36 px. Para no estrechar el navegador de presets por debajo de 140 px, el ancho mínimo de la ventana pasa de 1080 a 1120 px (1104 es el mínimo exacto; 1120 deja margen y sigue la rejilla), y el tamaño S, de 1080 × 580 a 1120 × 580. Una ventana guardada más estrecha abre con el ancho mínimo.

## Consecuencias

- El contrato de estado tiene tres versiones con fixtures: 1, 2 y 3. Los tests que comprueban que un fixture se vuelve a escribir idéntico lo hacen mientras `kStateVersion` sea la suya; desde la 3, el de la 0.1 tiene su equivalente en `v3_classic.txt`.
- Un proyecto de la 0.1 abre en Classic y suena exactamente igual: lo comprueban el contrato de la 0.1 (que no cambia), un test de core (las bandas que salen del estado son las de entonces) y uno del plugin (el procesador que carga ese proyecto suena como `dsp::Equalizer` con las bandas de la 0.1, con una diferencia máxima de 10⁻⁶).
- La única forma de pasar a Matched un proyecto antiguo es el menú. Un proyecto nuevo, en cambio, empieza en Matched sin hacer nada.
- Coste en el hilo de audio: una lectura atómica y una comparación por bloque. Cambiar de algoritmo vuelve a pasar las 24 bandas al ecualizador, como cualquier cambio de parámetro, y hace el fundido de 10 ms en las bandas activas. Medido con `processBlock` del procesador, 24 campanas estéreo a 48 kHz y bloques de 64 y 512 muestras (Linux, x86-64, Release): del 0,7 al 0,8 % de un núcleo tanto con Classic como con Matched, y lo mismo cambiando de algoritmo cada 100 bloques; las diferencias quedan dentro del ruido de la medida (±0,05 %). Diseñar cada banda con Matched cuesta lo medido en la [ADR 0013](0013-correccion-en-agudos.md).
- Un algoritmo futuro no cambia nada de esto: es un valor al final de `dsp::FilterAlgorithm` y su nombre en `ui/logic/src/options_menu.cpp`; el menú recorre `dsp::kNumFilterAlgorithms` y el estado ya sabe guardarlo.
