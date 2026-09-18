# Batched renderer vertex transforms

This experimental third pass adds `Soh3dsTransformVerticesArm11` in
`third_party/libultraship/src/fast/vertex_transform_arm11.S`. It transforms
signed XYZ vertex positions into clip-space XYZW using ARMv6K and VFPv2.
The sixteen matrix coefficients stay in registers for the whole batch, and
independent output components are interleaved to reduce dependency stalls.

The block-store kernel is 144 bytes; its scalar-store variant is 156 bytes.
Both the ordinary and disjoint-buffer `-O3` C benchmark references are 220 bytes
with devkitARM GCC 16.1.0. These are symbol sizes, not elapsed-time or
game-performance measurements. Version 2 completed physical testing but was
rejected on performance. Version 3 improves isolated transform timing on large
batches. Integrated loader trials and an inconclusive gameplay pilot are
recorded below; a gameplay speed benefit remains unproven.

## Integration

The `SOH3DS_ARM11_VERTEX_ASM` option defaults to **OFF**. A candidate build is:

```sh
SOH3DS_ARM11_VERTEX_ASM=ON SOH_BUILD_TARGET=soh_3ds_old scripts/build-3ds.sh
```

The normal build script defaults the option back to OFF when the environment
variable is omitted. For direct libultraship CMake builds, set
`-DSOH3DS_ARM11_VERTEX_ASM=ON` or `OFF` explicitly; CMake caches the setting.

The experimental vertex loader validates the batch bounds and requires at
least 32 vertices before calling the kernel. Smaller batches retain scalar
transforms; the production option remains OFF.
Input that overlaps RSP state uses the original scalar loop, preserving
ordered reads when vertex output or lighting-coefficient updates overwrite
later input data. `GBI_FLOATS` and other platforms also use the scalar path.
Compile-time layout checks protect the 16-byte input and 32-byte output strides.

Only XYZW is written by assembly. Lighting, positional lights, fog, aspect
correction, texture coordinates and clipping retain their existing calculations.
There are no new heap allocations. Version 3 keeps the matrix in s6–s21 and
saves/restores d8–d10 in a 24-byte stack frame, retaining eight-byte alignment.
It interleaves integer transfers/conversions with floating-point arithmetic
using the faster disjoint C schedule as a guide. Zero-count calls access no
memory. Evaluation uses
non-fused VFP operations and matches the production compiler's operand order.

## Correctness checks

```sh
python3 tests/arm11_vertex_test.py
python3 tests/arm11_vertex_integration_test.py
python3 tests/arm11_vertex_hardware_mode_test.py
```

These execute on `qemu-arm -cpu arm11mpcore` using devkitARM. Set `DEVKITPRO`
for a custom SDK location. CTest registers these tests when that SDK and QEMU
are available; ordinary host builds do not require the ARM toolchain.

The kernel test covers 8,192 batches, every size from 0–68, signed coordinate
extremes, halfword-only input alignment, word-only output alignment, arbitrary
float bits, all four rounding modes, flush-to-zero and default-NaN modes. It
compares exact output bits, untouched fields and guard records, source/matrix
preservation, FPSCR arithmetic flags and AAPCS callee-saved registers. A literal
hand-calculated transform also checks the matrix order independently.

The integration test executes the production loader with assembly enabled and
disabled, checking rendering modes, input/output aliasing, input backed by
mutable lighting state, null input and invalid destination/count combinations.
It uses isolated renderer state and devkitARM's math library; platform services
and full game execution are outside that test.

## Physical Old/New 3DS benchmark

```sh
python3 scripts/build-arm11-vertex-benchmark.py
```

Copy `builds/arm11-vertex-benchmark/soh-arm11-vertex-benchmark.3dsx` to the SD
card's `/3ds/` directory and run it through Homebrew Launcher. Wait for **Done**,
then press START. Results are saved to `/3ds/soh/arm11-vertex-benchmark.csv`.
The benchmark creates that directory if needed and reports an SD write failure.
It detects Old/New hardware and selects the model's normal application speed.

Correctness runs before timing. Native checks preserve the console's RunFast
controls (flush-to-zero, default NaN and round-to-nearest, with traps disabled).
The broader mode sweep runs only in QEMU, which emulates floating-point cases
that require a software exception handler on real VFP11. The native-mode
regression audits every attempted FPSCR write and checks early rejection of
incompatible inherited controls. The benchmark records its initial FPSCR and
writes a startup checkpoint before correctness begins.

