# Handwritten ARM11 kernels

These kernels are an explicit project goal for both audio and game code. They use ARMv6K and VFPv2 instructions supported by Old and New 3DS; they do not use NEON.

## Implemented

- `resample.S`: four-tap audio resampling. Loads two packed coefficient words and uses `SMLABB`/`SMLABT`, signed per-tap shifts and `SSAT`. Input loads stay halfword-sized, so inputs at two-byte alignment remain valid. Rounding happens separately for every tap before summation. Returns the advanced input and writes the fractional phase for the existing C state-management code.
- `matrix_copy.S`: game transformation matrix copies used by `Matrix_Push`, `Matrix_Get`, `Matrix_Put` and the replacement branch of `Matrix_Mult`. Uses VFP load/store-multiple instructions without floating-point arithmetic. All 64 bytes, including NaN payloads and negative zero, remain unchanged. Partial overlap branches to the original C sequence; disjoint and exactly in-place copies use assembly.

Both routines preserve AAPCS callee-saved registers. The resampler is a leaf with an unwind-described stack frame; the matrix copy is a leaf without a stack frame and uses only caller-saved `s0-s15`.

## Verification

Run from the repository root:

```
python3 tests/arm11_kernels_test.py
python3 tests/audio_mixer_arm_3ds_test.py
python3 scripts/build-arm11-benchmark.py
SOH_BUILD_TARGET=soh_3ds_old SOH_BUILD_JOBS=12 scripts/build-3ds.sh
```

The first test runs under `qemu-arm -cpu arm11mpcore`. It compares 8,192 resampler cases against C (PCM, phase, input advancement, guard regions, saturation and alignment), plus 8,448 matrix-copy cases including overlap. The second compares the actual integrated audio mixer and state against its C implementation over 400 randomized sequences. `SOH3DS_DISABLE_ARM11_ASM` retains the C resampler for this comparison.

## Hardware benchmark

Copy `builds/arm11-benchmark/soh-arm11-benchmark.3dsx` to `/3ds/soh-arm11-benchmark.3dsx` and launch it through Homebrew Launcher. Leave it running until it says Done, then press START. It writes `/3ds/soh/arm11-benchmark.csv`; that directory must already exist. The SoH installation already creates it.

The benchmark runs correctness checks before timing. It measures 512/544-sample resampling batches with both halfword alignments, and matrix copies, with warmup and five rounds that alternate C/ASM order. Each timed batch has just two system-tick reads. Printing and file writes occur outside measured intervals. The C matrix reference uses the game's `-Os`; the C resampler uses `-O2`. Results are raw system ticks, so compare paired medians rather than assuming the tick frequency follows New-model CPU speed.

These are isolated-kernel timings on core 0, not measurements of gameplay frame rate or audio's available syscore share. Whole-game comparisons still need the same save, area, route and logging settings. Keep ordinary game profiling and logging off.

## Current limitations

Functional ARM11/QEMU and Old-model Azahar checks pass. The physical Old 3DS benchmark also reports zero correctness failures. Across five alternating-order rounds, median paired time fell 37.72% for the resampler (1.606x throughput) and 51.27% for matrix copies (2.052x throughput). Raw data: `docs/evidence/arm11-kernels-20260908/hardware-benchmark.csv`. These are isolated core-0 results against the benchmark C references, not whole-game gains or an audio syscore scheduling measurement. Azahar timings are retained separately and never substituted for hardware results. Overall audio/frame-rate improvement still needs a game run.

Future candidates include ADPCM reconstruction, animation transforms and collision math. They need their own workload evidence and numerical/ABI tests. Replacing C with assembly by itself is not proof of a speed gain.


Second pass (2026-09-08): `audio_mix.S` adds ADPCM prediction, gain/reverb-buffer
mixing and envelope mixing. `matrix_vec.S` adds the game vector transform,
with the original C sequence retained for overlapping writes. Run
`python3 tests/arm11_extended_test.py` and
`python3 scripts/build-arm11-extended-benchmark.py` for the shared correctness
and hardware harness. The benchmark writes a separate
`/3ds/soh/arm11-extended-benchmark.csv`; its timings are not gameplay FPS.
See `docs/evidence/arm11-stage2-20260908/RESULTS.md` for measured candidates,
including the rejected first vector implementation and revised measurements.

The next candidate is a [batched renderer vertex transform](vertex_transform.md).
It passes ARM11 exact-result and renderer integration tests and has a separate
physical-console benchmark. On Old 3DS, v3 saves 16.2–18.4% of isolated
transform ticks at 16–68 vertices against ordinary C, but only 0.6–1.3%
against disjoint C. `SOH3DS_ARM11_VERTEX_ASM` defaults to OFF because a gameplay
benefit remains unproven. Controlled loader trials at 32–68 vertices save approximately
4–8.4% against the original C loader, with unexplained timing states in some
other cases. The experimental game candidate uses a 32-vertex cutoff;
both game packages have now been run on Old 3DS. The pilot captures differ in
scene windows and workload and contain logging stalls, so they establish no
assembly-attributable FPS gain. See the vertex document's gameplay pilot and
`docs/evidence/arm11-vertex-20260908/GAME-RESULTS.md` for the recorded evidence.

