# 0003. Formato de estado propio y versionado

**Estado:** Aceptada (noviembre de 2026)

## Contexto

El estado del plugin se guarda dentro de los proyectos y presets de los usuarios. Si una versión nueva no sabe leer el estado de una antigua, esos proyectos se rompen. Lo habitual en JUCE es volcar el `ValueTree` de los parámetros, pero eso ata el formato al framework y no tiene versión ni migraciones explícitas.

## Decisión

El estado lo escribe y lo lee `core/`, en un formato de texto propio:

```
esceQ-state <versión>
<id> <valor>
...
```

- Números con punto decimal, independientes de la configuración regional.
- Lectura tolerante: lo ausente toma el valor por defecto, los identificadores desconocidos se ignoran, los valores inválidos se sanean.
- Si la cabecera no es válida no se carga nada y el plugin conserva su estado.
- Cada cambio de formato sube `kStateVersion` y añade una migración de la versión anterior a la nueva.

## Consecuencias

- Por cada versión publicada se guarda al menos un estado real en `tests/fixtures/state/`. Esos archivos **no se modifican ni se borran**; el test que los carga es lo que garantiza la compatibilidad hacia atrás.
- Un estado escrito por una versión más nueva se carga parcialmente (lo que se conozca) en lugar de fallar.
- El formato es de texto y por tanto legible y fácil de depurar, a costa de ocupar algo más. Cuando haya 24 bandas serán unos pocos kilobytes, irrelevante.
- Datos que no son parámetros (por ejemplo, tamaño de ventana) necesitarán ampliar el formato; se hará con una versión nueva.
