# 0015. Compensación automática de ganancia: curva estática ponderada, hilo de control y parámetro fuera de los presets

**Estado:** Aceptada (noviembre de 2026)

## Contexto

La sección 5.5 del [plan](../../PLAN.md) pide un interruptor global, `auto_gain`, que mantenga la sonoridad igual al ecualizar, para comparar con el bypass sin que "más fuerte" parezca "mejor". El plan ya fijaba la fórmula (la potencia de ruido rosa ponderada con K de BS.1770, a partir de la curva estática), el límite de ±12 dB, que la dinámica no se compensa y que el hilo de audio aplica la ganancia con la rampa que ya existe. Al implementarlo quedaban por decidir:

1. Cómo se calcula la integral con precisión, también con bandas estrechas.
2. Qué potencia se toma con bandas en L, R, M o S.
3. En qué hilo se calcula, y cómo llega al hilo de audio sin bloquearlo y sin que un render dependa de la velocidad de ese hilo.
4. Cómo se guarda y qué pasa con los presets y los proyectos anteriores.
5. Dónde va el control, en una barra superior que ya estaba llena.

## Decisión

1. **Trapecio en `log2 f` con una rejilla refinada alrededor de cada banda.** 24 puntos por octava de 20 Hz a 20 kHz y, a menos de 4 anchos de banda (`1/Q` octavas) de cada banda, 16 puntos por ancho de banda; en shelves y tilt la zona se ensancha `|ganancia|/40` décadas. Peor error medido con 1500 curvas aleatorias: 0,005 dB. Se descartaron:
   - *Una rejilla uniforme:* con 24 puntos por octava se equivoca hasta en 3,8 dB con una campana de Q 30 (se cuenta de más o de menos según caiga el pico entre los puntos); con 96, todavía 0,44 dB, y cuatro veces más puntos en toda la banda. Tabla en [compensacion-de-ganancia.md](../dsp/compensacion-de-ganancia.md).
   - *Simpson:* con la misma rejilla da más error en los picos estrechos que el trapecio.
   - *Medir el audio:* lo descartaba ya el plan, porque bombearía con la música.

   Si ninguna banda cambia la magnitud (desactivadas, pasa todo, campanas, shelves y tilt a 0 dB), el resultado es exactamente 0 y no una integral que da casi 0: así la salida con la compensación encendida y la curva plana es bit a bit la de apagada.

2. **Media de la potencia de los dos canales de salida para entradas incorreladas.** Sin Mid ni Side, la media de `|H_L|²` y `|H_R|²`; con Mid o Side, la matriz de 2×2 de las magnitudes de cada banda, la misma composición que la fase lineal. Es exacto para una banda en M o S (o una en M y otra en S) también con la fase de los IIR, y una aproximación cuando se mezclan L o R con M o S. Se descartó suponer una entrada mono (L = R): una banda en Side no compensaría nada aunque cambie la mezcla, y una en Mid se compensaría entera. En mono cuentan las bandas que procesan el único canal (Stereo, Left, Mid).

3. **Un hilo de control por instancia, que es el de diseño de la fase lineal generalizado.** `core::PhaseDesignThread` pasa a ser `core::ControlThread`: cada 10 ms diseña los FIR pendientes (`PhaseEqualizer::serviceDesigns()`) y calcula la compensación pendiente (`dsp::AutoGain::service()`). El hilo de audio pide el cálculo cuando cambian los parámetros y la compensación está encendida, y lee el último resultado de un atómico; lo aplica sumado a `output_gain` en la misma `dsp::GainRamp` (20 ms), igual en los cuatro modos de fase. **Al renderizar sin tiempo real y al preparar el audio** se calcula en el momento (`flush()`, como el diseño de los FIR): el render no depende de la velocidad del hilo. Al encender la compensación, la ganancia no se mueve hasta que llega el valor de los ajustes de ese momento (`AutoGain::restart()`), unos 10 ms; después va con la rampa. Se descartaron:
   - *El hilo de audio:* el cálculo cuesta de 13 µs (una campana) a 75 µs (24 campanas), y hasta 4 ms en el peor caso (24 shelves de Q 30). En tiempo real, y con un parámetro automatizado, se pagaría en cada bloque.
   - *El hilo de mensajes:* algunos hosts lo tienen ocupado durante segundos y no corre durante un render: una exportación con automatización no seguiría la curva.
   - *Otro hilo más por instancia:* duplicaría el bucle, el cierre y `flush()` del de diseño. Uno solo que hace las dos cosas sirve: un diseño de High (13 ms) retrasa como mucho eso la compensación, que de todos modos entra con una rampa de 20 ms.

   **Con bypass, no hay compensación,** como con `output_gain`: el bypass es la referencia con la que se compara.