## Combined CPU follow-up

The next opt-in candidate is `skin_matrix_mult.S`, used by
`SkinMatrix_MtxFMtxFMult` when `SOH3DS_ARM11_MATRIX_MULT=ON`. It uses ARMv6K
and scalar VFPv2; exact finite results and supported in-place-left multiplication
are checked against the retained C body. Deterministic IEEE edge cases compare
non-NaN bits and NaN classification; payload selection is not preserved.
Runtime callee-saved register checks and a deliberately broken multiply verify
the test. Physical timing remains pending.

Run `python3 scripts/build-arm11-matrix-mult-benchmark.py` for the standalone
correctness-first hardware timer; normal builds leave this candidate OFF. The
new renderer option `SOH3DS_TRIANGLE_RUN_REUSE=ON` reuses triangle state only
within uninterrupted same-opcode command runs, with debugger/tracing fallback.
Both options are explicitly reset OFF by the canonical script when omitted.

Software evidence and candidate dispositions, including selective compiler
experiments and the GPU lighting numerical probe, are retained in
`builds/old3ds-cpu-batch-20260908T201258Z/`. No whole-game speed gain is yet
measured for this batch.

## Third pass (2026-09-11): Audio QADD16 mixer, VFP burst copy, and Old 3DS memory reduction

1. `audio_mix.S` (`Soh3dsAddMixerArm11`): signed 16-bit saturating vector addition using ARMv6K `QADD16`. Adds two samples per instruction with hardware saturation [-32768, 32767], bit-exact with `clamp16(*out + *in)`. Replaces scalar C loop in `aAddMixerImpl` with a 16-sample unrolled `ldmia`/`stmia` burst loop. Verified across 16,384 cases in `tests/arm11_extended_test.py` and 400 full-sequence replays in `tests/audio_mixer_arm_3ds_test.py` under QEMU ARM11.
2. `depth_copy.S` (`Soh3dsCopyWordsArm11`): 64-byte burst copy using VFPv2 load/store multiple (`vldmia`/`vstmia` across caller-saved `s0-s15`) with `pld` cache prefetching. Replaces generic scalar `memcpy` in `DepthSnapshot3DS::Capture`.
3. `system_3ds.cpp` (`Soh3dsInvalidateDataCache`): direct `svcInvalidateProcessDataCache` (SVC 82) for GPU readback invalidation, avoiding GSP sysmodule IPC latency.
4. CPU optimizations:
   - Hoisted `DisableDrawDistance` and `WidescreenActorCulling` CVar queries outside the per-actor loop in `Actor_DrawAll` (`z_actor.c`).
   - Cached `DisableLOD` CVar query in `z_skelanime.c` to run once every 64 calls instead of per-limb.
   - Replaced fixed-point division by `65536.0f` in `GfxSpMatrix` (`interpreter.cpp`) with multiplication by `(1.0f / 65536.0f)`, eliminating 16 non-pipelined VFP11 `vdiv.f32` stalls per matrix.
   - Gated out debug `Matrix_CheckFloats` (32 float comparisons per limb) on 3DS inside `Matrix_ToMtx` (`sys_matrix.c`).
5. BSS memory reduction:
   - Shrunk dead 98,304-byte N64 RSP `gGfxSPTaskOutputBuffer` on 3DS (Fast3D interprets from workBuffer; nothing reads RSP output). Reclaims 96 KiB BSS.
   - Converted static 138,488-byte `saveContextSave` in `SaveManager.cpp` (`ConvertFromUnversioned`) to dynamic heap allocation during legacy migration. Reclaims 135 KiB BSS.
   - Total static BSS reduction: 236,792 bytes (~231 KiB) permanently committed memory freed on Old 3DS.

## Fourth pass (2026-09-11): ARMv6 pack kernels

1. `audio_mix.S` (`Soh3dsInterleaveArm11`): final stereo interleave for
   `aInterleave`, which runs on every audio update (`audio_synthesis.c:674-676`).
   `PKHBT`/`PKHTB` pack left/right halfwords into output words, so four stereo
   frames cost two `ldmia` loads, four packs and one `stmia` instead of sixteen
   scalar halfword accesses. Word path requires all three buffers word-aligned;
   otherwise a halfword scalar path runs. DMEM buffers are only guaranteed
   halfword-aligned, so the check is load-bearing.
