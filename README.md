# esceQ

Ecualizador paramétrico y dinámico, libre, pensado para funcionar bien en FL Studio y en cualquier host VST3.

**Estado: fase 2 completada (EQ estático).** Ecualizador de 24 bandas con curva interactiva y analizador de espectro. Todavía no tiene ecualización dinámica (fase 3) ni procesado Mid/Side. Aún no hay versión publicada. La hoja de ruta está en [PLAN.md](PLAN.md).

![esceQ](docs/img/esceq.png)

## Uso

| Acción | Cómo |
|---|---|
| Añadir una banda | Doble clic en un hueco de la curva |
| Mover frecuencia y ganancia | Arrastrar el nodo (con Mayús, ajuste fino) |
| Cambiar el Q | Rueda del ratón sobre el nodo o con la banda seleccionada |
| Cambiar el tipo de filtro | Clic derecho en el nodo, o el panel inferior |
| Eliminar una banda | Doble clic en el nodo |

Tipos: campana, shelf de graves y de agudos, corte de graves y de agudos (6 a 96 dB/oct), notch, paso banda, paso todo y tilt. Todo es automatizable desde el host.

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
- [docs/dsp/filtros.md](docs/dsp/filtros.md): cómo están hechos los filtros.
- [docs/dsp/ecualizador.md](docs/dsp/ecualizador.md): suavizado, fundidos y analizador.
- [docs/decisiones/](docs/decisiones/README.md): registro de decisiones de diseño.
- [CHANGELOG.md](CHANGELOG.md): cambios por versión.

## Licencia

El plugin se distribuye bajo [AGPL-3.0-or-later](LICENSE). La biblioteca de `dsp/` se distribuye bajo [MIT](dsp/LICENSE).
