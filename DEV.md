# Entorno de desarrollo

Guía paso a paso para dejar el proyecto compilando, cargando en FL Studio y depurable. La plataforma principal es **Windows con Visual Studio**; al final hay notas para macOS y Linux.

Las reglas que debe respetar cualquier cambio están en [CONTRIBUTING.md](CONTRIBUTING.md), y la estructura del código en [docs/arquitectura.md](docs/arquitectura.md).

## Qué herramientas usar

| Herramienta | Para qué | Notas |
|---|---|---|
| **Visual Studio Community 2026** (o 2022) | Compilador, depurador e IDE | Gratuito. Es la opción recomendada: trae el compilador MSVC, CMake y Ninja. El CI usa la versión 2026 |
| **Git for Windows** | Control de versiones | |
| **FL Studio** (2024.1 o posterior) | Probar el plugin en el host objetivo | La versión de prueba sirve para cargar plugins |
| **pluginval** | Validar que el plugin se comporta bien en cualquier host | Opcional en local: el CI ya lo ejecuta |
| **esceq_snapshot** | Captura del editor sin abrir ningún host | Opcional; ver [CONTRIBUTING.md](CONTRIBUTING.md) |

No hace falta instalar JUCE ni Catch2: CMake los descarga al configurar.

Visual Studio Code también sirve como editor (ver más abajo), pero en Windows sigue necesitando el compilador de Visual Studio, así que lo más simple es usar Visual Studio directamente.

## Windows, paso a paso

### 1. Instalar Visual Studio

1. Descarga Visual Studio Community desde https://visualstudio.microsoft.com/es/.
2. En el instalador marca la carga de trabajo **"Desarrollo para el escritorio con C++"**.
3. En el panel derecho comprueba que están marcados:
   - MSVC (herramientas de compilación de C++ x64/x86)
   - Windows SDK
   - Herramientas de CMake de C++ para Windows
4. Instala. Son varios gigas.

### 2. Instalar Git

Descárgalo de https://git-scm.com/download/win e instálalo con las opciones por defecto.

### 3. Clonar el repositorio

Usa una ruta **corta y sin espacios**: las dependencias generan rutas largas y Windows tiene un límite de 260 caracteres (ver "Rutas largas en Windows" más abajo).

```
git config --global core.longpaths true
mkdir C:\dev
cd C:\dev
git clone https://github.com/guillesanper/plugin.git esceQ
```

### 4. Abrir y compilar en Visual Studio

1. Abre Visual Studio y elige **"Abrir una carpeta local"**. Selecciona `C:\dev\esceQ`.
2. Visual Studio detecta `CMakePresets.json` y muestra arriba un desplegable de configuración (según la versión, con el nombre corto o el descriptivo). Elige:
   - `plugin` (**Plugin completo**): para compilar y probar el plugin.
   - `plugin-debug` (**Plugin completo (depuración)**): para depurar con puntos de interrupción.
   - `core` (**Solo dsp + core**): para trabajar en algoritmos y tests. Configura en segundos porque no descarga JUCE.
3. Espera a que termine la configuración (ventana *Salida*). La primera vez descarga JUCE y tarda unos minutos.
4. **Compilar → Compilar todo** (`Ctrl+Mayús+B`). La primera compilación tarda varios minutos; las siguientes, segundos.

Resultado:

| Qué | Dónde |
|---|---|
| Plugin VST3 | `build\plugin\esceQ_artefacts\Release\VST3\esceQ.vst3` |
| Aplicación independiente | `build\plugin\esceQ_artefacts\Release\Standalone\esceQ.exe` |

Con la configuración de depuración, cambia `build` por `build-debug` y `Release` por `Debug`.

### 4 bis. Alternativa: línea de comandos

Abre **"Developer PowerShell for VS"** desde el menú Inicio (tiene el compilador en el `PATH`):

```
cd C:\dev\esceQ
cmake --preset plugin
cmake --build --preset plugin --parallel
ctest --preset plugin
```

### 5. Ejecutar los tests

- En Visual Studio: **Prueba → Explorador de pruebas → Ejecutar todas**. Los tests aparecen solo **después de compilar**: CMake los descubre ejecutando el programa de tests al terminar la compilación. Si la lista sigue vacía, mira la ventana *Salida* con "Mostrar salida de: CMake".
- En línea de comandos: `ctest --preset plugin` (o `ctest --preset core`).

Si un test de contrato falla (parámetros o estado), el cambio rompería proyectos guardados: no se arregla editando el test. Ver [CONTRIBUTING.md](CONTRIBUTING.md).

### 6. Cargar el plugin en FL Studio

Para no copiar el archivo tras cada compilación, haz que FL Studio busque directamente en la carpeta de salida:

1. En FL Studio: **Options → Manage plugins**.
2. En **Plugin search paths**, añade la carpeta `C:\dev\esceQ\build\plugin\esceQ_artefacts\Release\VST3`.
3. Pulsa **Find installed plugins**.
4. `esceQ` aparece en la lista de efectos. Cárgalo en una ranura del mezclador.