2. `matrix_f2l.S` (`Soh3dsMtxF2LArm11`): `guMtxF2L` float-to-N64-fixed matrix
   conversion. `VLDMIA` bursts eight floats, then `VMUL`/`VCVT` per element and
   `PKHTB`/`PKHBT` collapse the six mask/shift/or operations per pair into two.
   `guMtxF2L` compiles to a single tail-call branch. The float multiply is kept
   separate rather than folded into a fixed-point `VCVT #16`: the reference
   rounds the product to a float first, so a scaling `VCVT` would be exact and
   could differ by one LSB past 24 mantissa bits. `VCVT.S32.F32` truncates
   toward zero and saturates, matching the C cast.
3. `audio_mix.S` (`Soh3dsInterlArm11`): every-other-sample copy for `aInterl`,
   the reverb downsample. One `ldmia` of sixteen samples, four `PKHBT` packs and
   one `stmia` replace sixteen scalar halfword accesses. The reference reads and
   writes sequentially so overlap is observable; the burst path is used only when
   the destination does not run ahead of the source, and `out>in` takes a scalar
   path matching the reference exactly. The wrapper preserves the reference's
   `do/while`, where a zero count still processes one group of eight.

All three preserve AAPCS callee-saved registers and use only caller-saved
`s0-s15`. With these, every scalar loop in the N64 audio mixer that the game
actually issues is now ARM11 assembly.

Verified with `python3 tests/arm11_extended_test.py`: 16,384 comparisons now
include interleave across all three buffer alignments, `aInterl` across the
burst path, the scalar path, exact aliasing (`out==in`) and forward overlap, and
8,192 `guMtxF2L` cases spanning saturating magnitudes, sub-LSB fractions,
denormals, infinities and NaN, checking the source matrix for unintended writes
as well as output. Each kernel was mutation-tested (a wrong shift, and removal
of the `aInterl` overlap guard) and the test failed before being restored, so
the cases genuinely execute.

Expected gain is small and honestly bounded: `aInterleave` moves ~33k stereo
frames/second and `guMtxF2L` runs ~58 times per tick, so these are instruction
count reductions, not a measured frame-rate change. They are exactness-preserving
and on always-on paths.

## Where the remaining Old 3DS cost actually is

Investigated and deliberately **not** converted to assembly:

- `DeriveTriState` (~12.6 ms/frame on hardware) is branchy control flow that
  calls `Flush()`, `ImportTexture()` and `LookupOrCreateColorCombiner()`, and
  `Flush()` issues a draw. That time is draw and texture-import cost attributed
  to the enclosing profile scope, not instruction-level math. Assembly is the
  wrong tool; the win there is algorithmic.
- `PackGenericVertices` (~4.6 ms/frame) branches per vertex on state that is
  loop-invariant for the batch. Hoisting/specializing is the fix, not assembly.

RAM candidates checked and rejected as live, to stop them being re-proposed:

- `gZBuffer` (153,600 B) is not dead on this port: it is the pause-background
  save buffer (`PreRender_SetValues` `fbufSave`), the transition buffer
  (`z_play.c:827`) and the JPEG scratch for prerendered rooms (`z_room.c:244`).
- `D_0E000000` / `D_0F000000` (153,600 B each) are texture sources for
  `VisZbuf` and `VisMono`; `VisMono_Draw` is live (`z_play.c:1516`, `game.c:79`).
- `Rando::StaticData::locationTable` is the largest single BSS object at
  1,443,616 B, but it is read directly across many randomizer files, so making
  it lazy is an invasive refactor rather than a storage-class change.

## Fifth pass (2026-09-11): activating assembly that was already written

The largest single finding of this session was not new code. Six validated
optimizations, several of them hand-written ARM11 assembly with their own
hardware benchmarks, defaulted to OFF and were therefore absent from every
shipped build. Hardware telemetry proved it: `emitmix` reported `arm11=0`, so
all 67,293 triangles per 60 frames went through the generic C path.

| Option | What it is | Validation re-run in this tree |
| --- | --- | --- |
| `SOH3DS_ARM11_TRIANGLE_EMIT` | hand-written triangle emission | 34,384 emission comparisons |
| `SOH3DS_TRIANGLE_RUN_REUSE` | reuse state within command runs | 84 cases; key captures 360 -> 2-3 |
| `SOH3DS_ARM11_MATRIX_MULT` | `SkinMatrix_MtxFMtxFMult` | 32,768 exact products |
| `SOH3DS_ARM11_VERTEX_ASM` | batched vertex transform | 24,579 + 6,216 checks |
| `SOH3DS_ARM11_FILTER_ASM` | exact eight-tap audio filter | 18,433 PCM/history cases |
| `SOH3DS_FAST_GPUCMD` | single-parameter GPU register writes | 13,824 cases vs pinned libctru |
| `SOH3DS_OLD_CPU80` | Old CIA `MaxCpu` 0x9E -> 0xD0 | descriptor test: 30 / 80 / reset |

Defaults were left OFF: `triangle_emit_build_test` asserts that the canonical
build defaults OFF, so enabling happens through build flags. `scripts/build-3ds.sh`
already plumbed every one of them.

