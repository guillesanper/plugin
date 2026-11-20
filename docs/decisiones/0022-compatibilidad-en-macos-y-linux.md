# 0022. Compatibilidad en macOS y Linux: Linux compilado en Ubuntu 22.04, paquetes de macOS firmados ad hoc y comprobados, y LV2 aplazado

**Estado:** Aceptada (noviembre de 2026)

## Contexto

La fase 5 termina con macOS y Linux al nivel de Windows. El CI ya compilaba, validaba y empaquetaba los dos ([ADR 0010](0010-distribucion.md)), pero nadie había comprobado qué hacía falta para que los paquetes cargaran en el equipo de un usuario. Revisado en noviembre de 2026:

1. **Linux: el binario solo cargaba en distribuciones de 2024 o posteriores.** Se compilaba en el runner `ubuntu-24.04`, con su glibc (2.39) y su GCC 13. El VST3 y el CLAP pedían `GLIBC_2.38` (`__isoc23_strtol`, `__isoc23_strtoll` y `fmodf`, que las cabeceras de la 2.38 redirigen a símbolos nuevos) y `GLIBCXX_3.4.32` (`std::ios_base_library_init`, de la libstdc++ de GCC 13). Ubuntu 22.04 (glibc 2.35, soporte hasta 2027) y Debian 12 (2.36), las versiones más usadas, no los habrían cargado: el host habría dicho que el plugin no existe o no es válido, sin más.
2. **Linux: dependencias del sistema.** Además de la biblioteca de C y la de C++, el plugin enlaza con FreeType y Fontconfig (JUCE las usa para el texto). Nada lo decía.
3. **macOS: comprobaciones a medias.** El CI comprobaba con `lipo` las dos arquitecturas del VST3, pero no las del AU ni las del CLAP, ni la versión mínima de macOS de cada una, ni la firma. Con un generador que no es Xcode (el CI usa Make), JUCE no pide la firma ad hoc del enlazador: el enlazador firma ad hoc el binario arm64 (lo exige Apple Silicon), pero el paquete (bundle) no queda sellado, y el x86_64 no queda firmado. Al añadir la comprobación de la versión mínima salió lo peor: el binario arm64 pedía **macOS 26.0**, la versión del SDK del runner, y no habría cargado en un Mac con Apple Silicon y macOS 11 a 15. `CMakeLists.txt` fijaba `CMAKE_OSX_DEPLOYMENT_TARGET` en 11.0 con `set(... CACHE ...)` después de `project()`, y en macOS `project()` ya crea esa entrada de la caché (vacía), así que la línea no hacía nada. Afecta también a los paquetes de la 0.1.0.
4. **macOS: notarización y paquete `.pkg`.** Sin una cuenta de desarrollador de Apple no se puede firmar con un certificado ni notarizar.
5. **Linux: LV2.** La sección 1 del [plan](../../PLAN.md) lo pone entre los formatos de Linux.

## Decisión