Any correctness failure prevents timing. The
CSV has 80 timing rows: eight batch sizes, two alignment cases and five rounds.
Alignment case 0 uses normally aligned buffers; case 1 offsets input by two
bytes and output by four bytes. Each row contains:

- `c_ticks`: the renderer's transform expressions compiled at production `-O3`.
- `c_disjoint_ticks`: the same C loop with `restrict` pointers, allowing the
  compiler to keep matrix coefficients in registers too.
- `asm_v2_ticks`: the original assembly kernel retained as a timing control.
- `asm_block_ticks`: the revised schedule and 24-byte frame, with one VSTMIA.
- `asm_scalar_ticks`: the same revised kernel with four scalar VSTR stores.

References are separate translation units using production aliasing defaults.
Timed data contains finite values; correctness also covers exceptional floats.
All five paths are warmed up, forward/reverse execution order alternates, and each timed interval
has two system-tick reads. Printing, SD writes and display updates occur outside
those intervals. Generated source, disassembly and SHA-256 hashes accompany
the `.3dsx` for reproducibility.

Use the median paired ASM/C ratio for each batch size/alignment; also compare
against the disjoint C control. A result below 1 means fewer ticks. Small
batches can lose to register-save overhead. These measurements isolate the
transform kernel on core 0: the extra pass, dispatch checks, cache effects and
rest of the renderer require separate game measurements before default enablement.
QEMU and emulator timings must not substitute for physical hardware results.

## Initial hardware crash and v2 correction

The first uploaded benchmark crashed in its C reference during correctness.
Luma dump 21 contains 96 instruction bytes matching that benchmark's
`vertexReference`, with FPINST `0xee056a08` (`vmla.f32`) and FPEXC
`0xc0000708` (exception plus underflow flag). The harness had selected FPSCR
`0x00800000`, disabling flush-to-zero/default NaN and selecting directed
rounding. QEMU handled the resulting floating-point case; the console raised
an exception reported as undefined instruction.

Version 2 keeps the supported native controls and leaves the exhaustive mode
sweep in QEMU. It also reports and rejects unexpected inherited controls before
the arithmetic test. The assembly kernel is unchanged. The unsafe-write
regression failed on the original harness with 15,360 invalid native-mode
writes and passes after the correction. The physical v2 retest completed on
Old 3DS with FPSCR `0x03000000`, zero correctness failures across 24,646
checks, and all 80 timing rows.
The failed binary, raw dump and decoded evidence remain locally under
`builds/arm11-vertex-crash/`.

## Physical v2 results and v3 candidate

Median paired ratios across both alignments on the tested Old 3DS:

| Vertices | v2 ASM / ordinary C | v2 ASM / disjoint C |
| ---: | ---: | ---: |
| 1 | 1.225 | 1.159 |
| 2 | 1.124 | 1.182 |
| 4 | 1.057 | 1.180 |
| 8 | 1.016 | 1.179 |
| 16 | 0.994 | 1.179 |
| 32 | 0.982 | 1.178 |
| 64 | 0.975 | 1.179 |
| 68 | 0.975 | 1.180 |

A ratio below one is faster. The original assembly loses on small batches,
only modestly beats ordinary C on large batches, and is consistently slower
than the disjoint C control. It must not be enabled on those results.
The raw CSV and summary are saved as
`builds/arm11-vertex-research/hardware-benchmark-v2.csv` and
`hardware-summary-v2.json`; the successful v2 build is archived in
`builds/arm11-vertex-v2/`.

Version 3 reduces register-save traffic and changes scheduling as described
above. The benchmark compares both store variants against the two C controls
and the archived original assembly (`tests/fixtures/vertex_transform_arm11_v2.S`).
All three correctness suites pass for each revised store variant. The generated
native v3 correctness harness also passes under QEMU: 24,576 batches and 73,731
checks across the three assembly variants. The console run adds 69 checks of
the disjoint C control, for 73,800 expected checks. The physical Old 3DS run
completed all 73,800 checks with zero failures and all 80 timing rows.

| Vertices | v3 block / ordinary C | v3 block / disjoint C | v3 scalar / disjoint C |
| ---: | ---: | ---: | ---: |
| 1 | 1.118 | 1.061 | 1.081 |
| 2 | 1.001 | 1.050 | 1.076 |
| 4 | 0.917 | 1.022 | 1.048 |
| 8 | 0.865 | 1.004 | 1.035 |
| 16 | 0.838 | 0.994 | 1.026 |
| 32 | 0.824 | 0.988 | 1.022 |
| 64 | 0.816 | 0.987 | 1.020 |
| 68 | 0.816 | 0.987 | 1.021 |

