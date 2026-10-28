#!/bin/sh
# Stella PS5: one libretro core's objects -> one object whose only global names are its libretro entry points (cores.mk).
#   NM=... OBJCOPY=... LD=... localize_core.sh <out.o> <prefix> <objects...>
# The objects are linked into one (ld -r); then every strong global symbol it defines (functions, variables) but the
# <prefix>retro_* entry points is made local. Weak symbols (C++ inline functions, templates) stay as they are: two
# copies of a template are merged by the final link, as usual (tools/check_core_symbols.py checks that no other name
# is shared).
# SPDX-License-Identifier: MIT
set -e
out=$1
prefix=$2
shift 2
"$LD" -r -o "$out.all.o" "$@"
"$NM" -g --defined-only "$out.all.o" | awk -v p="${prefix}retro_" '$2 ~ /^[TDBRC]$/ && index($3, p) != 1 { print $3 }' >"$out.local"
"$OBJCOPY" --localize-symbols="$out.local" "$out.all.o" "$out"
rm -f "$out.all.o"
echo "$out: $(wc -l <"$out.local") names made local, $("$NM" -g --defined-only "$out" | grep -c " ${prefix}retro_") entry points"
