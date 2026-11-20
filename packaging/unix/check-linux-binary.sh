#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Comprueba que un plugin compilado para Linux carga en las distribuciones que
# se soportan (ADR 0022): Ubuntu 22.04, Debian 12, Fedora 36 o posteriores.
#
#   packaging/unix/check-linux-binary.sh ARCHIVO.so...
#
# Falla si algún archivo pide una glibc posterior a la 2.35 o una libstdc++
# posterior a la de GCC 12 (GLIBCXX_3.4.30, CXXABI_1.3.13), o si enlaza con
# una biblioteca que no está en la lista: las del sistema base y las dos que
# usa JUCE para el texto (FreeType y Fontconfig), que tiene cualquier
# escritorio.
set -euo pipefail

max_glibc=2.35
max_glibcxx=3.4.30
max_cxxabi=1.3.13
allowed="libc.so.6 libm.so.6 libgcc_s.so.1 libstdc++.so.6 ld-linux-x86-64.so.2 libfreetype.so.6 libfontconfig.so.1"

# true si la versión $1 es posterior a $2.
newer() { [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$1" ]; }

status=0
for file in "$@"; do
    versions=$(objdump -T "$file" | grep -o '\(GLIBC\|GLIBCXX\|CXXABI\)_[0-9.]*' | sort -u)
    for prefix in GLIBC GLIBCXX CXXABI; do
        case $prefix in
            GLIBC) max=$max_glibc ;;
            GLIBCXX) max=$max_glibcxx ;;
            CXXABI) max=$max_cxxabi ;;
        esac
        latest=$(echo "$versions" | sed -n "s/^${prefix}_//p" | sort -V | tail -1)
        echo "$file: ${prefix} ${latest:-ninguna} (máximo ${max})"
        if [ -n "$latest" ] && newer "$latest" "$max"; then
            echo "  ERROR: pide ${prefix}_${latest}:" >&2
            objdump -T "$file" | grep "(${prefix}_${latest})" | awk '{print "    " $NF}' >&2
            status=1
        fi
    done
    for lib in $(readelf -d "$file" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
        case " $allowed " in
            *" $lib "*) ;;
            *) echo "  ERROR: $file enlaza con $lib, que no está en la lista" >&2; status=1 ;;
        esac
    done
done
exit $status
