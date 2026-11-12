# 0008. Sistema visual propio: tema en código, controles propios y lógica fuera de la capa gráfica

**Estado:** Aceptada (noviembre de 2026)

## Contexto

Hasta la fase 3 la interfaz usaba los controles por defecto de JUCE (`Slider`, `ToggleButton`, `LookAndFeel_V4`) con una paleta mínima. Funcionaba, pero tenía el aspecto genérico del framework, los colores y medidas estaban repartidos por los componentes, y el comportamiento de los controles (qué hace el arrastre, la rueda, Mayús, el doble clic) era el de JUCE, sin tests propios.

Para la primera versión pública se quiere un acabado de producto con identidad propia, que se vea igual en Windows, macOS y Linux, y que se pueda rediseñar sin tocar el comportamiento. Había que decidir:

1. Dónde viven colores, tipografía y medidas.
2. Si los controles se hacen propios o se viste a los de JUCE.
3. Dónde vive el comportamiento de esos controles y la disposición de la ventana.
4. Qué tipografía se usa y cómo se distribuye.
5. Cómo se evita parecerse a productos comerciales.

## Decisión

1. **Un único sistema de diseño en código, `ui/src/Theme.h`:** tokens de color con nombre de función (`window`, `card`, `control`, `text`, `accent`...), espaciado en rejilla de 4 px, radios, grosores de línea y tamaños de letra. Ningún componente escribe un color ni una medida de estilo a mano. Cambiar el aspecto es tocar ese archivo.
2. **Controles propios para lo que define el producto**, y JUCE vestido para lo demás:
   - Propios (`ui/src/Controls.h`): potenciómetro y barra (`ParamControl`), interruptor (`ToggleSwitch`) y selector de segmentos (`Segmented`). Se enlazan a los parámetros con `juce::ParameterAttachment`, así que automatización y deshacer del host funcionan igual que desde la curva.
   - De JUCE, pintados por `EsceqLookAndFeel`: desplegables, menús, campos de texto y botones de texto. Reescribir un menú desplegable no aporta nada y sí riesgo (ventanas emergentes, teclado, accesibilidad).
3. **El dibujo son funciones sin estado (`ui/src/Draw.h`)** que reciben qué pintar y en qué estado (reposo, ratón encima, arrastre, foco, sin efecto, desactivado). Los componentes deciden el estado; la herramienta de capturas usa las mismas funciones para generar la galería de estados de [docs/ui.md](../ui.md).
4. **El comportamiento y la disposición viven en `ui/logic`, sin JUCE y con tests:**
   - `control_logic.h`: de arrastre, rueda y teclas a valor; ajuste fino; arco de valor (desde el centro en los bipolares); texto de edición, que se confirma con `core::parseValue`.
   - `meter_logic.h`: escala y balística de los medidores.
   - `layout.h`: dónde va cada zona y cada control para un tamaño de ventana. Los tests recorren los tamaños de ventana y comprueban que nada se solapa y que cada celda tiene ancho para su etiqueta y su valor.
   - `curve_mapping.h`: además de la geometría de la curva, la colocación de la etiqueta flotante, el indicador de ganancia fuera de rango y los textos de la etiqueta.
5. **Tipografía Inter (licencia SIL OFL 1.1), embebida en el binario** con `juce_add_binary_data`: pesos Regular, Medium y SemiBold, sin modificar, en `ui/assets/fonts/` junto a su licencia. Los valores numéricos usan cifras tabulares (`tnum`) para que no bailen al cambiar. No se depende de las fuentes del sistema.
6. **Identidad propia.** Color de acento verde menta (`#5FE3C0`) para lo global y el foco; cada banda tiñe sus propios controles; curva total blanca; tarjetas oscuras redondeadas. No se toma ningún recurso, color característico, disposición ni nombre de productos comerciales.

## Consecuencias

- Un rediseño (otros colores, otro potenciómetro) no toca el comportamiento ni sus tests; un cambio de comportamiento no toca el dibujo.
- Sustituir JUCE por otra biblioteca gráfica obligaría a reescribir `ui/src`, pero no `ui/logic`: el comportamiento y la disposición se conservan.
- El binario crece unos 1,2 MB por las tres fuentes.
- Los controles propios implementan a mano lo que `juce::Slider` daba hecho: teclado, edición de texto y accesibilidad (`AccessibilityValueInterface`). Está cubierto, pero es código que mantener.
- La accesibilidad es básica: nombres, roles, valores, orden de tabulación y anillo de foco. Los nodos de la curva no son elementos accesibles individuales; se manejan con el teclado desde la curva.
- Las capturas de `docs/img/` se generan con `tools/snapshot` y hay que regenerarlas cuando cambie el aspecto (ver [CONTRIBUTING.md](../../CONTRIBUTING.md)).
- La ventana mínima pasa de 900 × 460 a 960 × 540: es lo que necesita el panel para no cortar textos.