Two interactions worth keeping:

- `TRIANGLE_RUN_REUSE` raises the ARM11 emitter's share from 59-61% to 80-92%,
  because stable state keeps the assembly path selected. They compound.
- `COMPACT_VERTEX_STREAM` is left OFF on purpose. It passes its 14,764 checks,
  but with it on the selector routes triangles to `compact=` and `arm11=` drops
  to 0: the compact C path *replaces* the hand-written emitter. Emulator frame
  rate is identical either way, so there is no evidence to prefer C over the
  assembly. Re-test explicitly before ever enabling it.

`SOH3DS_OLD_CPU80` is not a micro-optimization: the Old CIA was requesting at
most 30% of core 1, which is where audio synthesis runs.

## Corrections to earlier claims in this document's lineage

Both were measured and both were wrong in the optimistic direction:

1. **Profiling overhead is ~0.6 ms/frame, not ~20 ms.** `Soh3dsProfileBegin`
   already samples 1-in-64 for Vertex/Triangle/TriangleKey/TriangleEmit and
   1-in-16 for the hot sections. The hardware log's own `samples=` fields sum to
   7,982 per 60 frames = 266 `svcGetSystemTick` calls per frame. So the 59.9
   ms/frame render cost is real, not a measurement artifact.
2. **`FAST_GPUCMD` is worth ~1.8 ms/frame, not the headline.** Instrumenting
   `__wrap_GPUCMD_Add` (then removing the instrumentation) measured 84.1% of
   calls as single-parameter, but only ~24 calls per draw and ~6,234 per frame.
   Command writes are not what makes a draw cost 96 us.
3. **Hand-hoisting invariants in `PackGenericVertices` made it worse**, despite
   `-fno-strict-aliasing`: `ldrb` 215 -> 202 but +609 instructions and +2,436
   bytes, because GCC `-O3` already performs the motion. Reverted, with the
   measurement recorded at the loop so it is not retried.

## Measured hardware frame budget (Old 3DS, per frame)

Render 59.9 ms + game tick 15.6 ms = 75.5 ms, i.e. 13.2 fps observed against a
50 ms budget for 20 fps. Non-nested scope costs:

    tristate 16.2  state 8.9  emit 8.7  vertex 8.4  pack 5.5
    trikey    4.9  depth 3.0  texture 2.8  draw remainder ~5

Only 204 draws and 1,064 triangles per frame, at ~294 us of CPU per draw, with
the GPU idle at 6.93 ms. The console is CPU-bound in per-draw state derivation
and submission, not geometry throughput. The enabled options address roughly 38
of the 59.9 ms; closing the gap needs about 25 ms.

The backend already caches TEV state, but `tristate` fires 182 times per 204
draws, so the material changes on nearly every draw and the cache rarely hits.
Reducing the draw count would require batching by material, which reorders
rendering and was not attempted.

## Hardware verdict (2026-09-11 07:52): the assembly is live and is not the limiter

Read this before planning more kernels.

`emitmix` reported `arm11=39410` against `generic=28330` - 58% of triangles
through hand-written assembly. Every earlier hardware run reported `arm11=0`,
because the options were switched off. So this is the first run that actually
exercised the ARM11 renderer path.

It did not reduce render CPU:

| same scene, frame 1800 | no ARM11 (04:19) | ARM11 live (07:52) |
| --- | ---: | ---: |
| cpu60 | 3794 ms | 3809 ms |
| draws per 60 frames | 12219 | 12497 |
| tris per 60 frames | 63815 | 66207 |
| emitmix arm11 | 0 | 39410 |

With the ARM11 emitter carrying 58% of triangles, plus `TRIANGLE_RUN_REUSE`,
`FAST_GPUCMD`, the vertex transform ASM and six matrix kernels, cpu60 stayed at
~63 ms/frame. The per-scope attribution (tristate 16.2 ms, emit 8.7, vertex 8.4)
summed to the right total but did not identify cost that removing instructions
recovers.

What limits the frame is the allocator:

    cpu60=3809  wait60=38  loop60=6084  tick max=5968 ms
    fps10=6.0   tps=5.8    ordblks=49668  keep=136  heap=35356/37876 KiB

Single ticks take 1.6-6.0 seconds, and the run crashed:

    alloc-fail req=1072 fordblks=1530120 ordblks=49663 keepcost=720
    fatal std::bad_alloc   (ResourceFactoryBinaryAnimationV0::ReadResource)

A 1,072-byte request failed with 1.5 MB free, fragmented into 49,663 blocks
averaging ~30 bytes, 720 bytes contiguous at the heap top.

