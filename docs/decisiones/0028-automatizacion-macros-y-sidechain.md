# 0028. Integración con el host: deshacer por diferencias, macros que modulan sin escribir en los parámetros, y el sidechain con ganancia, filtro del detector propio y escucha

**Estado:** Propuesta (noviembre de 2026), con la sección 5.9 del [plan](../../PLAN.md). El alcance lo ha decidido el usuario; cada bloque de la fase 7 la acepta en lo que le toca, con sus medidas. **Aceptadas las decisiones 1 y 2** con el bloque 7.1 (noviembre de 2026; la 2, con un cambio decidido con el usuario: ver la nota del final); el resto sigue propuesto.

## Contexto

El usuario quiere manejar esceQ desde FL Studio con automation clips y con sus controladores (*Peak Controller*, *Formula Controller*, *Fruity LFO*, *Link to controller*), y mejorar el sidechain. Los 391 parámetros ya son parámetros del host y la interfaz marca sus gestos, así que lo básico funciona. Lo que no aguanta un parámetro moviéndose sin parar:

1. **Deshacer.** `EditHistory` registra instantáneas del estado entero tras cada gesto, y deshacer aplica la anterior: vuelve a poner todos los parámetros que difieran, también los que mueve el host, y lo hace con gestos (el host puede grabarlo como un cambio del usuario).
2. **Fase lineal.** Cada cambio de una banda pide un FIR nuevo (de 0,2 a 13 ms de diseño). En tiempo real se diseña el último ajuste cada 10 ms; al renderizar, en cada bloque con cambios, y con una banda modulada el render puede ir más lento que el tiempo real.
3. **Un controlador, un parámetro.** En FL, cada *Link to controller* mueve un solo parámetro: bajar tres bandas con un *Peak Controller* pide tres enlaces con el mismo ajuste.
4. **Sidechain.** El bus está desactivado por defecto y no se ha comprobado en FL; "No sidechain" no sale con el bus activo y sin señal; no se ve cuánto llega, ni se puede oír lo que escucha el detector, ni hacer que escuche otra zona que la de la banda.

## Decisión (propuesta)

1. **Deshacer por diferencias.** Una acción guarda, de cada parámetro que tocaron sus gestos, el valor de antes y el de después (y el algoritmo de filtro y las asignaciones de las macros, si cambian). Deshacer y rehacer cambian solo esos. Lo que mueve el host sin gestos no entra en ninguna acción ni se toca al deshacer.
2. **Al renderizar, el diseño al ritmo del control.** El FIR de fase lineal y la compensación automática se rediseñan como mucho una vez por cada 10 ms de audio (`ControlThread::kPeriod`, contado en muestras), y no en cada bloque. El render sigue sin depender de su velocidad.
3. **Ocho macros que modulan sin escribir.** `macro_1` a `macro_8`, de 0 a 100 %, 0 por defecto. Cada una mueve hasta 8 destinos con una profundidad de −100 a +100 %, sumada en el espacio normalizado del host y limitada a 0..1. El valor que suena es el del parámetro más la modulación; el del host no cambia. Solo se modulan los parámetros continuos que no cambian la estructura ni la latencia (lista en la sección 5.9).
4. **Las asignaciones, en el estado versión 4,** en líneas `macro.N.M <id> <profundidad>`: cambian el sonido sin ser parámetros, como `filter_algorithm` en la [0014](0014-estado-v3-algoritmo-de-filtro.md). Los presets de usuario las llevan; los de fábrica, ninguna.
5. **Sidechain:** `sc_gain` (−24 a +24 dB) antes de todos los detectores; un medidor de su nivel; el aviso con tres estados (bus desactivado, en silencio más de 1 s, con señal); y el bus activo o no por defecto, según lo que se compruebe en FL.
6. **Filtro del detector propio por banda:** `bNN_det_own` (Linked u Own), `bNN_det_freq` y `bNN_det_q`: con Own, el detector escucha un paso banda en esa frecuencia en lugar del filtro de la banda. No se aplica en las bandas espectrales.
7. **Escuchar el detector:** *Listen*, en la interfaz y sin guardar, sustituye la salida por la señal del detector de una banda, con la latencia de lo que suena. No actúa al renderizar sin tiempo real.

## Alternativas

- **Macros que escriben en los parámetros** (moverlas cambia el valor que ve el host): el host grabaría la modulación como automatización, chocaría con un automation clip del mismo parámetro y llenaría el historial. Descartada.
- **Un modulador interno** (seguidor de envolvente o LFO que mueve las macros): duplica lo que ya hacen los controladores del host; el usuario lo deja fuera. Se puede añadir después sobre las macros sin cambiar su formato.
- **Las asignaciones como parámetros** (destino y profundidad automatizables): cientos de parámetros más, y automatizar un destino no tiene sentido musical. Descartada.
- **Mantener el estado en la versión 3** con las líneas de las macros como claves nuevas: una versión anterior las ignoraría igual, pero el número de versión dejaría de decir qué cambia el sonido. Descartada, salvo que el bloque 7.2 encuentre una razón.
- **Automatización a nivel de muestra** (repartir el bloque en cada punto de automatización): JUCE entrega el último valor de cada bloque en VST3, y el suavizado de los parámetros ya evita los saltos. No se hace.

## Consecuencias

- 81 parámetros nuevos (472), todos al final y con un valor por defecto que no cambia el sonido, y el estado en la versión 4: los contratos de la 0.1.0 y la 0.5.0 se mantienen, y la versión que cierre la fase (0.6.0) añade el suyo.
- El historial deja de ser una pila de instantáneas: hay que migrar sus tests, y la acción de *Apply* de la igualación y la carga de un preset pasan a ser diferencias de muchos parámetros.
- El procesador aplica las macros al traducir los parámetros en cada bloque: sin asignaciones, no cuesta nada; con ellas, unos microsegundos, y la fase lineal rediseña al ritmo de la modulación.
- Queda por comprobar con el usuario, en FL: el enrutado del sidechain (VST3 y CLAP, mono y estéreo), los controladores enlazados a un parámetro y a una macro, y qué ve *Last tweaked*.