These are median paired ratios across both alignments. Block stores win;
scalar stores remain slower than the disjoint C control. At 16–68 vertices,
the block kernel saves 16.2–18.4% of ordinary C transform ticks, but only
0.6–1.3% against disjoint C. The original v2 kernel is also timed within this
run: v3 saves 15.3–16.3% against it at those sizes. None of these percentages
is a loader or frame-rate improvement. Raw CSVs and summaries are preserved
in `docs/evidence/arm11-vertex-20260908/`.

## Production loader benchmark

```sh
python3 scripts/build-arm11-vertex-loader-benchmark.py
```

The builder compiles the real `Interpreter`/`RSP` declarations and extracts
`GfxSpVertex` and its six matrix, normal and aspect helpers from production.
The real `Ship::Math::clamp` is compiled separately, preserving its call
boundary. Production profiling checks remain present with profiling disabled.
The simplified fixtures from the earlier integration test are not used for
these timings. The RSP is 4,000 bytes with the current ARM build.

Four separate translation units compare the original C loader, assembly for
all nonempty disjoint batches, assembly for at least 16 vertices, and the
disjoint C transform for at least 16 vertices. Binary symbol renaming exposes
each actual method entry without an extra wrapper call or changes to the
class declaration. Version 1 intentionally retains its original all-count and
16-vertex controls when the production experimental threshold changes.
The builder reproduces the physically tested v1 binary byte for byte with
the current 32-vertex production source.

The Old 3DS game also builds with v3 explicitly enabled: all 299 enhancement
initializers are retained and the linked kernel's 36 instruction words match
the physically tested v3 kernel. This candidate is archived locally as
`builds/arm11-vertex-research/soh-vertex-v3-candidate.elf`. The later 32-vertex
candidate was used in the gameplay pilot below.

The builder first runs the native correctness driver and the exact loader
objects on `qemu-arm -cpu arm11mpcore`. It checks 6,664 cases across rendering
modes, alignment, aliasing, bounds, matrix/light/aspect updates and fixed-aspect
framebuffers. It separately proves unsupported inherited FPSCR controls are
rejected before arithmetic. The native benchmark expects 6,664 checks with
zero failures before timing.
Replacing the kernel with an immediate return in a copy of the QEMU executable
produces 3,712 detected failures, confirming that those checks exercise its work.

The output is
`builds/arm11-vertex-loader-benchmark/soh-arm11-vertex-benchmark.3dsx`.
The console title is **SoH vertex LOADER benchmark v1** and its CSV is
`/3ds/soh/arm11-vertex-loader-benchmark.csv`. Expect 700 rows: seven modes,
ten counts (including 15/16/17), input offsets of zero/two bytes, and five
alternating forward/reverse rounds. Modes 0–6 are unlit, unlit with fog,
directional lighting, directional lighting with fog, texture generation,
linear texture generation, and positional lighting with fog. Lit cases use
two active lights plus ambient.

Every timed path uses the same input and renderer addresses. State restoration,
warmups, SD writes and display updates stay outside each timed interval.
Timing measures cached matrix, light and aspect state; dirty-state paths are
checked for correctness but are not timing scenarios. CSV columns `c_ticks`,
`asm_ticks`, `asm16_ticks` and `c16_ticks` identify the four paths above.
Scene workload frequencies, broader cache traffic and frame-rate effects still
require game measurement.

## Physical loader results and game candidate

The Old 3DS completed all 6,664 checks without failures and all 700 timing rows.
Median paired ASM16/C ratios for large batches were:

| Mode | 32 vertices | 64 vertices | 68 vertices |
| --- | ---: | ---: | ---: |
| Unlit | 0.926 | 0.922 | 0.921 |
| Unlit with fog | 0.938 | 0.936 | 0.937 |
| Directional lighting | 0.935 | 0.932 | 0.933 |
| Directional lighting with fog | 0.936 | 0.939 | 0.938 |
| Texture generation | 0.946 | 0.944 | 0.945 |
| Linear texture generation | 0.960 | 0.960 | 0.959 |
| Positional lighting with fog | 0.916 | 0.916 | 0.916 |

These controlled cached-state loader trials save approximately 4–8.4% against
ordinary C. Disjoint C is essentially tied with assembly in six modes.
The apparent linear-texture-generation gap also exists below the assembly
threshold, so it cannot confidently be credited to the kernel.

