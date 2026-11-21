# 0023. Igualar a una referencia: la entrada de la pista contra la salida de la referencia, ajuste de lo que falta sobre la curva actual, y nada guardado

**Estado:** Propuesta (noviembre de 2026). La cierra el bloque 6.1 del [plan](../../PLAN.md) con sus medidas: si cambia algo de lo de abajo, lo dice al aceptarla.

## Contexto

La sección 5.6 del plan pide medir el espectro medio de la pista y el de una referencia (otra instancia de esceQ o el sidechain) y crear las bandas que acercan uno a otro, como bandas normales y en una sola acción de deshacer, sin parámetros nuevos. El plan anterior dejaba sin decidir:

1. Qué señal de la pista se mide: su entrada o su salida.
2. Qué se hace con las bandas que ya hay.
3. Cómo se miden los dos espectros para que se puedan restar.
4. Qué algoritmo convierte la diferencia en bandas, y con qué límites.
5. En qué hilo se calcula, y dónde vive cada pieza.
6. Qué se guarda.
7. Desde dónde se abre, si la barra superior está llena.

Los puntos 1, 7 y los nombres (*Match*, *Learn*, *Apply*) los ha decidido el usuario.

## Decisión

1. **La pista se mide antes del ecualizador (su entrada); la referencia, como suena (la salida de su instancia, o el sidechain).** Medir la salida de la pista mezclaría en el promedio las curvas por las que pase mientras se aprende, y obligaría a no tocar las bandas durante el aprendizaje. Con la entrada, lo aprendido no depende de las bandas. La referencia de otra instancia es su salida porque es lo que la sesión ya publica ([ADR 0011](0011-espectros-compartidos.md)) y lo que suena en la mezcla.
2. **Se ajusta lo que falta: el objetivo menos la curva estática actual.** Las bandas que ya hay se conservan y cuentan (con la regla de canales de la compensación de ganancia, [ADR 0015](0015-compensacion-automatica-de-ganancia.md)); las nuevas van en bandas libres (apagadas). Consecuencia buscada: aplicar dos veces no crea bandas la segunda vez. Descartado "sustituir las bandas de la última igualación": exigiría marcar en el estado qué bandas son de quién.
3. **Los dos espectros se miden igual**: el analizador de 8192 puntos en la rejilla de la sesión (192 puntos de 20 Hz a 20 kHz) y su promedio exponencial en potencia de 0,4 s. La referencia de otra instancia es el espectro que ya publica la sesión; la pista y el sidechain se calculan en la interfaz con las mismas piezas. Los sesgos del método (el máximo por punto, la inclinación de 4,5 dB/oct) son los mismos en los dos y se anulan al restar. La media a largo plazo se obtiene sumando en potencia lo publicado cada 50 ms, ponderado por el tiempo, solo cuando las dos tienen señal: el promedio exponencial es lineal en potencia, así que su media es la de la señal, y no hay que tocar la sesión. Se quita la media de la diferencia: no se iguala el nivel, solo el balance.
4. **Inicio voraz y Levenberg-Marquardt sobre las curvas analíticas de `dsp`.** Campana en el punto de mayor error con el Q de su lóbulo, o shelf si el lóbulo llega a un extremo; refino conjunto de frecuencia, ganancia y Q (en log2 f, dB y log Q) con el jacobiano por diferencias de la banda que se mueve, con límites proyectados (Q de 0,3 a 16 en campanas, 0,5 a 1 en shelves, ±18 dB), en una rejilla de 384 puntos con peso 0 donde una de las dos señales no está presente (la presencia de las colisiones) y 0,1 fuera del rango elegido. Se para con un error RMS de 0,5 dB, al llegar al máximo de bandas o cuando una banda mejora menos de 0,05 dB. Es el método habitual para ajustar un ecualizador paramétrico a una respuesta objetivo (Välimäki y Reiss, 2016); un ajuste por regiones fijas (un banco de campanas en tercios de octava) sería más simple pero gastaría bandas en lo que no lo necesita y no daría bandas que el usuario quiera editar. Las curvas son las del algoritmo de filtro y la frecuencia de muestreo de la instancia: lo que se ve es lo que suena. Es determinista.
5. **El ajuste corre en un hilo propio de la instancia (`core::MatchWorker`), no en el de control** ni en la interfaz: con 24 bandas puede tardar hasta 2 s, y el hilo de control no puede retrasar tanto un FIR ni la compensación de ganancia ([ADR 0021](0021-aviso-de-latencia-sin-temporizador.md)). Una petición nueva cancela la anterior en la iteración siguiente. El algoritmo va en `dsp/curve_match.h` (puro, MIT); el aprendizaje, los ajustes y el hilo, en `core/match.h`, porque el procesador los guarda mientras está abierto y `plugin` no puede depender de `ui/logic`; el estado del panel y los cambios de los parámetros, en `ui/logic/match.h`; en `ui/src`, solo el panel y el dibujo. El hilo de audio solo escribe la mezcla del sidechain en una cola, detrás de un atómico, mientras se aprende de él.
6. **No se guarda nada.** Ni los espectros aprendidos, que no se pueden rehacer sin el audio y se quedarían viejos, ni la referencia, cuyo identificador cambia en cada sesión del host, ni los ajustes del panel. Viven en el procesador mientras el plugin está abierto, como la selección de instancias. El estado sigue en la versión 3, sin preferencias nuevas; no hay parámetros nuevos; *Apply* solo cambia parámetros, con `applyValues` y la acción registrada en el momento, como al cargar un preset.
7. **El panel se abre desde el menú del botón de comparar** (*Match to reference…*), que ya lista las otras instancias. La barra superior no tiene sitio: en la ventana mínima el navegador de presets ya está en sus 140 px ([ADR 0018](0018-analizador-ampliado.md)).

## Consecuencias

- Ningún contrato cambia: ni parámetros (364), ni estado, ni presets, ni latencia, ni el sonido de una configuración dada. El contrato de la 0.1 sigue intacto.
- Sin el panel en uso, el coste es cero: nada nuevo en `processBlock` salvo leer un atómico, y la sesión solo calcula mientras alguien mira.
- Hereda los límites de la sesión: solo se ven las instancias del mismo proceso y formato. El sidechain cubre lo demás.
- Los espectros son mono: no se iguala la imagen estéreo, y las bandas creadas son Stereo.
- Con 8192 puntos, por debajo de unos 60 Hz la resolución es de pocos bins por tercio de octava; el rango por defecto empieza en 30 Hz y el suavizado de 1/12 de octava no es fiable ahí. Lo medirá el bloque 6.1.
- Cargar un archivo de audio como referencia queda fuera; se puede añadir como otra fuente del mismo aprendizaje.
