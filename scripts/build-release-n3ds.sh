#!/usr/bin/env bash
# Build fully optimized New 3DS release binary & CIA
set -euo pipefail
cd "$(dirname "$0")/.."

export SOH3DS_ARM11_VERTEX_ASM=ON
export SOH3DS_ARM11_TRIANGLE_EMIT=ON
export SOH3DS_COMPACT_VERTEX_STREAM=ON
export SOH3DS_TRIANGLE_RUN_REUSE=ON
export SOH3DS_ARM11_FILTER_ASM=ON
export SOH3DS_ARM11_MATRIX_MULT=ON
export SOH3DS_FAST_GPUCMD=ON
export SOH_BUILD_TARGET=soh_3ds

echo "=== Building optimized New 3DS binaries ==="
./scripts/build-3ds.sh

echo "=== Packaging New 3DS CIA ==="
./scripts/make-cia.sh build-3ds-mk/soh_3ds builds/soh_3ds-n3ds.cia build-3ds-mk/setup-romfs
