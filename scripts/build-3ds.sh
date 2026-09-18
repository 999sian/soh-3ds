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
# Both trees contain shared archives and package outputs. Concurrent invocations
# can remove an archive while another ar process is replacing it.
mkdir -p build-3ds-mk
exec {SOH_BUILD_LOCK_FD}>build-3ds-mk/.soh-build.lock
flock "$SOH_BUILD_LOCK_FD"
export DEVKITPRO="${DEVKITPRO:-$HOME/dkp-root/opt/devkitpro}"
BUILD_JOBS="${SOH_BUILD_JOBS:-2}"
GAME_TARGET="${SOH_BUILD_TARGET:-soh_3ds}"
case "$GAME_TARGET" in
    soh_3ds|soh_3ds_old) ;;
    *) echo "Unsupported SOH_BUILD_TARGET: $GAME_TARGET" >&2; exit 1 ;;
esac

# XML assets emit commands at load time. Match OoT's F3DEX2 interpreter and
# replace the legacy GBI_UCODE:BOOL=OFF cache entry before compiling the library.
cmake -S third_party/libultraship -B third_party/libultraship/build-3ds \
    -DGBI_UCODE:STRING=F3DEX_GBI_2 \
    -DSOH3DS_ARM11_VERTEX_ASM:BOOL="${SOH3DS_ARM11_VERTEX_ASM:-OFF}" \
    -DSOH3DS_ARM11_TRIANGLE_EMIT:BOOL="${SOH3DS_ARM11_TRIANGLE_EMIT:-OFF}" \
    -DSOH3DS_COMPACT_VERTEX_STREAM:BOOL="${SOH3DS_COMPACT_VERTEX_STREAM:-OFF}" \
    -DSOH3DS_TRIANGLE_RUN_REUSE:BOOL="${SOH3DS_TRIANGLE_RUN_REUSE:-OFF}"
cmake --build third_party/libultraship/build-3ds -j"$BUILD_JOBS"
cmake -S . -B build-3ds-mk \
    -DSOH3DS_COMPACT_VERTEX_STREAM:BOOL="${SOH3DS_COMPACT_VERTEX_STREAM:-OFF}" \
    -DSOH3DS_DSP_CAPTURE:BOOL="${SOH3DS_DSP_CAPTURE:-OFF}" \
    -DSOH3DS_AUDIO_PROFILE:BOOL="${SOH3DS_AUDIO_PROFILE:-OFF}" \
    -DSOH3DS_OLD_AUDIO_CORE0:BOOL="${SOH3DS_OLD_AUDIO_CORE0:-OFF}" \
    -DSOH3DS_OLD_CPU80:BOOL="${SOH3DS_OLD_CPU80:-OFF}" \
    -DSOH3DS_ARM11_FILTER_ASM:BOOL="${SOH3DS_ARM11_FILTER_ASM:-OFF}" \
    -DSOH3DS_ARM11_MATRIX_MULT:BOOL="${SOH3DS_ARM11_MATRIX_MULT:-OFF}" \
    -DSOH3DS_FAST_GPUCMD:BOOL="${SOH3DS_FAST_GPUCMD:-OFF}"
cmake --build build-3ds-mk -j"$BUILD_JOBS" --target "$GAME_TARGET"

# Static registration is the only reference to many cheats/enhancements.
# Ordinary archive linking silently drops them while leaving their UI visible.
python3 scripts/check-enhancement-link.py \
    --nm "$DEVKITPRO/devkitARM/bin/arm-none-eabi-nm" \
    build-3ds-mk/libsoh_enhancement.a "build-3ds-mk/$GAME_TARGET"

# Presence sanity: strings that only exist in libultraship-side 3DS code.
if [ "$(strings "build-3ds-mk/$GAME_TARGET" | grep -c 'resource: exception loading')" -eq 0 ]; then
    echo "build-3ds.sh: FATAL - libultraship-side code missing from binary (stale .a?)" >&2
    exit 1
fi
echo "build ok: $(md5sum "build-3ds-mk/$GAME_TARGET" | cut -c1-8)"
