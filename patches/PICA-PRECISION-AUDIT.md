# PICA combiner precision audit — 2026-09-08

## Scope and evidence

This audit tests the active `platform/3ds/source/gfx_citro3d.cpp` channel
planner. It compiles the real planning and constant-packing functions into a
host harness, following the existing `combiner_plan_3ds_test.py` approach.
No renderer behaviour is changed.

The provisional numerical model rounds and saturates every stage output to a
byte. It uses exact integer arithmetic for byte-valued operands, with nearest
rounding after multiplication or multiply-add. Constants use the renderer's
real `ToByte` conversion. This is motivated by TriAevum's
[shader generator](https://github.com/coccofresco/TriAevum/blob/main/tools/oot3d/native_pica_frontend/oot3d_native_pica_fragment_shader_gen.cpp)
(`ColorOperation`, `AlphaOperation`, and the `byteround` stage outputs), inspected
on 2026-09-08. No donor code is copied.

This is **not a hardware-validated PICA emulator**. It excludes texture
filtering, raster interpolation, stage scaling, buffer timing, GPU source
binding and framebuffer conversion. The existing floating-point evaluator in
`src/pica/pica_texenv.hpp` remains useful for algebraic lowering tests; its
results alone do not establish byte-exact rendering.

## Host results

Run `python3 tests/combiner_precision_3ds_test.py`, or the CTest test
`combiner_precision_3ds`.

| Check | Cases | Observed result under this model |
| --- | ---: | --- |
| Texture multiplication | 65,536 | Matches single rounded product |
| HUD direct interpolation | 16,777,216 | Matches single rounded expression |
| HUD two-stage interpolation | 16,777,216 | 4,121,020 differ, maximum 1/255 |
| Safe subtract folds, RGB and alpha | 82,432 | Maximum difference 1/255 |

The HUD reference is `(A-B)*C+B`, rounded once. Its two-stage implementation
rounds `A*C` before adding `B*(1-C)`. The extra rounding explains the measured
difference. The tests allow a one-byte error rather than incorrectly demanding
exact equivalence. Folded cases cover six boundary/interior values for each
constant and all 256 texture values, restricted to `D-B*C >= 0`.

These counts describe synthetic inputs, not how frequently errors occur in
gameplay. They do not prove a visible defect or justify changing the renderer.

## Azahar RGBA8 characterization

The standalone `tev_precision_3ds` target uses real Citro3D TEV stages and an
RGBA8 offscreen target. It performs a synchronized display transfer and cache
invalidation before reading the center pixel. Blending is disabled. It tests
GPU arithmetic directly, not the active backend's complete binding/draw path.

Azahar 2126.0 completed 585 unique samples: 189 each for multiplication,
direct interpolation and split interpolation, and nine each for alpha >= 8
and alpha > 48. All four channels matched the provisional model in every
sample. Rejected alpha samples retained the contrasting clear value 61.

Binary SHA-256:
`3aea251ad57b92599b41edca78d17a969cf87662f44fd7abced1dedb0bdf4457`.

Local package: `build-3ds-pica/tev-precision-v1.zip`. It includes the identical
`.3dsx`, checksum, instructions, Azahar log and `azahar-precision-v1.csv`.
On console, launch the `.3dsx` with Homebrew Launcher; it needs no ROM and
writes `/3ds/soh-tev/precision-v1.csv`. Wait for `TEV END samples=585` before
returning the CSV with the console model. Subsequent runs replace this CSV.

Build with the 3DS toolchain configured, then:
`DEVKITPRO=$HOME/dkp-root/opt/devkitpro cmake --build build-3ds-pica --target tev_precision_3ds -j2`.

## Remaining hardware and visual checks

The additional `renderer_precision_3ds` target links the active
`mk_gfx_citro3d` library and exercises real shader-ID decoding, texture upload,
source binding, packed vertices, draw submission, offscreen storage and RGBA8
readback. Azahar completed all 42 cases: texture passthrough, modulation, HUD
interpolation, translucent fog, alpha threshold, opaque cutout and fog with
alpha threshold. Maximum channel error against the SoH expression was 1/255
(the test permits 2/255 for combined quantization and blending).

Reference behaviour comes from libultraship's OpenGL `default.shader.glsl`:
fog mixes RGB while preserving alpha, cutouts pass above 0.19, and the low
alpha threshold is 8/256. The test includes 7/8 and 48/49 boundary values and
checks discarded samples are black and accepted samples have visible coverage.
These are synthetic constant-colour materials, not game-scene captures.

An initial harness run incorrectly expected stored alpha to equal source
alpha. The backend uses `As + Ad*(1-As)` with an opaque clear (`Ad=1`), which
correctly leaves stored alpha at 255. Correcting the harness expectation and
adding translucent RGB blending yielded 42/42 passes; no renderer fix was
needed. The initial output remains local at
`/tmp/soh-renderer-precision-initial.csv`.

The combined local package is `build-3ds-pica/precision-validation-v2.zip`.
It contains both probes, both Azahar CSVs, instructions and a manifest with
source/binary hashes. The active-renderer binary SHA-256 is
`8ee277fa7178970abf4228d0a2532b99c78df417cc9c257916ee302167331dfc`.
It writes `/3ds/soh-tev/renderer-v1.csv`. Both CSVs and the console model are
needed for physical validation. FTP connection attempts to the three addresses
recorded in earlier project hardware notes timed out on this audit run.

The latest package is **`build-3ds-pica/precision-validation-v3.zip`**. The
renderer probe now has 60 cases, adding two-cycle multiplication, grayscale,
and grayscale after fog. All 60 passed in Azahar, with maximum channel error
1/255. It writes `renderer-v2.csv`, and its binary SHA-256 is
`9d0f6d977c0c34767106984ca5ddd0c135235f1556e678b7db7074d3ce451ad6`.
The raw TEV probe and `precision-v1.csv` are unchanged. Earlier v1/v2 packages
are historical evidence; use v3 for new hardware measurements.

### Goal completion audit

| Requirement | Authoritative evidence | Status |
| --- | --- | --- |
| RGBA8 GPU characterization | Raw probe source, built binary and 585-row Azahar CSV | Implemented; emulator-qualified |
| Active renderer validation | Active library linked by renderer probe; 60-row Azahar CSV | Implemented; emulator-qualified for these synthetic materials |
| Intended SoH comparison | Reference shader equations, coverage boundaries and per-channel CSV expectations | Numerical comparison done; game-scene comparison missing |
| Matching physical readback | No device CSV received; recorded FTP addresses time out | Missing |
| Matching visual evidence | No paired physical/desktop/emulator game-scene captures | Missing |
| Fix demonstrated renderer defects | No renderer defect established by these samples | No fix justified |
| Preserve workspace changes | Probe targets/tests and audit added; production renderer unchanged | Preserved |

Hardware access or returned CSVs plus matching game-scene captures are required
to finish. The same dependency has remained unresolved through the initial
goal turn and two continuations. A fresh FTP check of the three recorded
addresses again timed out. Emulator evidence must not be promoted to a
physical-hardware pass. The user has been asked for the live FTP address and
port; further device work depends on that external state changing.

No physical-console measurements or matching game screenshots have been
collected. The existing phase-1 test reads RGB5A1 and
targets the alternative renderer, so it cannot establish one-byte precision
for the active backend. Use an RGBA8 offscreen readback for characterization;
five-bit conversion can hide the differences under investigation.

1. Run the packaged probe on physical 3DS and compare all rows with the Azahar
   reference. Preserve the binary hash and console model with the measurements.
2. Run the packaged active-renderer probe on hardware. Extend the synthetic
   draw coverage to varying vertex values, two-cycle materials and grayscale
   stage pressure as needed for the game-scene comparisons.
3. Compare the same save, camera, frame, settings and build on each target:

   | Surface | What to inspect |
   | --- | --- |
   | HUD text and fades | Blend colour and smooth fading |
   | Deku Tree webs | Texture alpha and colour saturation |
   | Foliage/cutout edges | Alpha-test coverage |
   | Fog with translucent surfaces | RGB fog and preserved alpha |

4. Log stage demand for materials using fog plus grayscale. Current code
   conditionally omits grayscale if the six-stage budget is exceeded; whether
   affected materials occur in these scenes has not been measured.

Compare native-resolution crops without video compression or rescaling. Treat
Azahar results as emulator evidence and physical measurements as hardware
evidence; neither substitutes for an intended-output reference from desktop
SoH. Keep performance measurements separate from diagnostic capture runs.