1. **Linux se compila en un contenedor `ubuntu:22.04` con su compilador, GCC 11.** Lo que se compila con una glibc carga con esa y las posteriores, así que el VST3 y el CLAP piden como mucho `GLIBC_2.35` y `GLIBCXX_3.4.30`, y cargan en Ubuntu 22.04, Debian 12, Fedora 36 o posteriores. Lo comprueba en cada push `packaging/unix/check-linux-binary.sh`: falla si un binario pide versiones posteriores o enlaza con una biblioteca fuera de la lista (las del sistema base, FreeType y Fontconfig). Con el binario de antes falla (pide 2.38 y 3.4.32); con el nuevo pasa. El job de Linux hace en el contenedor todo lo que hacía: compilar, tests, pluginval, clap-validator y paquete. Los jobs `core`, `rtsan` y `sanitizers` siguen en `ubuntu-24.04`: no publican nada.

   Se descartaron:

   - **El runner `ubuntu-22.04`.** GitHub lo retira: en deprecación desde septiembre de 2026, con cortes programados, y sin soporte en abril de 2027.
   - **GCC 12 en el contenedor.** El `ar` del sistema carga el plugin de LTO de GCC 11, que no entiende los objetos de GCC 12, y la biblioteca intermedia de JUCE queda sin símbolos (falla el enlazado). GCC 12 da además un falso aviso `-Wrestrict` (bug 105651 de GCC) en `"+" + std::string` en `core::formatValue`. Esa expresión se ha reescrito igualmente, con el mismo resultado, para que compile sin avisos con cualquier GCC.
   - **Enlazar libstdc++ de forma estática.** No hace falta con la de GCC 11, que tienen todas las distribuciones soportadas, y meter una segunda biblioteca de C++ en el proceso del host es una fuente de problemas.
   - **Forzar versiones antiguas de los símbolos (`.symver`) desde Ubuntu 24.04.** Frágil: depende de las cabeceras de la glibc instalada.

   El sonido es el mismo: con GCC 11 pasan los tests de respuesta en frecuencia y los golden, también el del contrato de la 0.1 (`release_contract_test.cpp`). GCC no contrae en FMA sin `-march`, y x86-64 sin `-march` no tiene FMA.

2. **Requisitos escritos en el paquete.** `INSTALL.txt` dice qué versión de cada sistema hace falta y, en Linux, las dos bibliotecas (`libfreetype6` y `libfontconfig1`, que trae cualquier escritorio). La interfaz usa X11, como la de cualquier plugin VST3 o CLAP en Linux: el host le da una ventana de X11. En Wayland funciona por XWayland, que traen los escritorios. Se comprueba con xvfb (X11) en el CI.

3. **macOS: la versión mínima se fija antes de `project()`, y cada formato se firma ad hoc y se comprueba antes de validarlo.** Después de compilar, el CI comprueba en el VST3, el AU y el CLAP que el binario lleva arm64 y x86_64, que las dos piden macOS 11.0 (`vtool -show-build`), firma el paquete entero ad hoc (`codesign --force --sign -`, sin certificado) y verifica la firma (`codesign --verify --strict`). pluginval, auval y clap-validator validan los paquetes ya firmados, que son los que se empaquetan.

4. **Sin notarización ni `.pkg` mientras no haya cuenta de desarrollador.** Un `.pkg` sin firmar también lo bloquea Gatekeeper al abrirlo y pide permisos de administrador para instalar en `/Library`; no ahorra al usuario el paso manual, solo lo cambia de sitio. El `.zip` con las instrucciones, que dicen cómo quitar la cuarentena, se queda. Cuando haya cuenta de Apple, firmar con certificado, notarizar y hacer un `.pkg` es un cambio solo del CI.

5. **LV2, aplazado.** Ardour, Reaper y Bitwig cargan VST3 o CLAP en Linux. LV2 añadiría un identificador congelado más (su URI), un validador más en el CI y otro formato que mantener, sin ningún host que lo necesite. Se reconsiderará si los usuarios lo piden.

## Consecuencias

- Un usuario de Ubuntu 22.04 o Debian 12 puede cargar esceQ. Si una dependencia nueva subiera las versiones necesarias, el CI fallaría antes de publicar nada.
- El job de Linux instala sus dependencias en el contenedor y CMake con pip (la de Ubuntu 22.04 es la 3.22; el proyecto pide la 3.24). Cuando Ubuntu 22.04 deje de tener soporte (2027), se subirá la imagen y, con ella, las versiones mínimas, y se dirá en el CHANGELOG.
- En macOS, el usuario sigue teniendo que quitar la cuarentena a mano. Lo que no se puede comprobar en el CI se comprueba en un Mac de verdad: que el VST3, el AU y el CLAP cargan en un host (Logic, Reaper, FL Studio) en Apple Silicon y en Intel después de quitar la cuarentena.
- En Linux, lo que tampoco se puede comprobar en el CI: que carga en un host real (Reaper, Bitwig, Ardour) con X11 y con Wayland.
