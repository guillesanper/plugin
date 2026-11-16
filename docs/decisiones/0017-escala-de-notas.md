# 0017. Escala de notas: lógica en ui/logic, dos preferencias de la interfaz y el interruptor en el menú de opciones

**Estado:** Aceptada (noviembre de 2026)

## Contexto

La sección 5.1 del [plan](../../PLAN.md) pide leer y fijar frecuencias como notas: un teclado de piano bajo la curva, la nota y su desviación en la etiqueta flotante y en el campo de frecuencia, ajustar a semitonos con Alt al arrastrar y con Ctrl/Cmd + izquierda/derecha, escribir una nota en el campo de frecuencia y una afinación de A4 ajustable entre 400 y 480 Hz. Fijaba también que no hay parámetros (dos preferencias, `ui.notes` y `ui.a4`) y los criterios de aceptación. Al implementarlo quedaban por decidir:

1. Dónde vive la lógica y cómo se redondea en el límite de ±50 ct.
2. Cómo se guardan las dos preferencias sin cambiar los proyectos anteriores.
3. Dónde se enciende el teclado y dónde se elige la afinación, con la barra superior llena.
4. Dónde va la nota en el panel, si no cabe junto al valor.
5. Qué hacen exactamente Alt y Ctrl/Cmd + flechas, y si dependen de que se vea el teclado.
6. Si cambiar el teclado o la afinación se deshace.

## Decisión

1. **Toda la lógica en `ui/logic/notes.h`, sin JUCE y con tests** (`tests/ui/notes_test.cpp`): `noteForFrequency`, `frequencyForNote`, `snapToSemitone`, `stepSemitones`, `parseNote`, `noteName`, `noteText`, `parseTuning`, y la geometría del teclado (`pianoKeys`, `pianoKeyAt`). Las notas van en numeración MIDI (A4 = 69, C4 = 60). La desviación va de −50 ct, incluido, a +50 ct, excluido: justo en la mitad entre dos notas la frecuencia es la de arriba, a −50 ct. Para que esa regla no dependa del error de coma flotante del logaritmo (la frecuencia de la mitad, calculada, puede caer a 1e-13 semitonos por debajo), lo que queda a menos de 1e-7 ct de la mitad cuenta como la mitad. Un test recorre las 120 notas con cinco afinaciones y comprueba ±49,99 ct y la mitad exacta; sin la tolerancia falla. La frecuencia de la banda se sigue guardando como número real: la nota solo se calcula al mostrarla, al ajustar o al escribirla.

2. **Dos líneas `ui.*` que solo se escriben si no tienen su valor por defecto**: `ui.notes 1` con el teclado visible y `ui.a4` si la afinación no es 440 Hz. El estado sigue en la versión 3 (las líneas `ui.*` nuevas no la suben, [ADR 0009](0009-preferencias-deshacer-y-presets.md)), y un proyecto que no las usa se escribe exactamente igual que antes: siguen pasando sin cambios los tests que reescriben los fixtures de la versión 3 idénticos. Lo ausente toma el valor por defecto (oculto, 440 Hz), así que los proyectos anteriores se ven igual. Lo inválido al leer (`ui.notes 7`, `ui.a4 399.9`, `nan`) toma el valor por defecto, como el resto de preferencias; la afinación se redondea a 0,1 Hz al sanearla (`core::validA4`), lo que además permite compararla exactamente.

3. **El teclado y la afinación van en el menú de opciones, no en un botón de la barra superior.** El plan pedía un botón en la barra, pero la barra está justa: con la ventana mínima (1120 px) el navegador de presets tiene exactamente los 140 px que necesita ([ADR 0015](0015-compensacion-automatica-de-ganancia.md)); un botón de icono más (28 px y su separación) obligaría a ensanchar la ventana mínima o a estrechar el navegador. El menú de opciones ya reúne las preferencias de la instancia, así que gana una tercera sección, **Notes**, con *Piano keyboard* (marcada si se ve) y un submenú *Tuning: A4 = 440.0 Hz* con las afinaciones habituales (415, 432, 440, 442, 443 y 444 Hz) y *Other...*, que abre un diálogo para escribir cualquiera de 400 a 480 Hz (lo que no se entiende o se sale del rango no cambia la afinación). Los textos y la correspondencia entre entradas y acciones están en `options_menu.h`, con tests.