Root cause: the scene-eviction pressure sweep in `z_play_otr.cpp` gated on
`heapCap - uordblks <= gSceneEvictSweepMB` with a 12 MB default. Under the CIA
the cap is ~37.7 MB and the working set ~35 MB, so headroom can never exceed
~2.5 MB and the condition is always true: the sweep dumped the entire `objects/`
cache on every scene transition, and the incoming scene's reload churn is the
fragmentation. The 12 MB figure had been calibrated against a run reporting an
84 MiB heap - an hbmenu/3dsx launch, which inherits the host title's exheader.
The floor is now derived from the actual cap (`heapCap/16`).

Lesson for future work on this port: verify an optimization is live on hardware
before believing a scope attribution, and check a threshold against the launch
mode it will actually run in. Two separate conclusions here were wrong because a
3dsx measurement was applied to a CIA process.

### Correction: the eviction sweep is NOT the cause (refuted 2026-09-11)

The paragraph above attributes the fragmentation to the pressure sweep. A
controlled emulator A/B refutes that, and it is recorded here so the wrong
explanation does not get inherited.

Same build, same route length, only `gSceneEvictSweepMB` changed:

| | ordblks start | ordblks end |
| --- | ---: | ---: |
| sweep forced on (floor 40 MB) | 43,425 | 45,222 |
| sweep disabled (0) | 43,774 | 45,231 |

No measurable difference. Two further observations from the same runs:

- The sweep fires only on scene transitions - twice in 200 s - and released
  only **2 of 8** and **0 of 7** directories, because `releaseObjectDir`'s pin
  guard and its `gameplay_` / `object_link_` filters reject most of them. So it
  never "dumps the whole objects/ cache"; that claim was wrong.
- A rate argument settles it independently of platform: hardware ordblks grew
  37,901 -> 47,048, about 91 blocks/second. Attributing that to ~4 transitions
  would require each to add ~2,287 blocks, against a measured 0-2 directories
  released. Fragmentation grows continuously, so its driver must be per-frame.

The `heapCap/16` change is still correct on its own terms - a 12 MB floor under
a 37.7 MB cap does fire unconditionally, which is a real miscalibration - but it
should not be expected to fix the stalls or the crash.

The actual driver is the per-frame small-allocation churn measured in Azahar:
~367 allocations per frame in steady state, 60% of them in the 33-64 byte class,
with the ~222/frame figure tracking the 204 draws/frame. Localizing the call
site needs a sampled `_Unwind_Backtrace`; `__builtin_return_address(1)` does not
walk past `operator new` under `-fomit-frame-pointer` (it returned the same
addresses offset by 8), and frame 0 is always `operator new` itself, which names
nothing.

### The churn site, found and fixed (2026-09-11)

The sampled unwind referred to above was done. On the 33-64 byte class, one
site accounted for 32% of samples: a `std::string` built in
`src/compat3ds/compact_zip.cpp`'s `Name()`, reached from
`O2rArchive::ListFiles(filter)`.

`ListFiles` walks **every entry in the archive** - 39,057 for a vanilla
`oot.o2r` - and built a `std::string` for each one purely to glob it, discarding
every non-match. `releaseObjectDir` calls it once per object directory per scene
transition, so each transition burst roughly 39,000 short-lived small
allocations. That is the mechanism by which a free list becomes ~50,000
fragments averaging 30 bytes.

Two traps cost a cycle each and are worth remembering:

- Frame 0 of a backtrace taken inside the malloc wrapper is always
  `operator new`, which names nothing.
- `__builtin_return_address(1)` does not walk past it under
  `-fomit-frame-pointer`; it returned the same addresses offset by 8. Only a
  real `_Unwind_Backtrace`, skipping to frame 3, named the requester.

Fix, allocation-free and semantics-preserving:

- `Soh3dsCompactZipApi` gains `nameInto(handle, index, out, capacity)`. An
  absent or oversized name returns 0 and the caller takes the original
  allocating path, so behaviour is unchanged.
- `ListFiles` globs against a 512-byte stack buffer and allocates only for
  matches.
- `Read()` no longer builds a `std::string` just to verify exact spelling
  (`NameEquals`); a name that does not fit compares unequal and takes the
  existing case-sensitive fallback: slower, still correct.

Measured in Azahar, same build and route, sampling 1 in 512 of the class:

| | total sampled | dominant site |
| --- | ---: | ---: |
| before | 6,093 | 1,953 |
| after | 4,416 | 81 |

The site fell 96% and the class fell 27.5%, about 858,000 fewer allocations per
run. Allocation counts are platform-independent, so this carries to hardware;
`ordblks` barely moves in Azahar only because it has 34 MB of headroom there and
never approaches the ceiling.

A second 32-slot pass attributed the remainder: 1,533 `map<string,...>` node
inserts, 486 from `ResourceMgr_UnpatchGfxByName`, 481 from nlohmann::json, and
342 the legitimate `ListFiles` matches. The obvious suspect was already handled -
`CosmeticsUpdateTick` returns early when no option is in rainbow mode, so the
gfx-patch path is not per-tick - which makes the rest most likely a startup
burst. The site counters are cumulative from boot, so separating startup from
steady state needs per-heartbeat deltas; that was left undone deliberately
rather than optimized blind before the hardware result for this fix is known.

