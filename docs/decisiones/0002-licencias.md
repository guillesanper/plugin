# 0002. Plugin en AGPLv3, DSP en MIT

**Estado:** Aceptada (noviembre de 2026)

## Contexto

JUCE sin licencia comercial exige que el producto se publique bajo AGPLv3. El SDK de VST3 es MIT desde la versión 3.8 y CLAP es MIT, así que los formatos no añaden restricciones. Queremos además que los algoritmos de audio puedan reutilizarse en otros proyectos.

## Decisión

- El repositorio y el plugin se publican bajo **AGPL-3.0-or-later** (`LICENSE`).
- `dsp/` y sus tests se publican bajo **MIT** (`dsp/LICENSE`).
- Cada archivo fuente lleva su identificador `SPDX-License-Identifier`.

## Consecuencias

- No se copia código AGPL/GPL de terceros dentro de `dsp/`: lo impediría seguir siendo MIT.
- `core/` queda en AGPLv3 por ahora. Mientras haya un único titular de derechos puede relicenciarse cuando se quiera.
- Cambiar la licencia del proyecto requiere el permiso de todos los autores. Antes de aceptar la primera contribución externa hay que decidir si se pide un acuerdo de contribución (CLA) que lo permita.
