# Cambios

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/). El proyecto sigue [versionado semántico](https://semver.org/lang/es/): cualquier cambio que afecte al estado guardado, a los parámetros o al sonido de una configuración existente se anuncia aquí de forma destacada.

## Sin publicar

### Corregido

- Un nodo arrastrado por debajo (o por encima) del rango visible desaparecía de la curva y no se podía volver a seleccionar. Ahora el arrastre se detiene en el borde de la vista, y un nodo con más ganancia de la que se muestra se dibuja pegado al borde, donde se puede seguir arrastrando.

### Cambiado

- Interfaz rediseñada con controles propios: potenciómetros con arco de valor (desde el centro en ganancia y rango), valor editable con un clic, ajuste fino con Mayús, rueda, y doble clic o Ctrl/Cmd + clic para volver al valor por defecto. Panel de banda en tres tarjetas (banda, EQ y dinámica) y tipografía propia embebida, igual en Windows, macOS y Linux.
- La ventana mínima pasa a 960 × 540.

### Añadido

- Etiqueta flotante con frecuencia, ganancia y Q al señalar o arrastrar un nodo; medidor de la ganancia dinámica junto al nodo y en el panel; flecha en el borde cuando la ganancia de una banda no cabe en el rango visible.
- Medidor de nivel de salida con pico retenido y aviso de recorte.
- Tamaños de ventana predefinidos (S, M, L), además del redimensionado libre.
- Manejo con teclado y nombres accesibles en todos los controles.
- Canal por banda: cada banda puede actuar en estéreo, solo en el canal izquierdo o el derecho, o solo en Mid o Side. La dinámica de la banda escucha ese mismo canal. 24 parámetros nuevos (`bNN_chan`), añadidos al final: los proyectos guardados antes cargan en estéreo y suenan igual.

- Estructura del proyecto: módulos `dsp`, `core` y `plugin`, tests y CI.
- Plugin VST3 (y AU en macOS) que deja pasar el audio, con ganancia de salida y bypass.
- Formato de estado propio, versión 1.
- Núcleo de filtros en `dsp/`: campana, shelves, cortes de 6 a 96 dB/oct, notch, paso banda, paso todo y tilt, con cálculo exacto de la respuesta en frecuencia.
- Ecualizador de 24 bandas conectado al plugin, con suavizado de parámetros y fundidos al activar, cambiar de tipo o hacer bypass, sin chasquidos.
- Interfaz propia: curva interactiva con nodos arrastrables, panel de la banda seleccionada, rango de visualización ajustable.
- Analizador de espectro antes y después del EQ.
- 146 parámetros automatizables (338 con la dinámica y el canal), con escala logarítmica para frecuencia y Q y valores legibles en el host ("1.20 kHz", "+3.0 dB").
- Dinámica por banda en campanas, shelves y tilt: umbral, rango (negativo comprime, positivo expande), ataque, relajación y rodilla, sin chasquidos al modularla. 168 parámetros nuevos (7 por banda), añadidos al final: los estados guardados antes cargan con la dinámica desactivada y suenan igual.
- Sidechain externo como bus de entrada opcional, elegible por banda.
- La curva muestra la ganancia dinámica en tiempo real, con la curva estática punteada y una barra con el rango junto al nodo.
