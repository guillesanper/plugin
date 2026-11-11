# Cambios

Formato basado en [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/). El proyecto sigue [versionado semántico](https://semver.org/lang/es/): cualquier cambio que afecte al estado guardado, a los parámetros o al sonido de una configuración existente se anuncia aquí de forma destacada.

## Sin publicar

### Añadido

- Estructura del proyecto: módulos `dsp`, `core` y `plugin`, tests y CI.
- Plugin VST3 (y AU en macOS) que deja pasar el audio, con ganancia de salida y bypass.
- Formato de estado propio, versión 1.
- Núcleo de filtros en `dsp/`: campana, shelves, cortes de 6 a 96 dB/oct, notch, paso banda, paso todo y tilt, con cálculo exacto de la respuesta en frecuencia.
- Ecualizador de 24 bandas conectado al plugin, con suavizado de parámetros y fundidos al activar, cambiar de tipo o hacer bypass, sin chasquidos.
- Interfaz propia: curva interactiva con nodos arrastrables, panel de la banda seleccionada, rango de visualización ajustable.
- Analizador de espectro antes y después del EQ.
- 146 parámetros automatizables, con escala logarítmica para frecuencia y Q y valores legibles en el host ("1.20 kHz", "+3.0 dB").
