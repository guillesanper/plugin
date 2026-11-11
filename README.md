# esceQ

Ecualizador paramétrico y dinámico, libre, pensado para funcionar bien en FL Studio y en cualquier host VST3.

**Estado: fase 3 completada (dinámica).** Ecualizador de 24 bandas con curva interactiva, analizador de espectro y dinámica por banda con sidechain externo. Todavía no tiene procesado Mid/Side ni L/R por banda (fase 4). Aún no hay versión publicada. La hoja de ruta está en [PLAN.md](PLAN.md).

![esceQ](docs/img/esceq.png)

## Uso

| Acción | Cómo |
|---|---|
| Añadir una banda | Doble clic en un hueco de la curva |
| Mover frecuencia y ganancia | Arrastrar el nodo (con Mayús, ajuste fino) |
| Cambiar el Q | Rueda del ratón sobre el nodo o con la banda seleccionada |
| Cambiar el tipo de filtro | Clic derecho en el nodo, o el panel inferior |
| Eliminar una banda | Doble clic en el nodo |
| Hacer dinámica una banda | Botón "Dynamic" del panel inferior, o clic derecho en el nodo |

Tipos: campana, shelf de graves y de agudos, corte de graves y de agudos (6 a 96 dB/oct), notch, paso banda, paso todo y tilt. Todo es automatizable desde el host.

**Dinámica:** las campanas, los shelves y el tilt pueden ser dinámicos. Cuando el nivel en su zona del espectro supera el umbral, la banda baja (rango negativo: de-esser, control de resonancias) o sube (rango positivo) hasta el rango indicado, con ataque, relajación y rodilla ajustables. La curva muestra en tiempo real la ganancia que se está aplicando, con la estática punteada como referencia. El detector escucha la entrada del plugin o, con "External", el sidechain.

**Sidechain en FL Studio** (pasos aún sin verificar en FL Studio): en el mezclador, haz clic derecho en la flecha de envío de la pista que debe controlar el EQ y elige "Sidechain to this track" en la pista donde está esceQ. Luego, en los ajustes del envoltorio del plugin (rueda dentada, pestaña *Processing*), asigna esa entrada al puerto de sidechain. Si eliges "External" sin nada conectado, el panel lo avisa con "No sidechain".

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
- [docs/dsp/dinamica.md](docs/dsp/dinamica.md): detector, envolvente y curva de ganancia de las bandas dinámicas.
- [docs/decisiones/](docs/decisiones/README.md): registro de decisiones de diseño.
- [CHANGELOG.md](CHANGELOG.md): cambios por versión.

## Licencia

El plugin se distribuye bajo [AGPL-3.0-or-later](LICENSE). La biblioteca de `dsp/` se distribuye bajo [MIT](dsp/LICENSE).