## Nota del bloque 7.1 (noviembre de 2026): deshacer por diferencias y el diseño al ritmo del control

**Deshacer por diferencias (decisión 1), hecho como se propuso.** `core::UndoHistory` guarda acciones (`core::EditAction`): de cada parámetro tocado, el valor de antes y el de después, saneados, y el algoritmo de filtro si cambió; deshacer devuelve la acción al revés. `EditHistory` anota el valor de un parámetro al empezar su primer gesto de la acción (`core::PendingAction`), y al registrarla, los gestos que sigan abiertos empiezan la acción siguiente desde su valor de ese momento. `PluginProcessor::applyEdit` pone solo esos parámetros, cada uno en su gesto si cambia; los demás no se tocan ni abren gestos. Siguen siendo una acción el arrastre, la ráfaga de rueda, la banda nueva, el pico, el *Apply* de la igualación, la carga de un preset (aparte de lo anterior) y el cambio de algoritmo: sus tests de siempre pasan sin cambios. Dos tests nuevos: en `tests/plugin/history_test.cpp`, con el host moviendo un parámetro sin gestos entre las acciones y durante el deshacer, deshacer y rehacer no lo cambian y no abren ningún gesto sobre él; en `tests/core/history_test.cpp`, lo mismo con `PendingAction` y `UndoHistory` solos. Las asignaciones de las macros entrarán en `EditAction` con el bloque 7.2.

**Al renderizar, el diseño al ritmo del control (decisión 2), con un cambio.** Medido con `esceq_bench --modulation` (una campana a 1 kHz con la ganancia a ±6 dB y 2 Hz, ajustada en cada bloque; Release, GCC, 4 núcleos): un diseño de *Linear High* tarda 2,9 a 3,3 ms a 48 kHz (unos 17 ms a 192 kHz), y dos tercios se van en la FFT. Ningún intervalo razonable cumple el criterio del plan («menos del doble de tiempo que sin modular»): sin modular, 60 s en *Linear High* se renderizan en 0,3 s, y haría falta un diseño cada 250 ms o más, que se oiría a escalones. Además, un FIR nuevo solo entra al empezar un bloque de la convolución (1024 muestras en *Linear High* a 48 kHz, 21 ms): diseñar cada 10 ms tira la mitad de los diseños. Decidido con el usuario: **el periodo es `kPeriod` (10 ms) o el bloque de la convolución del modo, lo que sea más largo**, y el criterio pasa a ser estas medidas; abaratar el diseño (la FFT) queda apuntado en el plan para más adelante. `ControlThread::renderBlock()` lo cuenta en muestras; un cambio tras un periodo sin diseños se diseña en su bloque, así que lo que no se mueve sin parar sale bit a bit como antes, y mientras se renderiza el hilo propio no diseña (el resultado no depende de él).

Render de 60 s a 48 kHz, en s (diseños):

| Modo | Bloque | Sin modular | En cada bloque (0.5.0) | Al ritmo | Dos renders iguales |
|---|---|---|---|---|---|
| Linear Low | 256 | 0,17 | 2,41 (11 249) | 1,39 (5625) | sí |
| Linear Low | 512 | 0,21 | 1,23 (5624) | 1,40 (5624) | sí |
| Linear Medium | 256 | 0,23 | 9,45 (11 249) | 5,56 (5625) | sí |
| Linear Medium | 512 | 0,31 | 4,67 (5624) | 4,33 (5624) | sí |
| Linear High | 256 | 0,30 | 34,91 (11 249) | 9,62 (2813) | sí |
| Linear High | 512 | 0,30 | 18,17 (5624) | 9,75 (2812) | sí |

A 96 y 192 kHz (5 s de audio), en % de la duración del audio:

| Modo | Hz | Bloque | En cada bloque (0.5.0) | Al ritmo |
|---|---|---|---|---|
| Linear Medium | 96 000 | 256 | 56,4 % | 15,5 % |
| Linear Medium | 192 000 | 256 | 253,9 % | 35,0 % |
| Linear Medium | 192 000 | 1024 | 64,3 % | 32,9 % |
| Linear High | 96 000 | 256 | 260,1 % | 35,1 % |
| Linear High | 96 000 | 1024 | 68,4 % | 34,3 % |
| Linear High | 192 000 | 256 | 1276,7 % | 84,5 % |
| Linear High | 192 000 | 1024 | 325,8 % | 82,8 % |

Con el ritmo nuevo, ningún caso medido va más lento que el tiempo real (el peor, *Linear High* a 192 kHz, al 85 %); hasta la 0.5.0, a 192 kHz y bloques de 256, iba 12,8 veces más lento. Sin modular, todos por debajo del 2,5 %.

En tiempo real (bloques al ritmo del reloj, 2 s, 48 kHz, bloques de 256 y 512), con la misma banda modulada, el hilo de control diseña unas 90 veces por segundo en *Low* y *Medium* y de 54 a 68 en *High* (cada diseño tarda más que el periodo), y ocupa un 1,5 %, un 6,5 % y del 17 al 22 % de un núcleo; el hilo de audio, del 0,1 % (IIR) al 1,5 % (*High*), con la llamada más lenta por debajo de 0,75 ms. Eso no cambia con este bloque; está en el README (*Automatizar y controlar desde FL Studio*), con lo que no conviene automatizar.