Alternativa clásica: copiar `esceQ.vst3` a `C:\Program Files\Common Files\VST3` (requiere permisos de administrador).

FL Studio bloquea el archivo mientras el plugin está cargado: **cierra FL Studio antes de recompilar**, o el enlazador fallará con un error de acceso denegado.

### 7. Depurar

**Lo más rápido: la aplicación independiente.** En Visual Studio, en el desplegable de elemento de inicio elige `esceQ.exe` (destino `esceQ_Standalone`) con la configuración de depuración y pulsa `F5`. Sirve para casi todo lo que no dependa del host.

**Dentro de FL Studio:**

1. Compila con **Plugin completo (depuración)** y añade a FL Studio la ruta `build-debug\plugin\esceQ_artefacts\Debug\VST3` (quita la de Release para no tener dos copias).
2. Abre FL Studio y carga el plugin.
3. En Visual Studio: **Depurar → Asociar al proceso** (`Ctrl+Alt+P`) y elige `FL64.exe`.
4. Los puntos de interrupción ya funcionan. Un punto de interrupción en el hilo de audio detiene el sonido: es normal.

### 8. Validar con pluginval (opcional)

Descarga `pluginval_Windows.zip` de https://github.com/Tracktion/pluginval/releases, descomprímelo y ejecuta:

```
pluginval.exe --strictness-level 10 --validate build\plugin\esceQ_artefacts\Release\VST3\esceQ.vst3
```

Debe terminar en `SUCCESS`. Es la misma comprobación que hace el CI en cada push.

### 9. Presets de usuario durante el desarrollo

Los presets de usuario se guardan en `%APPDATA%\esceQ\Presets`. Para no mezclar los de pruebas con los tuyos, define la variable de entorno `ESCEQ_PRESETS_DIR` con una ruta absoluta antes de abrir el host o la aplicación independiente: el plugin usará esa carpeta. Los tests de `tests/plugin` la apuntan a una carpeta temporal.

### Rutas largas en Windows

Es la causa más habitual de que la configuración de CMake o la compilación fallen en Windows con errores que no dicen nada claro ("no se encuentra el archivo", "the specified path is too long", `MSB3491`, `C1083`, o un `FetchContent` que se queda a medias).

Por qué pasa: Windows limita las rutas a 260 caracteres. JUCE se descarga dentro de la carpeta de compilación y crea rutas muy profundas (por ejemplo, `build\_deps\juce-build\tools\extras\Build\juceaide\...` o las del SDK de VST3). Si el repositorio está en `C:\Users\nombre\Desktop\proyectos\esceQ\esceQ`, la ruta de partida ya gasta 50 caracteres y el límite se alcanza.

Qué hacer, por orden de eficacia:

1. **Clonar en una ruta corta**, como `C:\dev\esceQ`. Es lo que resuelve el problema siempre.
2. Si no quieres mover el repositorio, **compilar en una carpeta corta** fuera de él:

   ```
   cmake -S . -B C:\b\esceq -DCMAKE_BUILD_TYPE=Release
   cmake --build C:\b\esceq --config Release --parallel
   ```

   (En Visual Studio, con "Abrir carpeta", la carpeta de compilación la fijan los presets: esta opción es para la línea de comandos.)
3. Activar las rutas largas en Git, para que el `git clone` de las dependencias no falle: `git config --global core.longpaths true`.
4. Activar las rutas largas en Windows (PowerShell como administrador, y reiniciar):

   ```
   New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem" -Name "LongPathsEnabled" -Value 1 -PropertyType DWORD -Force
   ```

   Ayuda con Git y CMake, pero **no basta por sí solo**: partes de MSBuild y del compilador siguen sin admitir rutas largas. Por eso los puntos 1 o 2 son los importantes.

Después de cambiar de ruta, borra la carpeta `build` anterior y vuelve a configurar. CMake avisa al configurar si la carpeta de compilación tiene una ruta larga.

### Versiones de Visual Studio

El CI compila en Windows con la imagen `windows-latest` de GitHub, que desde 2026 trae **Visual Studio 2026 (MSVC 19.51)**: es el mismo compilador que instala Visual Studio Community 2026. El código es C++20 estándar, y `dsp/`, `core/` y `ui/logic` se compilan con `/W4 /WX /permissive-`, así que si el CI está en verde, compilan sin avisos con ese compilador.

Con otra versión (más antigua o más nueva) puede aparecer algún aviso distinto, y en esos módulos un aviso es un error. Si te encuentras uno:

1. Copia el mensaje completo (archivo, línea y código del aviso) en una incidencia: se arregla en el repositorio.
2. Mientras tanto, para seguir trabajando, configura con `-DESCEQ_WARNINGS_AS_ERRORS=OFF`.

## Flujo de trabajo diario

| Estoy trabajando en... | Configuración | Ciclo |
|---|---|---|
| Algoritmos (`dsp/`) o parámetros y estado (`core/`) | Solo dsp + core | Editar → compilar → tests. Segundos |
| Integración con el host (`plugin/`) | Plugin completo (depuración) | Editar → compilar → aplicación independiente o FL Studio |
| Comprobación final antes de un commit | Plugin completo | Compilar → tests → probar en FL Studio |