### Steady-state churn attributed (2026-09-11)

The cumulative site counters could not separate a startup burst from
steady-state churn, so the heartbeat was changed to report per-interval
deltas. First attempt still failed: the 32 slots fill with one-off boot sites,
after which every steady-state site lands in the overflow counter and cannot
be named (`over=` grew ~20 per interval while only one site showed a delta).
Clearing the table once at the first heartbeat fixed coverage - `over=17` then
stayed flat for the rest of the run, so the named sites are the whole story.

Measured steady state, per 60 frames (1-in-512 sampling, ~29 samples):

| ~hits | site | reached from |
|------:|------|--------------|
| ~11 | `ResourceIdentifier::ResourceIdentifier(std::string)` | `ResourceManager::LoadResource(std::string)` |
| ~10 | `std::string` ctor | `ResourceMgr_LoadGfxByName` |
| ~6 | `LoadResource(ResourceIdentifier)` | `LoadResource(std::string)` |
| ~2 | SDL controller hashtable node | `_Map_base` |
| ~1 | `gfx_push_current_dir` | `gfx_pushcd_handler_custom` |

The top three are one path and account for ~27 of ~29 samples, i.e. roughly
**230 allocations per frame** in the 33-64 byte class. `ResourceMgr_LoadGfxByName`
(`soh/ResourceManagerHelpers.cpp:425`) is called per display list per frame -
z_player_lib limb DLs, En_Box, GbiWrap - and each call resolves a path, then
builds a `std::string` and a `ResourceIdentifier` to do a cache lookup that
almost always hits.

Deliberately **not** fixed here. The cheap fix is memoizing by path pointer
(the decomp passes static literals, so pointer identity is stable), but a
cached `Gfx*` goes stale when the resource cache evicts - which is exactly the
class of bug that produced the wrong prerendered-room images earlier in this
port. A correct version has to invalidate with the resource cache, and that is
not worth writing before the current build has a hardware number: its
allocation fix targets transition bursts, and if those were what produced the
6-second ticks, this per-frame path may not matter.

### Rejected: `--gc-sections` on the image (measured 2026-09-11)

The image is 23.1 MB `.text` + 3.6 MB `.rodata` + 3.0 MB `.bss`, resident in
the same region as the arena, so section GC looked like free heap. Built the
whole tree with `-ffunction-sections -fdata-sections` on `soh_decomp`,
`soh_enhancement` and `soh_3ds` plus `-Wl,--gc-sections`:

|  | .text | .rodata | .bss | text symbols |
|---|---:|---:|---:|---:|
| before | 23692.73 KiB | 3729.50 KiB | 3083.45 KiB | 33,998 |
| with GC | 23692.73 KiB | 3729.50 KiB | 3083.45 KiB | 33,998 |

**Exactly zero bytes**, and the flags were confirmed present in both
`flags.make` and `link.txt`. The 299 static initializers were retained, so it
was not over-pruning either - there is simply nothing unreachable. Two reasons:
`libultraship.a`, `libImGui.a`, SDL and libctru are prebuilt *without* function
sections, so GC can only drop whole objects there (the linker already does
that), and their vtables sit in monolithic `.rodata` sections that anchor every
virtual function they name. Reverted - it costs a 730 s full rebuild and buys
nothing.

`.bss` is also close to irreducible by storage class: `locationTable`
(1,443,104 B) is the only large object left and is referenced directly across
many randomizer files, and `gGfxPools` (299,040 B), `gZBuffer`/`D_0E000000`
(153,600 B each) and `gSaveContext` (138,488 B) are all live. Image size is not
a cheap lever on this port; the remaining levers are allocation behaviour, not
footprint.

### The by-name lookup copied its path twice (fixed 2026-09-11)

`ResourceMgr_GetResourceByNameHandlingMQ` builds a `std::string` from the
incoming `const char*`, then `ResourceManager::LoadResource(const std::string&)`
constructed a `ResourceIdentifier` whose `const std::string Path` **copied it
again**. Two allocations per lookup, hundreds of lookups per frame.

`ResourceIdentifier` already had a `std::string&&` constructor, so the fix is
three lines and changes no semantics: `LoadResource` takes `std::string` by
value and `std::move`s it into the identifier, and the SoH caller moves its
dead `resolvedPath` down. No new overload - an added `std::string&&` overload
would make every `const char*` call site ambiguous.

Measured with the same per-interval sampling (33-64 byte class, 1-in-512):

| | samples / 60 frames | dominant site |
|---|---:|---:|
| before | ~29 | ~11 `ResourceIdentifier(std::string)` |
| after | **~20** | ~11 `std::string` in `GetResourceByNameHandlingMQ` |