4. **En el panel, la nota sustituye a la etiqueta "FREQ" del potenciómetro, no al valor.** El valor mide lo que su celda: con la ventana mínima, 88 px; con la mediana, 95. Medido en las capturas, "240 Hz · B3 -49 ct" ocupa 96 px y "1.20 kHz · D6 +37 ct" unos 110, así que no caben en la línea del valor en los tamaños S y M. En la línea de la etiqueta caben (letra de etiqueta, unos 60 px), y el valor en Hz debajo deja claro qué control es. El nombre accesible sigue siendo el del parámetro. La etiqueta flotante de la curva sí lleva el texto completo, "220 Hz · A3 +0 ct", porque su ancho se adapta. Las dos cosas solo con el teclado visible: por defecto la interfaz se ve como antes.

5. **Alt y Ctrl/Cmd + flechas ajustan siempre, se vea o no el teclado**, con la afinación elegida y sin salirse del rango de la frecuencia (20 Hz a 20 kHz: la nota más cercana que cabe, de E0 a D#10 con 440 Hz).
   - Alt al arrastrar: la banda suena en la nota más cercana a donde estaría el nodo sin ajustar. La vista guarda esa frecuencia sin ajustar durante el arrastre; si se ajustara la del parámetro, cada movimiento menor de medio semitono volvería a la misma nota y el nodo se quedaría pegado. Hasta ahora Alt no hacía nada en la curva.
   - Ctrl/Cmd + izquierda/derecha: un semitono, a la nota siguiente o anterior (desde fuera de una nota, a la más cercana en ese sentido). Antes avanzaba un 1 % del recorrido del parámetro, 1,2 semitonos: el cambio es pequeño y deja la banda afinada. Con Mayús sigue el paso fino de siempre (0,1 %), para afinar a mano entre notas.
   - Escribir una nota en el campo de frecuencia ("A3", "C#2", "Eb4", también con `♯` y `♭`) funciona en cualquier parámetro en Hz; como un número, se ajusta al rango. Los números se siguen leyendo como antes ("1e3" no es una nota).

6. **Ni el teclado ni la afinación entran en deshacer.** Son preferencias de la interfaz, como el rango o el analizador ([ADR 0009](0009-preferencias-deshacer-y-presets.md)): no cambian el sonido. Ajustar una banda a una nota (Alt, Ctrl/Cmd + flechas, escribirla) sí es una acción, porque cambia su frecuencia como cualquier gesto.

Detalles del teclado (`ui/src/PianoKeyboard`): una franja de 14 px bajo la curva, a 4 px de ella, con su misma x y su mismo ancho (`editorLayout(..., pianoKeyboard)`); la curva cede esos 18 px y lo demás no se mueve. Sin el teclado, la disposición es la de antes, píxel a píxel (test de `layout`, con las medidas de la ventana mediana fijadas). Las teclas siguen el eje logarítmico: cada semitono mide lo mismo, las negras ocupan su semitono y el 60 % del alto, y las blancas se extienden bajo la mitad de cada negra vecina. Cada do lleva su octava si cabe (C1 a C9; C10 en ventanas anchas). Al pasar el ratón por una tecla, se ilumina y la curva dibuja su frecuencia como línea vertical con su nombre ("A3 · 220 Hz").

## Consecuencias

- Ningún parámetro nuevo (siguen 364) y el estado sigue en la versión 3. El contrato de la 0.1 no cambia.
- El sonido no cambia: la afinación solo interviene al convertir entre notas y frecuencias.
- El coste es despreciable: el teclado solo se repinta al cambiar la tecla señalada, la afinación o el tamaño, y la curva no hace nada nuevo salvo la línea de la tecla señalada. Con `esceq_snapshot --bench` (L, escala 1, 24 bandas dinámicas), la mediana por fotograma de la curva es de 12,6–13,0 ms con el teclado y 12,5–12,6 ms sin él, dentro del ruido ([docs/ui.md](../ui.md)).
- La afinación también está activa con el teclado oculto (ajustar y escribir notas); quien la cambie y oculte el teclado sigue afinando a ella. El submenú dice siempre cuál es.
- La sección 5.1 del plan queda con estas diferencias: el interruptor en el menú de opciones y no en la barra, la nota del panel en la etiqueta y no junto al valor, y Ctrl/Cmd + flechas avanza un semitono siempre, con Mayús para el paso fino.
