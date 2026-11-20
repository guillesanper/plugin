# 0021. Aviso de latencia sin temporizador, y pluginval con el arreglo de JUCE en Linux

**Estado:** Aceptada (noviembre de 2026)

## Contexto

El CI tenía dos fallos intermitentes, los dos en el job `plugin`:

1. **macOS, `esceq_plugin_tests`** (ejecución 33): "Fase: cambiar de modo con el audio en marcha avisa al host desde el hilo de mensajes" daba `getLatencySamples() == 0` en vez de 1280. El procesador avisaba al host de un cambio de latencia con un temporizador de 20 Hz en el hilo de mensajes ([ADR 0012](0012-modos-de-fase.md), punto 5), y el test dejaba correr el hilo de mensajes un tiempo fijo, 150 ms, contando con que el temporizador saltara en ese tiempo. Un temporizador de JUCE no garantiza su periodo: en un runner cargado, 150 ms no bastaban. La cabecera prometía lo mismo ("como mucho 1/kLatencyPollHz segundos después"), y tampoco era cierto.

   Reproducido en Linux: con el test fijado a una CPU ocupada por cuatro bucles y con prioridad baja (`taskset -c 0 nice -n 19`), falla 6 de cada 30 veces con el mismo mensaje.

2. **Linux, pluginval nivel 10** (ejecución 36): después de `SUCCESS`, segfault (código 139) en el hilo de mensajes de pluginval, dentro de `juce::RunLoop::Impl::registerEventHandler(...)::{lambda(int)#1}`, en el `operator[]` de un `unordered_map`, llamado desde `LinuxEventLoop::registerFdCallback` en `MessageManager::runDispatchLoop`.

   No es código de esceQ. `RunLoop` es la implementación del host de `Steinberg::Linux::IRunLoop`: el plugin le pide que vigile sus descriptores (la cola de mensajes de su copia de JUCE y la conexión con X11) y el host llama al plugin cuando hay algo que leer. pluginval 1.0.4 está compilado con JUCE 8.0.3 (`strings` lo dice), y en esa versión la función que registra captura `this` (el `Impl`) y busca el descriptor en el mapa sin comprobar nada: desensamblada en el binario de pluginval, es un `operator[]` directo sobre el mapa. El bucle de eventos de Linux copia la lista de funciones a llamar antes de llamarlas. Si en la misma vuelta hay que leer de un descriptor del host cuyo mensaje destruye la última instancia del plugin (y con ella el `Impl`) y también de un descriptor del plugin, la segunda llamada usa memoria liberada. JUCE lo arregló en la 9.0.2 con un `weak_ptr` al mapa, con un comentario que describe exactamente este caso (`juce_VST3PluginFormatImpl.h`, la versión que usa esceQ). pluginval no tiene una versión estable posterior: la siguiente es la 2.0.0, que en noviembre de 2026 sigue en beta (beta 3, con JUCE 9.0.3).

   Reproducido en local con pluginval 1.0.4 bajo xvfb y gdb, con la misma pila, cuando la máquina está cargada (compilando a la vez): 1 de cada 15 o 20 ejecuciones, al final de una prueba, sobre todo de *Fuzz parameters*. Lo que pone el plugin es la otra mitad de la condición: mensajes pendientes en su cola justo al destruirlo. Al principio parecía que los ponía el temporizador de 20 Hz del procesador (punto 1), que mandaba un mensaje cada 50 ms aunque no pasara nada. Sin el temporizador sigue cayendo igual (6 de 95 ejecuciones, misma pila): cambiar parámetros deja siempre avisos de JUCE pendientes (el envoltorio VST3, el historial de deshacer), y eso le pasa a cualquier plugin hecho con JUCE. El arreglo tiene que estar en el host.

## Decisión