**31% off the whole size class**, ~230 -> ~160 allocations/frame, and the
`ResourceIdentifier` copy is gone from the profile entirely. What remains is the
one string the cache key owns; removing it needs heterogeneous lookup
(`is_transparent` hash/equal over `string_view` + owner + parent) on the cache
map, which changes key semantics and was not attempted.

Deliberately still **not** done: memoizing `Gfx*` by path pointer. It would
remove the last allocation too, but the cached pointer goes stale on resource
eviction, tunic CVar changes and MQ toggles - the failure mode that produced
wrong prerendered-room images earlier in this port.

`tests/resource_path_resolution_test.py` now guards the move. Its mock
`ResourceManager` mirrors libultraship - `LoadResource` takes the path by value
and constructs an identifier that owns a `std::string` - so two warmed cache
hits must cost exactly two allocations, one per lookup. Mutation-checked:
replacing `std::move(resolvedPath)` with `resolvedPath` in production fails the
test, and restoring it passes.

### Allocation-free cache probe (2026-09-11)

The remaining per-frame site was the one `std::string` the cache key must own.
Removed by looking the cache up heterogeneously instead:

- `ResourceIdentifierProbe { string_view Path; uintptr_t Owner; Archive* Parent; }`
  carries the three key fields by reference.
- `ResourceIdentifierHash` and a new `ResourceIdentifierEqual` are
  `is_transparent`, and `mResourceCache` now names the equality type.
- `ResourceIdentifier::CalculateHash` hashes through `std::hash<std::string_view>`
  for both the path and the parent path, so probe and key hash identically **by
  construction** rather than by trusting libstdc++ to route `hash<string>` and
  `hash<string_view>` to the same bytes. `mHash` is never persisted.
- `GetCachedResource` now takes a `std::string_view` (replacing the
  `const std::string&` overload, so no `const char*` call site turns ambiguous).
  It strips the `__OTR__` prefix exactly as `LoadResource` does - keys are stored
  unprefixed, so a naive probe would have missed on every path and silently
  degraded to the old cost.
- `ResourceMgr_GetResourceByNameHandlingMQ` probes with a view first and only
  allocates on a miss. The MQ `/nonmq/` -> `/mq/` rewrite goes into a 256-byte
  stack buffer (it shrinks by 3), falling back to the owning path if it does not
  fit, so MQ players get the same benefit.

Measured, per 60 frames, 33-64 byte class, 1-in-512 sampling:

| build | samples | allocs/frame | site from GetResourceByNameHandlingMQ |
|---|---:|---:|---:|
| original | ~29 | ~230 | ~21 (string + identifier copy) |
| by-value move | ~20 | ~160 | ~11 |
| probe | **~6.7** | **~57** | **~3** |

**77% off the class overall.** The ~3 that remain are genuine cache misses,
which must allocate. `texUploads` stayed at 1-2 per interval, confirming the
probe hits rather than silently missing and reloading.

Azahar read `fps10=28.9` reproducibly here against `30.0` on the probe build
(which does strictly more work), with `tps=19.9` unchanged. That is inside the
28.2-30.0 band this session saw on identical binaries, so it is not evidence
either way - and Azahar fps has repeatedly failed to predict this hardware.

`tests/resource_path_resolution_test.py` asserts every warmed hit path allocates
**zero** - non-MQ, MQ-rewritten, and MQ-with-no-match - and its mock mirrors
production's shared_ptr-returning Context so a per-lookup refcount copy would
also show. Mutation-checked: disabling the probe fails it.

### The last two per-frame allocators (2026-09-11)

After the probe, the profile named exactly two remaining per-frame sites,
~2 samples each (~17 allocations/frame apiece). Both turned out to be waste
rather than work.

**1. `gfx_push_current_dir` was allocating for data nothing read.** The
`std::stack<std::string> currentDir` in `interpreter.cpp` had a declaration, a
push (from the `G_PUSHCD` opcode handler), and a reset at the end of every
`RunCommands` - **no pop and no reader anywhere in the tree**. So each pushed
path was allocated, never consulted, and thrown away at frame end. The public
entry point `ResourceMgr_PushCurrentDirectory` had no live callers either (its
only use in `GbiWrap.cpp` is commented out). Deleted the stack, the pusher, the
now-unused `GetPathWithoutFileName`, and the dead bridge; `gfx_pushcd_handler_custom`
just consumes the command. Deleting rather than no-op'ing the function is
deliberate: a future reader gets a compile error instead of a silently empty
stack.

**2. `GetConnectedSDLGamepadsForPort` rebuilt a map per call.** Every SDL
mapping object calls it once per frame and either iterates it or asks if it is
empty; it materialised a fresh `unordered_map` (bucket array + a node per pad)
each time. The answer only changes on device connect/disconnect or an
ignore-list edit, and all those paths live in the same class, so it now returns
a `const&` into a per-port cache cleared at those three points. All 16 call
sites are range-for or `.empty()`, so the reference binds unchanged, and
references into an `unordered_map` survive rehashing of the outer map.

