# 0009. Estado versión 2 con preferencias de la interfaz, deshacer por instantáneas y presets en el formato de estado

**Estado:** Aceptada (noviembre de 2026). Amplía la [0003](0003-formato-de-estado.md).

## Contexto

Para la primera versión pública faltaban tres cosas que tocan el estado del plugin:

1. Guardar con el proyecto las preferencias de la interfaz (modo del analizador, rango de la curva, tamaño de la ventana). No son parámetros: el host no debe verlas ni automatizarlas. La [ADR 0003](0003-formato-de-estado.md) ya preveía ampliar el formato con una versión nueva.
2. Deshacer y rehacer dentro del plugin. Muchos hosts no deshacen los cambios de parámetros de un plugin, o lo hacen valor a valor.
3. Presets de fábrica y de usuario.

Al escribir los tests de la capa del plugin apareció además un problema que ya existía: el host guarda cada parámetro como una posición entre 0 y 1 en `float`, y al volver de ahí los valores se desviaban un poco (6 dB volvía como 6,0000014 dB). Era inaudible, pero un preset recién cargado ya no coincidía exactamente con su archivo.

## Decisión

### Estado, versión 2

- El formato sigue siendo `clave valor`, una línea por dato. La versión 2 añade claves con el prefijo `ui.`, que no son parámetros: `ui.analyzer` (0 apagado, 1 post, 2 pre + post), `ui.range` (6, 12, 18 o 30), `ui.width` y `ui.height` (píxeles lógicos; se omiten si la ventana no se ha tocado) y `ui.preset` (nombre del preset cargado; el valor es el resto de la línea).
- `core::PluginState` reúne los parámetros y `core::UiSettings`. Lo leen y lo escriben `deserializePluginState` y `serializePluginState`. `serializeState` y `deserializeState` siguen existiendo y tratan solo los parámetros: es lo que usa un preset.
- **Migración 1 → 2:** un estado de la versión 1 no tiene preferencias, así que toman su valor por defecto; y si aparece alguna línea `ui.*` en un estado que dice ser de la versión 1, no la escribió esceQ y se descarta. Las migraciones se aplican en cadena antes de interpretar las líneas.
- **Compatibilidad hacia delante:** una versión anterior de esceQ que lea un estado de la versión 2 ignora las claves `ui.*` (regla "lo desconocido se ignora") y carga los parámetros. Un test comprueba que ningún parámetro empieza por `ui.`.
- Las preferencias se sanean al leerlas y al guardarlas. El tamaño de ventana es un par: si una medida no vale, se descartan las dos. El nombre del preset no puede contener saltos de línea (no puede colar líneas en el estado).
- Fixture nuevo: `tests/fixtures/state/v2_ui.txt`. Los de la versión 1 no se tocan y siguen cargando.

### Deshacer y rehacer

- **Instantáneas completas.** `core::UndoHistory` guarda el valor de los 338 parámetros en cada acción (1,4 kB) y deshacer es volver a la anterior. Hasta 200 acciones. Es más simple y más robusto que guardar diferencias: no hay acciones que aplicar al revés ni orden que respetar.
- **Una acción por gesto, no por valor.** `plugin/src/EditHistory` escucha el principio y el final de los gestos de los parámetros (los que la interfaz marca con `beginChangeGesture`/`endChangeGesture`) y registra una instantánea cuando se cierra el último gesto abierto, tras esperar 300 ms por si llega otro. Resultado: un arrastre entero, añadir una banda (seis parámetros) o una ráfaga de rueda son una sola acción.
- **Los cambios del host no son acciones.** La automatización no abre gestos, así que no llena el historial; sus cambios quedan incluidos en la instantánea de la siguiente acción del usuario.
- **Cargar un proyecto borra el historial** (`setStateInformation`); cargar un preset desde el plugin es una acción que se puede deshacer.
- Deshacer aplica la instantánea con `setValueNotifyingHost`, cada parámetro en su gesto: el host se entera y puede grabarlo.
- Atajos: Ctrl/Cmd+Z y Ctrl/Cmd+Mayús+Z (o Ctrl/Cmd+Y). Muchos hosts se quedan con esas teclas, así que también hay botones.

### Presets

- **Un preset es un estado con solo parámetros**, en el mismo formato de texto. `core::serializePreset` escribe solo los que no tienen su valor por defecto: el archivo es corto y legible.
- **De fábrica:** cinco (De-esser, Resonance Control, Low-End Cleanup, Vocal, Gentle Master), escritos como texto dentro de `core/src/presets.cpp`. Un test comprueba que cada línea es un parámetro que existe con un valor dentro de su rango, y lo que promete cada preset. Una vez publicados no cambian de nombre ni de sonido; los nuevos se añaden al final.
- **De usuario:** archivos `.esceq` en `%APPDATA%\esceQ\Presets` (Windows), `~/Library/Application Support/esceQ/Presets` (macOS) o `~/.config/esceQ/Presets` (Linux). La variable de entorno `ESCEQ_PRESETS_DIR` la sustituye (la usan los tests). Los nombres se limpian con `core::sanitizePresetName` para que sirvan como nombre de archivo en los tres sistemas.
- Cargar un preset no cambia las preferencias de la interfaz. El nombre del preset cargado se guarda en el estado (`ui.preset`) y se marca como modificado cuando los parámetros dejan de coincidir con él; la salida y el bypass no cuentan.
- No se sobrescribe un preset existente sin preguntar.

### Valores exactos al pasar por el host

`core::fromNormalized` devuelve, de entre los valores que corresponden a una misma posición 0..1, el de menos cifras significativas, y `toNormalized` calcula en doble precisión. Así `fromNormalized(toNormalized(v)) == v` para los valores "redondos" (los escritos a mano, los de los presets, los valores por defecto), y una segunda vuelta nunca mueve nada. La correspondencia con la posición no cambia más allá de la resolución del `float` (menos de dos millonésimas del valor), así que la automatización guardada se reproduce igual.

## Consecuencias

- El contrato de estado tiene ahora dos versiones con fixtures: 1 y 2.
- Hay un tercer grupo de tests, `tests/plugin`, que sí usa JUCE (sin la interfaz): estado por `get/setStateInformation`, agrupación del deshacer y presets en disco. Se ejecuta con `ctest` en la compilación completa.
- El historial vive en el procesador, no en el editor: cerrar y abrir la ventana no lo pierde. No se guarda con el proyecto.
- Si el host deshace por su cuenta un cambio de parámetro, el plugin no lo distingue de una automatización: no crea una acción, y la siguiente instantánea lo recoge.
- Un preset guardado por una versión más nueva carga lo que se conozca, como cualquier estado.
- Las preferencias `ui.*` nuevas se añaden sin subir de versión mientras tengan un valor por defecto razonable; solo un cambio de significado exige versión y migración.