1. **El hilo de control avisa de la latencia, y el hilo de mensajes se lo dice al host.** `core::ControlThread` recibe, si se le da, una función que llama una vez por cada cambio de `dsp::PhaseEqualizer::latencySamples()` respecto al último visto. Lo comprueba en cada vuelta (cada `ControlThread::kPeriod`, 10 ms, o en `flush()`), después de entregar los diseños: la latencia cambia en el hilo de audio al adoptar el modo nuevo. El procesador le da una que pide un `juce::AsyncUpdater` (`LatencyReporter`), y este, en el hilo de mensajes, llama a `setLatencySamples()` con la latencia del motor si no es la que conoce el host. Sin cambios de latencia, el procesador no manda ningún mensaje al hilo de mensajes.
2. **Tests deterministas.** `PluginProcessor::flushControlThread()` hace ya, en el hilo que llama, el trabajo del hilo de control (`ControlThread::flush()`). El test del cambio de modo lo usa: el aviso queda en la cola de mensajes antes de dejar correr el hilo de mensajes, y no depende de cuándo despierte ningún hilo. Que el hilo propio también avisa lo comprueba otro test, con un límite de 10 s que solo sirve para detectar que no avisa. En `tests/core/control_thread_test.cpp`, que el aviso llega una vez por cambio y ninguna sin cambios.
3. **En Linux, pluginval 1.0.4 se compila con el arreglo de JUCE.** `tools/pluginval/build.sh` descarga pluginval 1.0.4 y su JUCE 8.0.3, fijados por commit, aplica `tools/pluginval/juce-8.0.3-vst3-runloop.patch` (el cambio de JUCE 9.0.2 en `RunLoop::Impl`, línea a línea, y nada más) y lo compila. El CI guarda el resultado en la caché, con la clave del script y del parche, así que solo compila cuando cambian. Los tests de pluginval son los de la 1.0.4, como en Windows y macOS, donde se sigue usando el binario publicado: `RunLoop` solo existe en Linux. El paso sigue bajo `gdb`, por si aparece otro cierre.

Se descartaron:

- **Esperar más en el test** (o esperar a que se cumpla, sin el aviso). Seguiría dependiendo de cuándo salta un temporizador y no arreglaría la cabecera, que prometía un plazo.
- **Avisar desde el hilo de audio.** `triggerAsyncUpdate()` y `setLatencySamples()` toman cerrojos y avisan al host: no se hacen en tiempo real.
- **Arrancar el temporizador solo al cambiar de modo.** El cambio de modo puede llegar por automatización en el hilo de audio, que no puede arrancar un temporizador; y el hilo de control ya mira la latencia cada 10 ms sin coste.
- **Usar pluginval 2.0.0-beta3** (JUCE 9.0.3, ya con el arreglo). Cambiaría el validador de las releases por una versión sin publicar, con otros tests.
- **Compilar pluginval 1.0.4 con JUCE 9.** pluginval 1.0.4 no está hecho para JUCE 9; serían más cambios que el arreglo.
- **Dejar pluginval 1.0.4 como está y documentarlo.** El job de Linux fallaría de vez en cuando por algo ajeno a esceQ, y un CI que falla solo deja de mirarse.
- **Evitarlo desde el plugin.** No se puede: basta con que el host destruya la instancia con un aviso de JUCE pendiente.

## Consecuencias

- El host conoce la latencia nueva como mucho unos 10 ms (lo que tarda el hilo de control en verla) más lo que tarde el hilo de mensajes en atender el aviso después de que el audio cambie de modo, en vez de hasta 50 ms. Ni el sonido, ni el estado, ni los parámetros cambian.
- Una instancia quieta ya no despierta al hilo de mensajes 20 veces por segundo.
- El test de macOS, si falla, falla por algo de esceQ: lo que antes era un plazo ahora es una cadena de pasos que el test hace él mismo.
- El job de Linux compila pluginval cuando no está en la caché (unos minutos). Cuando salga una pluginval estable con JUCE 9.0.2 o posterior, se vuelve al binario publicado y se borran `tools/pluginval/`, el paso de la caché y esta excepción.

## Medidas

En el equipo de desarrollo (Linux x86-64, cuatro núcleos), con pluginval nivel 10 bajo xvfb y gdb, y con la máquina compilando a la vez, que es cuando aparece el cierre:

| Validador | Plugin | Ejecuciones | Cierres en `RunLoop` |
|---|---|---|---|
| pluginval 1.0.4 publicado | esceQ con el temporizador | 32 | 1 |
| pluginval 1.0.4 publicado | esceQ sin el temporizador | 95 | 6 |
| pluginval 1.0.4 con el parche | esceQ sin el temporizador | 49 | 0 |

Las ejecuciones del validador publicado y del parcheado se alternan, con el mismo plugin y la misma carga. El test del cambio de modo, fijado a una CPU ocupada (`taskset -c 0 nice -n 19`, con cuatro bucles de carga en esa CPU): la versión anterior falla 8 de 60 veces; la nueva, 0 de 30.