Antes de cada `git push`: los tests pasan y el plugin carga. Después del push, revisa que el CI esté en verde en la pestaña **Actions** de GitHub.

## Visual Studio Code (alternativa)

1. Instala igualmente Visual Studio (o solo las "Build Tools for Visual Studio" con la carga de C++): VS Code no trae compilador.
2. Instala las extensiones **C/C++** y **CMake Tools**.
3. Abre la carpeta del repositorio. CMake Tools ofrece los mismos presets; elige uno, y compila con `F7`.

## macOS

1. Instala las herramientas de Xcode: `xcode-select --install`.
2. Instala CMake: `brew install cmake`.
3. Compila:

   ```
   cmake --preset plugin
   cmake --build --preset plugin --parallel
   ```

4. El VST3 y el AU quedan en `build/plugin/esceQ_artefacts/Release/`. Cópialos a `~/Library/Audio/Plug-Ins/VST3` y `~/Library/Audio/Plug-Ins/Components`.

## Linux

```
sudo apt-get install build-essential cmake ninja-build \
  libasound2-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxi-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev
cmake --preset plugin
cmake --build --preset plugin --parallel
ctest --preset plugin
```

Copia `build/plugin/esceQ_artefacts/Release/VST3/esceQ.vst3` a `~/.vst3`.

### RealtimeSanitizer

Con Clang 20 o posterior se pueden ejecutar los tests con RealtimeSanitizer, que aborta si una función del hilo de audio (las marcadas `ESCEQ_NONBLOCKING`) asigna memoria o bloquea. El CI lo hace en cada push.

```
sudo apt-get install clang-20 libclang-rt-20-dev
CC=clang-20 CXX=clang++-20 cmake -S . -B build-rtsan -DESCEQ_BUILD_PLUGIN=OFF -DESCEQ_RTSAN=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-rtsan --parallel
ctest --test-dir build-rtsan --output-on-failure
```

El test `rtsan_detecta_asignaciones` es un canario: un programa que asigna memoria a propósito en una función de tiempo real y que **debe** abortar. Si pasara sin abortar, el sanitizador no estaría actuando.

### AddressSanitizer y UndefinedBehaviorSanitizer

Detectan accesos a memoria ya liberada, fuera de ámbito o fuera de los límites, y comportamiento indefinido. El CI ejecuta así todos los tests, incluidos los del plugin. En local (Linux o macOS, con GCC o Clang):

```
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DESCEQ_BUILD_CLAP=OFF \
  "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer" \
  "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"
cmake --build build-asan --parallel --target esceq_tests esceq_plugin_tests
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure
```

### ThreadSanitizer

Detecta carreras de datos entre hilos. El CI no lo ejecuta; conviene pasarlo si se toca algo que comparten el hilo de audio, el de mensajes o un hilo del host (estado, historial, colas):

```
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DESCEQ_BUILD_CLAP=OFF \
  "-DCMAKE_CXX_FLAGS=-fsanitize=thread -fno-omit-frame-pointer" "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread"
cmake --build build-tsan --parallel --target esceq_plugin_tests
./build-tsan/tests/plugin/esceq_plugin_tests_artefacts/RelWithDebInfo/esceq_plugin_tests
```

El hilo de diseño de la fase lineal y su intercambio con el de audio se prueban en los tests de `dsp/` y `core/`:

```
cmake --build build-tsan --parallel --target esceq_tests
./build-tsan/tests/esceq_tests "Hilo de diseno*,Handoff*,TripleBuffer*,PhaseEqualizer*"
```

## Problemas frecuentes

| Síntoma | Causa y solución |
|---|---|
| CMake no encuentra un compilador | Falta la carga de trabajo de C++ en Visual Studio, o la terminal no es la "Developer PowerShell" |
| La configuración de CMake falla, o errores de ruta demasiado larga, o archivos que "no existen" dentro de `build\_deps` | Ruta larga. Clona en una ruta corta (`C:\dev\esceQ`) o compila en `C:\b\esceq`. Ver "Rutas largas en Windows" |
| Un aviso del compilador detiene la compilación con una versión nueva de Visual Studio | Ver "Versiones de Visual Studio": `-DESCEQ_WARNINGS_AS_ERRORS=OFF` y avisa con el mensaje |
| `LNK1104` o "acceso denegado" al enlazar | FL Studio tiene el plugin cargado. Ciérralo y recompila |
| FL Studio no muestra el plugin | Revisa la ruta en *Plugin search paths* y pulsa *Find installed plugins*; comprueba que es la versión de 64 bits |
| FL Studio muestra el plugin dos veces | Están añadidas a la vez las rutas de Release y de Debug |
| Tras cambiar `CMakeLists.txt` algo se comporta raro | Borra la carpeta `build` y vuelve a configurar |
| La configuración falla sin conexión | La primera vez necesita Internet para descargar JUCE y Catch2 |
| En Linux falta `X11/extensions/XInput2.h` u otra cabecera | Falta algún paquete de la lista de dependencias |