Unlit fog has large, sometimes persistent timing spikes in the threshold
variants at some small counts and at 16. Spikes also occur at four vertices,
where no assembly runs. The two threshold loader bodies are each 3,596 bytes
and differ only at eight relocated call instructions; their executed scalar
fallback operations are equivalent. Review found no missed workload-state
reset. The cause is unproven; placement or scheduling would need separate A/A,
rotated-order and repeated-launch measurements to distinguish.

The experimental production cutoff is now 32 vertices. This is a conservative
batch-size choice supported by the large-batch data, not a fix for those fog
timing states. The dispatch regression first failed with 931/6,216 mismatches
under the old cutoff, then passed after the change. Tests explicitly check
31/32/33 and retain overlapping-input coverage at eligible batch sizes.
The actual production layouts/helpers with the 32-vertex candidate also pass
6,664 correctness checks under ARM11 emulation.

Matched game CIAs are prepared under `builds/arm11-vertex-game-test/`:
`soh-vertices-control.cia` uses the normal C loader, and `soh-vertices-test.cia`
enables the experimental 32-vertex path. All other existing ARM11 kernels and
game settings match. The test build retains all 299 enhancement initializers,
and the linked kernel matches the physical benchmark. The production default
stays OFF until gameplay and matched route measurements justify enabling it.

Hardware loader CSV and per-alignment summaries are saved in
`docs/evidence/arm11-vertex-20260908/hardware-loader-v1.csv` and
`hardware-loader-v1-summary.json`.

## Old 3DS gameplay pilot

The user ran the uploaded control (`555c1465`) and 32-vertex assembly test
(`c9f4923a`). These build identities follow the requested install/run procedure;
the frame trace itself does not embed a build identifier. Both captures used
Logging and Frame Timing, with Detailed Profiling and Texture Tracing disabled.

The control trace contains frames 639–938 in the Chamber of Sages
(`kenjyanoma_scene`). The test contains startup/menu frames, an earlier
target-20 segment in `spot00_scene`, then the Chamber of Sages segment at
frames 563–780. Combining both target-20 segments produces a misleading
12.929 submitted frames/s for the test and must not be used as an assembly
performance estimate.

Restricting the descriptive comparison to the later test segment gives:

| Capture | Saved intervals | Duration | Submitted frames/s | Median render time |
| --- | ---: | ---: | ---: | ---: |
| Control, frames 639–938 | 299 | 23.891 s | 12.515 | 51.995 ms |
| Test, frames 563–780 | 217 | 17.665 s | 12.284 | 52.010 ms |

These windows still differ in camera position, scene entry and rendering work.
Complete same-scene diagnostic windows in the test have about 2.3% fewer
draws and 4.7% fewer triangles than control. Both traces also include SD trace
writes lasting over 500 ms. Consequently these numbers establish neither a
gain nor a regression attributable to assembly. They measure submissions,
not LCD presentation, and include logging overhead.

The new vertex option remains **OFF by default**. Earlier ARM11 kernels remain
enabled. Raw captures, hashes, analysis and limitations are archived under
`docs/evidence/arm11-vertex-20260908/`; see `GAME-RESULTS.md`. The user reports
very crackly audio in both builds, including with logging off. Crash and visual
feedback is unspecified. Audio starvation is now the priority investigation;
the renderer profile is deferred. Neither the shared audio problem nor this
pilot's frame-rate differences can be attributed to the new vertex kernel.

## Online references consulted (8 September 2026)

- [Arm AAPCS32](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst):
  d8–d15 preservation, FPSCR rules and eight-byte public-interface stack alignment.
- [GCC ARM options](https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html):
  `armv6k`, `mpcore`, hard-float ABI and `vfp` as an alias of VFPv2.
- [3dbrew hardware](https://www.3dbrew.org/wiki/Hardware):
  Old/New ARM11 MPCore and VFPv2 hardware, model clocks and New-model L2 cache.
- [Luma exception handling](https://github.com/LumaTeam/Luma3DS/blob/master/k11_extension/source/fatalExceptionHandlers.s)
  and [Linux VFP register definitions](https://github.com/torvalds/linux/blob/master/arch/arm/include/asm/vfp.h):
  saved FPINST/FPEXC register layout and decoding of the hardware underflow flag.

Retrieved copies are retained locally in `builds/arm11-vertex-research/`.