4. **Un parámetro, `auto_gain` ("Auto Gain"), al final de la tabla, apagado por defecto y fuera de los presets.** Un parámetro y no una línea del estado (como el algoritmo de filtro, [ADR 0014](0014-estado-v3-algoritmo-de-filtro.md)): aquí un solo valor por defecto, apagado, vale para lo antiguo y para lo nuevo, y automatizarlo no hace daño. El estado sigue en la versión 3: un parámetro más, con su valor por defecto si falta, así que **los proyectos y presets anteriores cargan con la compensación apagada y suenan igual**. Va en `core::kParametersOutsidePresets`, como el modo de fase ([ADR 0012](0012-modos-de-fase.md)): elegir un preset no debe encender ni apagar la compensación (todos los presets anteriores la apagarían, porque no la llevan), ni cambiar de golpe el volumen mientras se comparan presets con ella encendida.

5. **Un botón "Auto" en la barra superior, junto a la salida, que muestra la compensación; el tamaño de la ventana pasa al menú de opciones.** El botón se queda encendido y dice "Auto −2.3 dB" mientras la compensación está encendida (y "Auto" cuando no). El valor lo calcula la interfaz con la misma función, a partir de los mismos parámetros, como la curva: se ve aunque el host no esté procesando audio, y es el que suena. Para que quepa sin ensanchar la ventana mínima (1120 px) ni el tamaño M (1180 px), el selector S/M/L sale de la barra y pasa al menú de opciones, en una sección *Window size*: es una preferencia que se toca poco, y la ventana se sigue pudiendo redimensionar desde la esquina. Se descartaron:
   - *Ensanchar la barra:* con el selector S/M/L en ella, el botón y su separación piden unos 100 px más; el ancho mínimo pasaría de 1120 a unos 1220 px, y el tamaño M, que hoy es 1180, también tendría que crecer.
   - *El interruptor en el menú de opciones y solo el valor en la barra:* lo que se usa al comparar debe estar a un clic, y el valor no tendría sitio igualmente.
   - *Mostrar el valor en el campo de la ganancia de salida:* ese campo es editable y es otro parámetro.

## Consecuencias

- Contratos: un identificador nuevo al final (`auto_gain`), en el fixture de parámetros. El estado sigue en la versión 3 y el contrato de la 0.1 no cambia. Un proyecto anterior abre con la compensación apagada y suena bit a bit igual.
- Coste en el hilo de audio con la compensación apagada: una comparación por bloque. Encendida, copiar los ajustes de las bandas (unos 3 kB) en los bloques en que cambian, y una lectura atómica por bloque. El cálculo, en el hilo de control: de 13 µs a 4 ms por cambio, según la curva.
- En fase lineal la compensación se aplica a la salida, así que al mover una banda la ganancia cambia antes de que llegue el audio con la curva nueva (la latencia del modo, hasta 363 ms en High). Pasa igual con `output_gain`; con la curva quieta no hay ninguna diferencia.
- La compensación es una estimación para ruido rosa: con música de otro espectro (un bajo solo, un charles) la sonoridad puede cambiar algo. Es lo que se espera de una compensación que no bombea.
- El selector del tamaño de la ventana está en el menú de opciones; el tamaño se sigue guardando igual (`ui.width` y `ui.height`).
