# libultraship 3DS patches

Applies to `kenix3/libultraship` @ `port-maintenance` (probed at `62e973a`).

```sh
cd libultraship && git apply ../patches/libultraship-3ds.patch
```

## Status

**All 140 applicable translation units compile, and `libultraship.a` (6.3 MB)
links into a working `.3dsx`.**

The 8 not built are correctly platform-gated by LUS's own CMake and are not
failures: `CoreAudioAudioPlayer` (macOS), `WasapiAudioPlayer` (Windows),
`MobileImpl` (iOS/Android), `Keystore`/`LibraryLoader`/`ScriptLoader`/
`scriptingbridge` (scripting + DLL loader off), plus `gfx_opengl`/`gfx_sdl2`
which these patches exclude for 3DS.

Error progression while getting there: **223 → 61 → 39 → 16 → 15 → 7 → 1 → 0.**

Reproduce:

```sh
export DEVKITPRO=$HOME/dkp-root/opt/devkitpro
./scripts/build-3ds-deps.sh                 # zlib, libzip, nlohmann, tinyxml2, spdlog, SDL2
cd libultraship
cmake -B build-3ds \
  -DCMAKE_TOOLCHAIN_FILE=../cmake/3DS.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=$DEVKITPRO/portlibs/3ds \
  -DINCLUDE_MPQ_SUPPORT=OFF -DGFX_DEBUG_DISASSEMBLER=OFF -DDISABLE_DLL_LOADER=ON
cmake --build build-3ds -j -- -k
```

## The root cause: `int32_t` is `long int` on devkitARM

This is the finding that matters, and it was not in `RESEARCH.md`.

```
devkitARM r68 : __INT32_TYPE__ = long int
x86-64 Linux  : __INT32_TYPE__ = int
```

Every platform SoH currently supports has `int32_t == int`, so code that mixes
the two spellings compiles by coincidence. On 3DS they are **distinct types**,
and every such conflation becomes a hard error. It accounted for **223 of the
first 223 errors** seen.

None of these are 3DS-specific defects. They are latent portability bugs that any
target with `int32_t != int` would expose.

## What each patch fixes

| File | Fix |
|---|---|
| `include/libultraship/libultra/types.h` | `s8..u64` were spelled as builtin types (`typedef signed int s32`). Now alias the `stdint.h` exact-width types. No-op where `int32_t == int`; on 3DS it makes `s32` and `int32_t` the same type again — and incidentally makes LUS's types match libctru's exactly. |
| `include/libultraship/libultra/abi.h` | Had its own `typedef unsigned int u32`, conflicting with `types.h`. |
| `include/libultraship/libultra/os.h` | `osEepromLongRead/Write` declared with a trailing `int` here and `int32_t` in `eeprom.h` — two incompatible declarations of the same C function. |
| `include/ship/audio/*.h`, `src/ship/audio/*.cpp` | `AudioPlayer::Buffered()` returns `int32_t`; all four overrides returned `int`. 158 errors on their own, because every TU including `AudioPlayer.h` re-reported them. |
| `src/fast/interpreter.cpp` | `#ifndef _WIN32 → #include <dlfcn.h>` assumes every non-Windows target has `dlopen`. Guarded for 3DS, and the `dladdr` call site returns `false` (nothing is dynamically loaded, so a low pointer is always a real segmented address). |
| `cmake/dependencies/common.cmake` | Don't build `imgui_impl_opengl3` / `imgui_impl_sdl2` on 3DS — the first needs `dlfcn.h`, the second needs `SDL.h` on the include path. The 3DS port supplies a citro3d ImGui backend instead. |
| `src/fast/CMakeLists.txt` | Exclude `gfx_opengl` / `gfx_sdl2` on 3DS. Note the existing exclusion regexes in this file reference the pre-refactor `graphic/Fast3D/backends/` paths and **match nothing** — a latent bug worth reporting upstream. |

## Remaining int32_t sites, now fixed

All were the same conflation, mostly at ImGui call boundaries (ImGui takes
`int*`; LUS passed `int32_t*` from CVar getters):

| File | Fix |
|---|---|
| `InputEditorWindow.cpp` | 7 locals declared `int32_t` then passed to `ImGui::SliderInt`/`Combo` — now `int` |
| `GuiTextureFactory.cpp` | stb_image writes through `int*`; take into `int` locals and copy into the `int32_t` metadata |
| `Fast3dWindow.{h,cpp}` | `KeyDown`/`KeyUp` were `bool(int32_t)`; the `GfxWindowBackend` vtable wants `bool(*)(int)` |
| `BinaryWriter.cpp` | `int strLen` made `Write()` ambiguous against the int32_t/int64_t overloads; now explicitly `int32_t` (the wire format is 32-bit) |
| `ResourceManager.cpp` | `std::max(1, (int32_t)…)` — both args must agree; now `std::max<int32_t>` |

`reinterpret_cast<int*>` would happen to work on ARM32 (`sizeof(long) == sizeof(int)`)
but is ill-formed, so none of these use it.

## Upstreamability

The `types.h`, `abi.h`, `os.h` and audio fixes are strict improvements that are
no-ops on existing platforms — worth offering upstream regardless of the 3DS
port. The CMake exclusions are 3DS-specific and belong behind `if(N3DS)`.
