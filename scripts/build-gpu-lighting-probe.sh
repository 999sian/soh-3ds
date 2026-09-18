#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
out=${1:-builds/old3ds-cpu-batch-20260908T201258Z/gpu-lighting}
mkdir -p "$out"
dkp=${DEVKITPRO:-/home/sian/dkp-root/opt/devkitpro}
"$dkp/tools/bin/picasso" -o "$out/candidate.shbin" tests/gpu_lighting_precision.v.pica
python3 scripts/bin2c.py "$out/candidate.shbin" "$out/candidate.c" gpu_lighting_shbin
flags=(-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -mword-relocations -fno-fast-math -ffp-contract=off -ffunction-sections -fdata-sections -D_GNU_SOURCE -D__3DS__ -D_3DS -DARM11 -O2 -I"$dkp/libctru/include" -Itests)
"$dkp/devkitARM/bin/arm-none-eabi-gcc" "${flags[@]}" -c "$out/candidate.c" -o "$out/candidate.o"
"$dkp/devkitARM/bin/arm-none-eabi-g++" "${flags[@]}" -std=c++20 tests/gpu_lighting_precision_3ds.cpp "$out/candidate.o" -specs="$dkp/devkitARM/arm-none-eabi/lib/3dsx.specs" -Wl,--gc-sections -L"$dkp/libctru/lib" -lcitro3d -lctru -lm -o "$out/gpu-lighting.elf"
"$dkp/tools/bin/3dsxtool" "$out/gpu-lighting.elf" "$out/gpu-lighting.3dsx"