Also fixed on the way: `PortIsIgnoringInstanceId` used
`GetIgnoredInstanceIdsForPort`, which returns the set **by value** - a full
`unordered_set` copy per gamepad per call - and whose `operator[]` inserted an
empty ignore set for every port ever queried. It now does a `find`.

`tests/connected_gamepads_cache_test.py` extracts the four production functions
and drives them against a stub with the same members: warmed reads allocate
zero and return the same object, querying a port inserts no ignore set,
ignore/un-ignore are visible immediately, one port's ignore does not disturb
another, and a disconnect leaves no stale pointer. Mutation-checked - dropping
either invalidation, or restoring the by-value set copy, fails it.

Azahar after both changes: `fps10=28.9 tps=19.9 catches=0`, identical to the
build before them, so neither regressed anything measurable there.

### Allocation rate by size class, and where it stops being worth fixing (2026-09-11)

Every measurement above sampled only the 33-64 byte class, because that is where
the `bad_alloc` happened. Counting *all* classes exactly (per-frame, averaged
over each 60-frame heartbeat) gives the full picture:

| class | <=16 | <=32 | <=64 | **<=128** | <=256 | <=1k | >1k |
|---|---:|---:|---:|---:|---:|---:|---:|
| allocs/frame | 8-13 | 11-12 | 32-35 | **66-96** | ~1 | 0-5 | 0 |

Two things worth noting. The 33-64 class now counts 32-35 per frame by exact
counting, which independently corroborates the sampled 230 -> ~57 figure from
the fixes above. And the largest remaining class is 65-128, which had never been
attributed.

Sampling it at 1-in-256 named it almost entirely (`over=0`): frame
interpolation. `InterpolateCtx::interpolate_branch` inserting into
`unordered_map<Mtx*, MtxF>` (~47/frame) plus `FrameInterpolation_Interpolate`
itself (~43/frame) - `MtxF` is 64 bytes, so each node lands in this class.
`FrameInterpolation_Interpolate` builds a fresh map every interpolated frame,
returns it by value, and the caller destroys it.

**Deliberately not converted.** Two reasons:

1. *It is the benign churn pattern.* These are uniform-size nodes allocated and
   freed within the same frame, so they recycle through one dlmalloc bin and do
   not fragment. The crash came from mixed small allocations leaving 49,663
   sub-30-byte holes under a 1,072-byte request - a different mechanism.
2. *The safe fix is not cheap.* Consumption is pure `find()` by key
   (`interpreter.cpp:1723`), never iteration, so the map *can* persist across
   frames - but `gGfxPools` is double buffered, so a `Mtx*` address is reused,
   and an object present this tick but absent last tick legitimately gets no
   replacement. A leftover entry would apply another object's transform: the
   same stale-key failure mode as the wrong prerendered-room images. Doing it
   safely means generation-stamping the value, which changes
   `unordered_map<Mtx*, MtxF>` across 16 sites in libultraship, SoH and the
   window layer, to remove a cost Azahar cannot measure.

If hardware shows a flat heap and long ticks, this is where the remaining
per-frame CPU sits and the conversion is justified. Not before.

### Callsite audit for the view-based cache lookup (2026-09-11)

`GetCachedResource` changing from `const std::string&` to `std::string_view`
also made it strip the `__OTR__` prefix, which is a behaviour change: prefixed
paths previously missed unconditionally (keys are stored unprefixed) and can now
hit. Audited all three external callers:

- `z_play_otr.cpp:205,237,251` (scene eviction) - `f` comes from
  `ArchiveManager::ListFiles`, i.e. archive entry paths, already unprefixed. No
  change. (And in that direction a hit is the safe outcome anyway: a miss costs
  a re-upload, a stale key shows the wrong room.)
- `ResourceManagerHelpers.cpp:241` (room prefetch budget) - the argument comes
  from `RoomPrefix3DS`, which strips `__OTR__` itself at
  `RoomPrefetchPolicy3DS.h:18`. No change.
- `libultraship/tests/archive_resource_tests.cpp` - string literal and
  identifier overloads, both still bind.

So the stripping only takes effect for the new hot caller, which passes the
game's raw prefixed path. No existing behaviour depends on the old miss.

### The Azahar fps question, closed

`fps10` read 28.9 on the last several builds against 30.0 earlier, which looked
like it might be a cost of the cache work. It is not, and the existing data
already shows it: `probe2.3dsx` - the transparent-lookup build *with* sampling
instrumentation compiled in - read **30.0**, while `rc2.3dsx`, the same source
with the instrumentation removed, read **28.9**. The build doing strictly more
work measured faster, so the difference is host run-to-run variance. `tps`
stayed at 19.9 throughout, which is the metric that governs game speed.
