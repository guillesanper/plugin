# 0010. Distribución: CLAP con clap-juce-extensions, instalador de Windows y releases desde el CI

**Estado:** Aceptada (noviembre de 2026)

## Contexto

La primera versión pública necesita llegar a los usuarios: formatos que carguen en sus hosts, una forma sencilla de instalar en Windows (la plataforma principal) y un proceso de publicación que no dependa de la máquina de nadie. Había que decidir:

1. Cómo se obtiene el formato CLAP, que JUCE 9 no genera.
2. Qué se entrega en cada plataforma.
3. Quién construye lo que se publica.
4. Qué pasa con la firma de código, que cuesta dinero.
5. Qué queda congelado al publicar.

## Decisión

1. **CLAP con clap-juce-extensions** (licencia MIT), que envuelve el mismo `AudioProcessor`: no hay código de plugin duplicado. El proyecto no publica etiquetas recientes, así que se fija **por commit** en `CMakeLists.txt`. Se valida con **clap-validator 0.3.2** (fijado por versión) en el CI de las tres plataformas, igual que el VST3 con pluginval. Se puede desactivar con `-DESCEQ_BUILD_CLAP=OFF`.
2. **Paquetes:**

   | Plataforma | Paquete | Contenido |
   |---|---|---|
   | Windows x64 | `esceQ-X.Y.Z-windows-x64-setup.exe` (Inno Setup) | VST3 en `Common Files\VST3`, CLAP en `Common Files\CLAP`, a elegir |
   | macOS universal | `esceQ-X.Y.Z-macos-universal.zip` | VST3, AU y CLAP (arm64 y x86_64), con instrucciones |
   | Linux x64 | `esceQ-X.Y.Z-linux-x64.tar.gz` | VST3 y CLAP, con instrucciones |

   El instalador de Windows no crea carpeta de aplicación: deja los plugins donde los hosts los buscan y un desinstalador. No toca los presets del usuario.
3. **Todo lo construye el CI.** El workflow `CI` compila, pasa los tests, valida cada formato y genera los paquetes en cada push; en Windows además **instala en silencio el instalador recién generado, comprueba que deja cada formato en su carpeta, valida el VST3 instalado y lo desinstala**. El workflow `Release` se dispara al subir una etiqueta `vX.Y.Z`, reutiliza el `CI` entero (`workflow_call`) y publica en GitHub exactamente los paquetes que ese CI ha generado, con sus sumas SHA-256. Antes de compilar comprueba que la etiqueta coincide con la versión de `CMakeLists.txt` y que `CHANGELOG.md` tiene una sección para esa versión, que es el texto de la release. Se puede ensayar a mano (`workflow_dispatch`): hace todo menos publicar.
4. **Sin firma de código por ahora.** En Windows el instalador mostrará el aviso de SmartScreen; en macOS hay que quitar la cuarentena a mano (el paquete lo explica). Firmar exige un certificado de Windows y una cuenta de desarrollador de Apple (99 USD al año): se decidirá cuando haya usuarios.
5. **Al publicar la 0.1.0 quedan congelados:**
   - la tabla de parámetros: `tests/fixtures/parameters/release_0.1.txt`;
   - el formato de estado: `tests/fixtures/state/release_0.1_full.txt`, un proyecto que usa todo lo que tiene la versión;
   - los presets de fábrica: `tests/fixtures/presets/release_0.1/`;
   - el sonido de ese proyecto: `tests/fixtures/dsp/release_0.1_audio.txt`, la salida del ecualizador con dinámica, sidechain y bandas en Mid, Side e izquierda;
   - las identidades: códigos del VST3 y del AU, `CLAP_ID` (`org.esceq.esceq`) y el `AppId` del instalador.

   Los comprueba `tests/core/release_contract_test.cpp`. Cada versión publicada añadirá sus propios archivos; los anteriores no se tocan.

## Consecuencias

- El CI tarda más: cada plataforma compila un formato más, y macOS compila dos arquitecturas. Con GCC, el enlazado con LTO se reparte entre los núcleos (`-flto=auto`); sin eso el job de Linux no terminaba a tiempo.
- Publicar una versión es: subir la versión en `CMakeLists.txt`, escribir su sección en `CHANGELOG.md`, y subir la etiqueta. Los pasos están en [CONTRIBUTING.md](../../CONTRIBUTING.md).
- Si clap-juce-extensions deja de mantenerse o no sigue a una versión nueva de JUCE, el CLAP se puede desactivar sin afectar a los demás formatos. La alternativa natural sería clap-wrapper, ya prevista en el [PLAN](../../PLAN.md).
- El golden de audio se compara con una tolerancia de 10⁻⁴ (−80 dB): por debajo de lo audible, y con margen para las diferencias de redondeo entre compiladores. Un cambio de algoritmo que altere el sonido lo hace fallar: tendría que ir detrás de una versión de algoritmo guardada en el estado.
- Comprobación del nombre (noviembre de 2026): no se ha encontrado ningún plugin, producto ni marca registrada llamado "esceQ" (búsquedas en KVR, en la web y en agregadores de registros de marca; en GitHub solo aparecen dos repositorios sin relación con el audio). Los nombres más parecidos son "EaseQ" (Air-G Audio) y "EssEQ" (Voxengo), que se escriben distinto. No es una búsqueda oficial en los registros de marcas (EUIPO, USPTO): conviene hacerla antes de invertir en la marca.
- Noviembre de 2026: la 0.1.0 no llegó a publicarse (sus paquetes de macOS y Linux tenían los fallos que corrige la [ADR 0022](0022-compatibilidad-en-macos-y-linux.md)); la primera versión pública es la 0.5.0, decidido con el usuario. Los contratos de la 0.1.0 se mantienen, y los de la 0.5.0 se añaden con sus archivos `release_0.5*`: la tabla de 391 parámetros, un proyecto con el estado en la versión 3 que usa lo nuevo (Matched, Linear Low, tiempos automáticos, dinámica espectral, saturación y las preferencias de la interfaz), los presets de fábrica y el sonido de `dsp::PhaseEqualizer` con ese proyecto en Linear Low y en IIR.
