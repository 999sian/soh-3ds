#!/usr/bin/env bash
# Canonical 3DS build: BOTH trees, in order.
#
# soh_3ds links the PREBUILT ${LUS_BUILD}/src/libultraship.a - editing
# third_party/libultraship and rebuilding only build-3ds-mk ships a stale
# library. That exact mistake shipped several builds whose libultraship-side
# fixes (exception containment, interpreter guards, queue locks) silently
# never entered the binary; it was caught only when a hardware debug session
# showed a catch handler that provably never fired and `strings` showed its
# log text absent. This script exists so "the build" always means both trees.
set -euo pipefail
cd "$(dirname "$0")/.."
export DEVKITPRO="${DEVKITPRO:-$HOME/dkp-root/opt/devkitpro}"
BUILD_JOBS="${SOH_BUILD_JOBS:-2}"

# XML assets emit commands at load time. Match OoT's F3DEX2 interpreter and
# replace the legacy GBI_UCODE:BOOL=OFF cache entry before compiling the library.
cmake -S third_party/libultraship -B third_party/libultraship/build-3ds \
    -DGBI_UCODE:STRING=F3DEX_GBI_2
cmake --build third_party/libultraship/build-3ds -j"$BUILD_JOBS"
cmake --build build-3ds-mk -j"$BUILD_JOBS" --target soh_3ds

# Static registration is the only reference to many cheats/enhancements.
# Ordinary archive linking silently drops them while leaving their UI visible.
python3 scripts/check-enhancement-link.py \
    --nm "$DEVKITPRO/devkitARM/bin/arm-none-eabi-nm" \
    build-3ds-mk/libsoh_enhancement.a build-3ds-mk/soh_3ds

# Presence sanity: strings that only exist in libultraship-side 3DS code.
if [ "$(strings build-3ds-mk/soh_3ds | grep -c 'resource: exception loading')" -eq 0 ]; then
    echo "build-3ds.sh: FATAL - libultraship-side code missing from binary (stale .a?)" >&2
    exit 1
fi
echo "build ok: $(md5sum build-3ds-mk/soh_3ds | cut -c1-8)"
