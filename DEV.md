# Entorno de desarrollo

Guía paso a paso para dejar el proyecto compilando, cargando en FL Studio y depurable. La plataforma principal es **Windows con Visual Studio**; al final hay notas para macOS y Linux.

Las reglas que debe respetar cualquier cambio están en [CONTRIBUTING.md](CONTRIBUTING.md), y la estructura del código en [docs/arquitectura.md](docs/arquitectura.md).

## Qué herramientas usar

| Herramienta | Para qué | Notas |
|---|---|---|
| **Visual Studio 2022 Community** (o posterior) | Compilador, depurador e IDE | Gratuito. Es la opción recomendada: trae el compilador MSVC, CMake y Ninja |
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

Usa una ruta **corta y sin espacios**: las dependencias generan rutas largas y Windows tiene un límite de 260 caracteres.

```
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

## Problemas frecuentes

| Síntoma | Causa y solución |
|---|---|
| CMake no encuentra un compilador | Falta la carga de trabajo de C++ en Visual Studio, o la terminal no es la "Developer PowerShell" |
| Errores de ruta demasiado larga al descargar JUCE | Clona en una ruta corta (`C:\dev\esceQ`) |
| `LNK1104` o "acceso denegado" al enlazar | FL Studio tiene el plugin cargado. Ciérralo y recompila |
| FL Studio no muestra el plugin | Revisa la ruta en *Plugin search paths* y pulsa *Find installed plugins*; comprueba que es la versión de 64 bits |
| FL Studio muestra el plugin dos veces | Están añadidas a la vez las rutas de Release y de Debug |
| Tras cambiar `CMakeLists.txt` algo se comporta raro | Borra la carpeta `build` y vuelve a configurar |
| La configuración falla sin conexión | La primera vez necesita Internet para descargar JUCE y Catch2 |
| En Linux falta `X11/extensions/XInput2.h` u otra cabecera | Falta algún paquete de la lista de dependencias |
