# 0005. 24 bandas declaradas desde ya, y normalización definida en core

**Estado:** Aceptada (noviembre de 2026). Complementa a [0004](0004-parametros-congelados.md).

## Contexto

La fase 2 pedía un ecualizador de 8 bandas y la fase 4, uno de 24. Pero los parámetros solo se pueden añadir al final de la tabla, y pasar de 8 a 24 bandas más adelante dejaría las bandas 9 a 24 detrás de cualquier parámetro que se añadiera entre medias, con nombres y posiciones poco coherentes.

Además, el host guarda la automatización como una posición entre 0 y 1, no como un valor en Hz o dB. La correspondencia entre ambas forma parte del contrato: si cambia, una automatización guardada pasa a mover el parámetro a otros valores. En JUCE esa correspondencia la define normalmente el framework (`NormalisableRange`), lo que la ataría a JUCE.

## Decisión

- Se declaran ya las 24 bandas, cada una con 6 parámetros: `on`, `type`, `freq`, `gain`, `q` y `slope`. Las desactivadas no consumen CPU.
- La correspondencia valor ↔ 0..1 la definen `core::toNormalized` y `core::fromNormalized`. El plugin se la pasa a JUCE mediante un `NormalisableRange` con funciones propias.
- Frecuencia (20 Hz a 20 kHz) y Q (0,1 a 30) usan escala logarítmica; ganancia (±30 dB), lineal.
- El texto que se muestra y el que se acepta al escribir un valor también salen de `core` (`formatValue`, `parseValue`).
- El contrato de parámetros pasa a un archivo, `tests/fixtures/parameters/v1.txt`, con identificador, tipo, escala, rango y número de opciones de cada parámetro. Con 146 parámetros, escribirlo a mano dentro del test ya no era razonable.

## Consecuencias

- Los parámetros de la dinámica (fase 3) se añadirán al final, después de las 24 bandas. Para acceder a ellos hará falta una segunda función de índices además de `bandParameter()`.
- Cambiar de framework no altera la automatización guardada.
- El orden de las opciones de `type` y `slope` es contrato: un tipo de filtro nuevo se añade al final de la lista.
- Hasta la 0.1 aún puede ajustarse el contrato si hay un buen motivo; a partir de ahí es firme.
