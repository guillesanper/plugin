#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Compila pluginval 1.0.4 con un arreglo de JUCE, para validar el VST3 en Linux.
#
#   tools/pluginval/build.sh DIRECTORIO
#
# Deja el ejecutable en DIRECTORIO/pluginval, y nada más (compila en
# DIRECTORIO/.work, que borra al terminar). pluginval 1.0.4 se compila con
# JUCE 8.0.3, cuyo host VST3 de Linux (RunLoop) usa memoria liberada si una
# instancia se destruye mientras el plugin tiene mensajes pendientes, y a
# veces se cae al terminar una prueba. JUCE lo arregló en la 9.0.2; el parche
# lleva ese arreglo, y nada más, a la copia de JUCE de pluginval. Los tests de
# pluginval son los de la 1.0.4. Ver docs/decisiones/0021-aviso-de-latencia-sin-temporizador.md.
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "uso: $0 DIRECTORIO" >&2
    exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
out=$(mkdir -p "$1" && cd "$1" && pwd)
work="$out/.work"

# Fijados por commit: una etiqueta se puede mover.
pluginval_commit=ed19c2c16b57a6d94db391bea3ef4a80b769d5bf # v1.0.4
juce_commit=5179f4e720d8406ebd1b5401c86aea8da6cc83c9      # JUCE 8.0.3

rm -rf "$work/src" "$work/build"
git clone --quiet --depth 1 --branch v1.0.4 https://github.com/Tracktion/pluginval "$work/src"
test "$(git -C "$work/src" rev-parse HEAD)" = "$pluginval_commit"
git -C "$work/src" submodule update --quiet --init --depth 1 modules/juce
test "$(git -C "$work/src/modules/juce" rev-parse HEAD)" = "$juce_commit"
git -C "$work/src/modules/juce" apply "$here/juce-8.0.3-vst3-runloop.patch"

cmake -S "$work/src" -B "$work/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/build" --config Release --parallel 4
cp "$work/build/pluginval_artefacts/Release/pluginval" "$out/pluginval"
rm -rf "$work"
"$out/pluginval" --version
