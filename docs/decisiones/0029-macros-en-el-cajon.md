# 0029. Las macros en la interfaz: una pestaña del cajón Output, Link to Macro en el menú de cada potenciómetro y del nodo, y el anillo de modulación

**Estado:** Aceptada (noviembre de 2026), en el bloque 7.3 del [plan](../../PLAN.md) (sección 5.9). Completa la [ADR 0028](0028-automatizacion-macros-y-sidechain.md) (las macros, sin ventana, en el bloque 7.2) y cambia la cabecera del cajón de la [ADR 0027](0027-cajon-de-la-etapa-de-salida.md). No cambia el sonido, los parámetros ni la versión del estado: añade una preferencia de la interfaz, `ui.output_tab`.

## Contexto

Con el bloque 7.2, las ocho macros (`macro_1` a `macro_8`) ya eran parámetros del host y sus asignaciones iban en el estado, pero solo se podían crear escribiendo un preset a mano. Faltaba dónde ver las macros, cómo asignarlas y cómo ver lo que hacen. La sección 5.9 dejaba al usuario tres sitios:

- **(a)** una fila de ocho potenciómetros plegable bajo la curva;
- **(b)** una pestaña del cajón *Output*;
- **(c)** un panel que se abre desde la barra superior, sobre la curva, como el de *Match*.

Medido con la disposición de entonces: (a) le quita a la curva unos 96 px de alto (en el tamaño S, de 364 a unos 268), que es lo que menos le sobra, y la ADR 0027 ya descartó una tira así por eso; (c) cabe en la barra (sobraban 88 px), pero tapa la curva mientras está abierto, y las macros se mueven mientras se trabaja en ella; (b) no le quita sitio a nada: en 160 px de ancho caben dos columnas de celdas de 66 px, y en el alto del cajón, cuatro filas.

## Decisión

1. **Una pestaña *Macros* en el cajón *Output*** (la opción b, elegida por el usuario). La cabecera del cajón pasa a tener dos pestañas, *Output* y *Macros*, y a su derecha la flecha que lo pliega. *Output* es lo de la ADR 0027 sin cambios; *Macros*, los ocho potenciómetros en dos columnas y cuatro filas (1 2 / 3 4 / 5 6 / 7 8), con su nombre y su valor. La pestaña elegida se recuerda con el proyecto (`ui.output_tab 1`, solo con *Macros*), como el cajón plegado; plegado, la pestaña lateral marca con su punto también una macro con destinos fuera de 0, y su ayuda lo dice ("Macros: 3 links moving").
2. **Una macro sin destinos se atenúa** (sin efecto, pero se puede mover y automatizar), y su ayuda dice qué mueve cada una y con qué profundidad, o cómo se asigna.
3. **Link to Macro, en el menú contextual** de cada potenciómetro que puede ser destino (`core::isMacroTarget`) y del nodo de la curva (frecuencia, y ganancia y Q si el tipo los usa): cada macro es un submenú con ocho profundidades (±100, ±50, ±25, ±10 %), *Other...* para escribir cualquiera de −100 a +100, y *Remove link* si ya lo mueve. El clic derecho en una macro lista sus destinos para cambiarlos o quitarlos, y *Remove all links*. Cada elección es una acción de deshacer (`PluginProcessor::changeMacroAssignments`). Un menú y no arrastrar sobre el anillo: el arrastre ya es del valor, y en FL el clic derecho no es del host dentro del plugin (README).
4. **Lo que suena, a la vista y el valor, en su sitio:** cada potenciómetro enlazado lleva un anillo fino del color de la curva total, por dentro del arco de valor, del valor del parámetro a lo que suena (con la macro en 0, un punto: está enlazado). La curva (la de cada banda, su referencia estática y la total) se dibuja con lo que suena, que es la misma suma que el audio (`core::applyMacros`); el nodo se queda en el valor de base, porque es lo que se arrastra y lo que ve el host, con una línea fina hasta un círculo donde suena la banda.
5. **En una ventana baja, las celdas encogen sus huecos.** Una celda de potenciómetro mide 74 px de alto (14 de etiqueta, 32 del potenciómetro mínimo, 20 del valor y dos huecos de 4); en ventanas de menos de unos 572 px de alto, cuatro filas no caben así en el cajón, y los huecos encogen hasta 0 (66 px en la ventana mínima, 1120 × 540). Las celdas con 74 px o más no cambian, píxel a píxel.

Lo que se decide sin ventana está en `ui/logic` con sus tests: `esceq/ui/macros.h` (las entradas del menú, las asignaciones que salen de cada elección, los destinos del nodo, el anillo, las ayudas y el aviso; `tests/ui/macros_test.cpp`), `outputDrawerLayout` y `knobCellLayout` (`tests/ui/layout_test.cpp`) y `outputStageActive`/`outputStageTooltip` (`tests/ui/output_stage_test.cpp`). En `ui/src`, `MacroLinks` construye los menús y pone al día los anillos.

## Alternativas descartadas

- **La fila bajo la curva y el panel desde la barra:** ver el contexto.
- **Una pestaña aparte para cada macro, con sus destinos a la vista:** cabría una lista de ocho destinos con su profundidad, pero las macros se mueven desde el host, y lo útil es ver las ocho a la vez; los destinos están en la ayuda y en el menú de cada macro.
- **Cambiar la profundidad arrastrando el anillo** (como en algunos sintetizadores): choca con el arrastre del valor y no se descubre sin explicarlo; el menú con profundidades fijas y *Other...* cubre lo mismo.
- **Mover el nodo a lo que suena:** al arrastrarlo, saltaría entre lo que se arrastra y lo que suena, y un clic movería la banda a otro sitio. El nodo es el valor del parámetro, como el potenciómetro.

## Consecuencias

- La cabecera del cajón cambia (las pestañas en lugar del título *OUTPUT*), así que cambian las capturas que la muestran.
- Con *Macros* a la vista, la saturación y la ganancia de salida están en la otra pestaña: se ven en la ayuda del punto solo con el cajón plegado.
- El lector de pantalla lee el valor de base de un potenciómetro, no lo que suena; el menú de los potenciómetros se abre también con Mayús+F10, pero el de los nodos solo con el ratón (los nodos no son elementos accesibles).
- El sonido no cambia: la interfaz solo cambia asignaciones con `changeMacroAssignments`, y la curva usa la misma suma que el audio.
