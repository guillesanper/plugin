# esceQ

Ecualizador paramétrico y dinámico, libre, pensado para funcionar bien en FL Studio y en cualquier host VST3.

**Estado: fase 0.** El plugin carga y deja pasar el audio con un control de ganancia de salida. Todavía no ecualiza. La hoja de ruta está en [PLAN.md](PLAN.md).

## Compilar

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Detalles, requisitos y reglas de contribución en [CONTRIBUTING.md](CONTRIBUTING.md).

## Documentación

- [DEV.md](DEV.md): cómo montar el entorno de desarrollo, paso a paso.
- [PLAN.md](PLAN.md): objetivos, decisiones y hoja de ruta.
- [docs/arquitectura.md](docs/arquitectura.md): módulos, contratos y modelo de hilos.
- [docs/decisiones/](docs/decisiones/README.md): registro de decisiones de diseño.
- [CHANGELOG.md](CHANGELOG.md): cambios por versión.

## Licencia

El plugin se distribuye bajo [AGPL-3.0-or-later](LICENSE). La biblioteca de `dsp/` se distribuye bajo [MIT](dsp/LICENSE).
