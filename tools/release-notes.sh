#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Imprime la sección de CHANGELOG.md de una versión: es el texto de la
# release de GitHub. Falla si la versión no tiene sección.
#
# Uso: tools/release-notes.sh 0.1.0
set -euo pipefail

version="${1:?uso: release-notes.sh VERSION}"
changelog="$(dirname "$0")/../CHANGELOG.md"

notes="$(awk -v version="$version" '
    /^## / {
        if (printing) exit
        # Cabeceras admitidas: "## 0.1.0", "## [0.1.0]", con o sin fecha detrás.
        header = $2
        gsub(/[\[\]]/, "", header)
        if (header == version) { printing = 1; next }
    }
    printing { print }
' "$changelog")"

# Sin líneas en blanco al principio ni al final.
notes="$(printf '%s\n' "$notes" | sed -e '/./,$!d' | sed -e ':a' -e '/^\n*$/{$d;N;ba' -e '}')"

if [ -z "$notes" ]; then
    echo "CHANGELOG.md no tiene una seccion para la version $version" >&2
    exit 1
fi
printf '%s\n' "$notes"
