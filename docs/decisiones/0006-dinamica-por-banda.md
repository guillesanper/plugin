# 0006. Dinámica por banda: detección en la entrada, curva 1:1 con rango y ganancia evaluada en cada muestra

**Estado:** Aceptada (noviembre de 2026)

## Contexto

La fase 3 añade dinámica a cada banda: su ganancia se mueve según el nivel de la señal en su zona del espectro. Había que decidir:

1. Qué escucha el detector de una banda: la entrada del ecualizador, o la señal que llega a esa banda tras las anteriores.
2. Qué forma tiene la curva de ganancia: con una relación (ratio) como un compresor clásico, o con un rango máximo.
3. Cómo se aplica la ganancia dinámica al filtro sin romper el suavizado de la fase 2 ni producir chasquidos.
4. Qué hacer con los tipos sin ganancia (cortes, notch, paso banda, paso todo).
5. Cómo encajan los parámetros nuevos en la tabla congelada.

## Decisión

1. **El detector interno escucha la entrada del ecualizador**, no la salida de la banda anterior. Así las bandas no se influyen entre sí, y el umbral se lee contra lo que muestra el analizador "pre". El detector externo escucha el bus de sidechain.
2. **Curva de umbral, rango y rodilla, sin ratio.** El exceso sobre el umbral se traslada 1:1 a la ganancia hasta agotar el rango: con rango negativo la banda limita su zona del espectro al umbral (de-esser, control de resonancias); con rango positivo la realza en la misma medida (expansión hacia arriba). La rodilla suave redondea la entrada. Una ratio, si hace falta, se puede añadir más adelante como parámetro nuevo al final de la tabla; con ratio 1:1 implícita, el comportamiento actual se conservaría.
3. **La ganancia dinámica se suma a la ganancia estática ya suavizada y se evalúa en cada muestra.** Los coeficientes de la banda se recalculan con `designBand` cuando la ganancia dinámica cambia al menos 0,001 dB (o vuelve a 0 dB exactos). Se midió que recalcular cada 8 muestras produce saltos audibles con ataques rápidos: con un ataque de 1 ms, el paso máximo entre muestras de un seno llega a 2,5 veces el de la señal limpia, mientras que con el cálculo en cada muestra es igual al de la señal limpia. Umbral, rango y rodilla se suavizan también en cada muestra, porque la ganancia los sigue 1:1: en escalones cada 32 muestras, como frecuencia y ganancia, se oía un "zipper" (−39 dB de agudos espurios, frente a −82 dB). Activar o desactivar la dinámica hace un fundido de 10 ms de su efecto.
4. **La dinámica solo actúa en los tipos con ganancia** (campana, shelves y tilt). En el resto el parámetro se conserva pero no tiene efecto, y la interfaz lo muestra desactivado. El filtro del detector depende del tipo: paso banda para la campana, paso bajo o paso alto de 12 dB/oct para los shelves, banda completa para el tilt.
5. **Siete parámetros por banda, después de los de las 24 bandas** (`bNN_dyn`, `bNN_thr`, `bNN_range`, `bNN_att`, `bNN_rel`, `bNN_knee`, `bNN_sc`), con su propia función de índice, `core::dynamicParameter()`, como preveía la [ADR 0005](0005-bandas-y-normalizacion.md). El formato de estado no cambia de versión: los estados anteriores cargan con la dinámica desactivada.

## Consecuencias

- Con la dinámica desactivada, el sonido es idéntico bit a bit al de la fase 2 (el contrato de sonido no cambia).
- Coste: con las 24 bandas dinámicas en estéreo y comprimiendo todo el tiempo, un 5 % de un núcleo a 48 kHz (frente al 0,9 % sin dinámica). Con unas pocas bandas dinámicas es despreciable. Se optimizará en la fase 5 si hace falta.
- La detección es estéreo enlazada (el canal más fuerte manda); la detección por canal llegará con el procesado L/R y M/S de la fase 4.
- Con el sidechain externo elegido y nada conectado, la banda no recibe señal y no actúa; la interfaz lo avisa.
- Cambiar más adelante a detectar la señal tras las bandas anteriores, o el filtro del detector, alteraría el sonido de proyectos guardados: habría que hacerlo como un algoritmo nuevo (ver [docs/dsp/filtros.md](../dsp/filtros.md), "Contrato de sonido").
