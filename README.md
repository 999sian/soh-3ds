# Ship of Harkinian — Nintendo 3DS

An experimental port of [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright)
for the New Nintendo 3DS family, with a PICA200 renderer, 3DS controls and
touch-screen integration, audio, and on-device game-asset setup.

This is an active development source tree. Compatibility and performance vary;
it is not a finished release. The main hardware target is New Nintendo 3DS / New
Nintendo 2DS XL. CIA packaging requests the extended memory available on those
systems; the development `.3dsx` does not provide the same memory configuration.

## Download

[**v0.1.0-alpha.3 prerelease**](https://github.com/999sian/soh-3ds/releases/tag/v0.1.0-alpha.3)
includes the native randomizer settings menu, Reset to defaults, the starting-heart
fix and faster seed generation. Install the CIA with FBI on New 3DS / New 2DS XL;
the release page includes a QR code and checksums. See [randomizer controls and
benchmark details](RANDOMIZER-3DS.md).

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

## Randomizer on 3DS

Choose **Randomizer → All settings** to edit seed settings, starting inventory,
logic tricks, excluded locations and randomizer enhancements on the console.
Press **A** on a setting for its full description. See [the randomizer guide](RANDOMIZER-3DS.md)
for touch controls and presets. **Reset to defaults** restores the randomizer
settings and enhancements, including three starting hearts.

**Starting Hearts** is under **Starting Inventory / Other** and defaults to **3**.
This update fixes missing setting definitions on 3DS, which previously generated
seeds with zero-valued settings and one starting heart. Generate a new seed and
start a new save after updating; existing seeds and saves keep their old settings.

## Camera controls

The New 3DS C-stick controls the camera by default. On the first launch after
updating, missing C-stick bindings in older profiles are restored automatically;
existing bindings, sensitivity, and deadzone settings are preserved. No separate
settings file is required. Use **Controls → Enable C-stick camera** to turn
free-look on or off; a saved Off preference is preserved.

## Debug logging

Logging is off by default. Open the bottom-screen **Settings** tab, select
the **Debug** page, and toggle **Logging** when you need diagnostics. Changes
apply in-game and are saved. **Frame Timing**, **Detailed Profiling**, and
**Texture Tracing** are separate options, also off by default, and only run
while Logging is on. Old logging flag files no longer enable these recordings.

Logging can cause stutter. Turn it off after testing. Normal on-screen setup
progress and error messages remain visible with logging off.

## Cheats and Mirror Mode

Open the bottom-screen **Settings** tab and select **Cheats**. **Moon Jump**
uses the button assigned to **N64 L** in Controls: **ZL** in the default and
OoT3D layouts. Hold it to rise. **No Clip** lets Link walk through walls;
floors and gravity still apply. **Infinite Ammo** refills ammunition to your
current equipment capacity; it does not grant missing weapons or upgrades.

**Mirrored World** is on the **Extra Modes** page. Choose **Always** to mirror
the current world, or another mode to select which scenes are mirrored.

## Host build and tests

Requires CMake 3.20 or newer, a C++20 compiler, Python 3, Bash, and the libzip
development package (for the archive regression test). From the
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
  -DGBI_UCODE:STRING=F3DEX_GBI_2 \
  -DINCLUDE_MPQ_SUPPORT=OFF -DGFX_DEBUG_DISASSEMBLER=OFF \
  -DDISABLE_DLL_LOADER=ON
cmake --build third_party/libultraship/build-3ds -j2

cmake -S . -B build-3ds-mk \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/3DS.cmake" \
  -DCMAKE_BUILD_TYPE=Release
./scripts/build-3ds.sh
```

For subsequent builds, use `scripts/build-3ds.sh`: it configures libultraship for
OoT's F3DEX2 graphics commands and rebuilds it before linking the game, preventing
stale library changes from being omitted.
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
