#!/usr/bin/env bash
# Build an installable CIA from the soh_3ds ELF (not the .3dsx).
#
#   ./scripts/make-cia.sh [elf] [out.cia] [setup-romfs-dir]
#
# Why a CIA: a .3dsx has no exheader, so it cannot request the 124 MB N3DS
# memory mode, the 804 MHz clock + L2 cache, or core 2 - platform/3ds/cia/app.rsf
# (adapted from devilutionX's Packaging/ctr/template.rsf) grants all of them.
# A CIA process has no cwd; tests/soh_3ds_main.cpp chdir()s to sdmc:/3ds/soh.
set -euo pipefail
cd "$(dirname "$0")/.."
export DEVKITPRO="${DEVKITPRO:-$HOME/dkp-root/opt/devkitpro}"

ELF="${1:-build-3ds-mk/soh_3ds}"
HASH="$(md5sum "$ELF" | cut -c1-8)"
OUT="${2:-builds/soh_3ds-$HASH.cia}"
ROMFS_DIR="${3:-build-3ds-mk/setup-romfs}"

for required in roms.tsv soh.o2r.gz; do
    if [ ! -f "$ROMFS_DIR/$required" ]; then
        echo "make-cia.sh: missing setup RomFS file: $ROMFS_DIR/$required" >&2
        exit 1
    fi
done

while IFS=$'\t' read -r sha1 archive display variant; do
    if [[ ! "$variant" =~ ^[a-z0-9_-]+$ ]] || [ ! -f "$ROMFS_DIR/torch/$variant.tar.gz" ]; then
        echo "make-cia.sh: missing or invalid setup metadata bundle: $variant" >&2
        exit 1
    fi
done < "$ROMFS_DIR/roms.tsv"

mkdir -p "$(dirname "$OUT")"
"$DEVKITPRO/tools/bin/makerom" -f cia -o "$OUT" -elf "$ELF" \
    -rsf platform/3ds/cia/app.rsf -icon platform/3ds/cia/icon.icn -banner platform/3ds/cia/banner.bnr \
    "-DROMFS_ROOT=$ROMFS_DIR" -target t -exefslogo
echo "cia ok: $OUT ($(stat -c %s "$OUT") bytes, elf $HASH)"
