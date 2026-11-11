# 0004. Parámetros en una tabla única, con identificadores congelados

**Estado:** Aceptada (noviembre de 2026)

## Contexto

Los hosts identifican cada parámetro por su identificador para la automatización, y el estado guardado también. Renombrar un parámetro, cambiar su rango o reordenarlos rompe la automatización y los presets existentes. Además, si la lista de parámetros se repite en varios sitios (procesador, interfaz, estado), es fácil que se desincronicen.

## Decisión

- Existe una única tabla, `core::kParameters`, de la que se derivan los parámetros del host, el estado y, más adelante, la interfaz.
- Una vez publicado, un parámetro no cambia de identificador, tipo, rango ni posición, y no se elimina.
- Los parámetros nuevos se añaden al final.
- `tests/core/parameters_test.cpp` contiene una copia congelada del contrato y falla ante cualquier cambio incompatible.

## Consecuencias

- Si un parámetro queda obsoleto, se mantiene en la tabla (oculto en la interfaz si hace falta) en lugar de borrarlo.
- Cambiar el rango de un parámetro existente exige crear uno nuevo con otro identificador y migrar el estado.
- El nombre visible y la unidad sí pueden cambiar: no forman parte del contrato.
- Hasta la versión 0.1 no hay usuarios, así que el contrato aún puede ajustarse; a partir de ahí es firme. Antes de la 0.1 hay que declarar los parámetros de las 24 bandas, aunque no todas se usen.
