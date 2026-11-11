# 0001. JUCE como framework, con el DSP independiente de él

**Estado:** Aceptada (noviembre de 2026)

## Contexto

Hace falta un framework que exporte VST3 (prioritario para FL Studio), AU y más adelante CLAP, y que ofrezca una capa gráfica capaz de una interfaz rica: curva interactiva, analizador, muchas bandas. Alternativas valoradas: CLAP nativo con clap-wrapper, DPF, iPlug2 y nih-plug (Rust). Todas tienen licencias más permisivas, pero obligan a construir o importar la interfaz gráfica y cuentan con menos documentación.

## Decisión

Usar JUCE, confinado a `plugin/` y `ui/`. `dsp/` y `core/` son C++20 puro y no incluyen ninguna cabecera de JUCE.

Se usa JUCE 9 (el plan inicial hablaba de JUCE 8; la 9 ya era la versión estable al empezar y mantiene la misma licencia).

## Consecuencias

- El plugin queda obligado a AGPLv3 (ver [0002](0002-licencias.md)).
- Cambiar de framework en el futuro supone reescribir `plugin/` y `ui/`, no el DSP ni el formato de estado.
- No se pueden usar utilidades de JUCE (`juce::dsp`, `ValueTree` para el estado, etc.) dentro de `dsp/` ni `core/`, aunque a veces fuera cómodo.
- El CI compila `dsp/` y `core/` sin JUCE para que la separación no se rompa por descuido.
