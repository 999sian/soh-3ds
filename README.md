# Ship of Harkinian — Nintendo 3DS

An experimental port of [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright)
for the New Nintendo 3DS family, with a PICA200 renderer, 3DS controls and
touch-screen integration, audio, and on-device game-asset setup.

This is an active development source tree. Compatibility and performance vary;
it is not a finished release. The main hardware target is New Nintendo 3DS / New
Nintendo 2DS XL. CIA packaging requests the extended memory available on those
systems; the development `.3dsx` does not provide the same memory configuration.

## Game data

Supply your own supported Ocarina of Time ROM. ROMs and extracted game archives
are not included in this repository. The first-run setup looks for an uncompressed
`.z64`, `.n64`, or `.v64` file in `/3ds/soh/` on the SD card, validates it against
the packaged ROM manifest, and generates the game archive on the console.
Setup requires a build packaged with the support archive and Torch metadata.

**On-device extraction can take a long time.** Keep the console connected to
power and let setup finish without closing the app or removing the SD card.

To avoid extracting on the console, use desktop Ship of Harkinian (SoH) with
your supported ROM to generate the game archive, then copy `oot.o2r` (or
`oot-mq.o2r` for Master Quest) into `/3ds/soh/` on the SD card. Use a desktop SoH
version compatible with this port; setup validates the archive version before
using it. A valid archive lets setup skip game extraction.

**Archive format:** this port uses `.o2r`, not the legacy `.otr` format produced
by older SoH releases. Renaming an `.otr` file to `.o2r` will not convert it;
generate a compatible `.o2r` with SoH instead.

## Camera controls

The New 3DS C-stick controls the camera by default. On the first launch after
updating, missing C-stick bindings in older profiles are restored automatically;
existing bindings, sensitivity, and deadzone settings are preserved. No separate
settings file is required. Use **Controls → Enable C-stick camera** to turn
free-look on or off; a saved Off preference is preserved.

## Host build and tests

Requires CMake 3.20 or newer, a C++20 compiler, Python 3, and Bash. From the
repository root:

```sh
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

This builds portable logic and runs regression tests. It does not build the game
for a desktop platform or validate rendering on 3DS hardware.

## Building for 3DS

Install devkitPro/devkitARM, libctru, citro3d, and the required 3DS portlibs.
Set `DEVKITPRO` to your installation path. The dependency script builds additional
libraries, including SDL2, into that installation.

```sh
export DEVKITPRO=/opt/devkitpro
./scripts/build-3ds-deps.sh

cmake -S third_party/libultraship -B third_party/libultraship/build-3ds \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/3DS.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$DEVKITPRO/portlibs/3ds" \
  -DINCLUDE_MPQ_SUPPORT=OFF -DGFX_DEBUG_DISASSEMBLER=OFF \
  -DDISABLE_DLL_LOADER=ON
cmake --build third_party/libultraship/build-3ds -j2

cmake -S . -B build-3ds-mk \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/3DS.cmake" \
  -DCMAKE_BUILD_TYPE=Release
./scripts/build-3ds.sh
```

For subsequent builds, use `scripts/build-3ds.sh`: it rebuilds libultraship before
linking the game, preventing stale library changes from being omitted.
Set `SOH_BUILD_JOBS` to change its parallel build limit (default: 2).

`scripts/prepare-setup-romfs.py --help` describes the inputs required to stage
first-run setup data. Once `build-3ds-mk/setup-romfs/` contains the support archive,
ROM manifest, and Torch configuration, `scripts/make-cia.sh` packages the game ELF
using devkitPro's `makerom`. Build outputs remain local.

The default renderer uses the backend in `platform/3ds/` when present. Configure
with `-DSOH3DS_USE_MK_RENDERER=OFF` to select the alternative in `src/pica/`.
The renderers are not feature-equivalent. See
[renderer provenance](platform/3ds/PROVENANCE.md) for the existing licensing and
distribution constraints; this repository does not grant a new blanket license.

## Source layout

| Path | Purpose |
| --- | --- |
| `src/pica/` | Alternative PICA200 backend and combiner logic |
| `src/compat3ds/` | 3DS compatibility code |
| `src/setup/` | ROM validation and on-device archive setup |
| `platform/3ds/` | Hardware renderer, platform support, and CIA packaging |
| `third_party/` | Dependency source snapshots, including local port changes |
| `cmake/` | Cross-compilation configuration |
| `scripts/` | Build, packaging, and development utilities |
| `tests/` | Host regressions and hardware test programs |
| `patches/` | Port patches and technical notes |

The root `docs/` directory contains local development material and is excluded
from Git. Dependency copyright notices, licenses, and provenance remain with
their respective sources.
