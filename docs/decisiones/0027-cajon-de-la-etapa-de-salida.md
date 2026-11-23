# 0027. El cajón de la etapa de salida: la saturación, la ganancia de salida y Auto, plegables junto al medidor

**Estado:** Aceptada (noviembre de 2026), en el bloque 6.9 del [plan](../../PLAN.md) (sección 5.8). Cambia la barra superior de la [ADR 0015](0015-compensacion-automatica-de-ganancia.md) y la [ADR 0018](0018-analizador-ampliado.md): la ganancia de salida y el botón *Auto* salen de ella. No cambia el sonido, los parámetros ni la versión del estado.

## Contexto

La saturación (sección 5.8, [ADR 0026](0026-saturacion.md)) tiene tres parámetros globales, `sat_mode` (Off, Soft, Warm), `sat_drive` y `sat_mix`, y la interfaz tenía que darles un sitio. La barra superior está llena: en la ventana mínima (1120 px) el navegador de presets tenía exactamente su mínimo, 140 px. La sección 5.8 dejaba tres opciones para el usuario:

- **(a)** una tarjeta en el panel cuando no hay banda seleccionada (donde están los gestos de la curva): sitio de sobra, pero no se ve con una banda seleccionada;
- **(b)** el menú de opciones: lo más rápido, pero el drive y la mezcla no se pueden arrastrar ni ver sin abrirlo;
- **(c)** un botón en la barra que abra un pequeño panel sobre la curva: hay que quitar sitio a otro control o ensanchar la ventana mínima, y choca con la tarjeta *Match*, que ocupa la misma esquina.

El usuario no eligió ninguna de las tres: pidió una pestaña que despliegue los controles, como la de los ajustes avanzados de FabFilter Pro-L 2, para añadir controles sin sobrecargar la interfaz, y aprovechar para mover a ella otros controles y liberar sitio. Sobre la propuesta concreta, eligió el contenido y el comportamiento en la vista ampliada (abajo).

## Decisión

1. **Un cajón plegable, *Output*, entre la curva y el medidor de salida**, con el alto de los dos (el del teclado de notas incluido). Va en el orden de la señal: el ecualizador (la curva), la saturación, la ganancia de salida y la compensación, y el medidor, que mide lo que sale de todo eso.
2. **Dentro van la saturación, la ganancia de salida y *Auto*.** Arriba, *Saturation*: el selector Off/Soft/Warm y los potenciómetros *Drive* y *Mix*. Abajo, junto al medidor: el potenciómetro *Gain* (`output_gain`) y el botón *Auto* (`auto_gain`), que salen de la barra superior. Son la etapa de salida y están junto a su medidor; la barra gana 268 px.
3. **Medidas:** abierto, 160 px (las celdas de *Drive* y *Mix* tienen el mínimo de una celda de potenciómetro, 66 px); plegado, una pestaña de 16 px. Con la separación, la curva cede 168 px abierto y 24 plegado. La ventana mínima no cambia (1120 × 540): abierta, la curva mide al menos 868 px de ancho.
4. **Plegar y abrir:** la cabecera del cajón (el título y una flecha) lo pliega, y la pestaña lo abre. Plegado, la pestaña muestra un punto del color de acento si algo de dentro cambia el sonido (la saturación fuera de Off, una ganancia distinta de 0 dB o *Auto* encendido), y su ayuda dice qué ("Saturation Warm, drive 9.0 dB, mix 80% · Output -3.0 dB · Auto gain on"), porque no se ve.
5. **Abierto por defecto, y se recuerda con el proyecto**, como el teclado de notas: es una preferencia de la interfaz (`ui.output_stage`), no se deshace ([ADR 0009](0009-preferencias-deshacer-y-presets.md)) y solo se escribe plegado (`ui.output_stage 0`), así que un estado que no lo pliega se escribe igual que antes y los proyectos anteriores lo abren. Abierto por defecto porque plegado no se ve la ganancia de salida.
6. **En la vista ampliada se oculta**, con el medidor, abierto o plegado: es para mirar el espectro. Al volver, está como estaba.
7. **La barra superior** pierde la barra de salida, su valor y *Auto*. El navegador de presets tiene ahora su máximo (320 px) en todos los tamaños, y en la ventana mínima aún sobran 88 px, que quedan libres para lo que venga.
8. **Los controles de la saturación:** con Off, *Drive* y *Mix* se atenúan (sin efecto, pero editables, como la dinámica apagada) y su ayuda lo dice. La ayuda del selector dice qué hace y, en fase lineal, la latencia que añade a la frecuencia de muestreo actual ("adds 0.98 ms latency" a 48 kHz, 1.07 ms a 44,1, 0.45 a 96 y 0.18 a 192) y que encenderla o apagarla deja un hueco; en IIR no dice nada de latencia, porque no tiene. La latencia es la del modo de fase elegido (`dsp::PhaseEqualizer::saturationLatency`).

Todo lo que se decide sin ventana está en `ui/logic` con sus tests: la disposición en `layout.h` (`editorLayout(..., drawerOpen)` y `outputDrawerLayout`) y los textos en `output_stage.h`.

## Alternativas descartadas

- **Una tira horizontal bajo la curva**, más parecida a la de Pro-L 2, con la saturación, la salida y *Auto* en fila: le quita a la curva 88 px de alto, que es lo que menos le sobra (en el tamaño S quedaría en unos 264 px). El ancho sobra más que el alto.
- **El cajón solo con la saturación:** no cambia la barra, pero no libera nada, y la salida quedaría lejos de su medidor.
- **Las tres de la sección 5.8:** ver el contexto.
- **Mover también el rango de la curva o el bypass:** el rango es de la vista, no de la etapa de salida, y el bypass tiene que verse siempre.

## Consecuencias

- La curva es más estrecha con el cajón abierto (168 px; en el tamaño M, de 1096 a 928). Quien la quiera entera lo pliega, y queda plegado en ese proyecto.
- La ganancia de salida es un potenciómetro en vez de una barra; se maneja igual (arrastre, rueda, teclado, doble clic, escribir el valor).
- Plegado, la ganancia de salida no se ve; la pestaña avisa con el punto y la ayuda.
- Cambian todas las capturas de la documentación.
- El sonido no cambia: la interfaz solo mueve parámetros, el procesador es el mismo y `processBlock` no sabe si el editor está abierto.
