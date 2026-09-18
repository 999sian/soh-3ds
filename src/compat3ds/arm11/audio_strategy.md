# Old 3DS synth performance and offload

The user reports severe crackling in both vertex control/test builds, even
with Logging disabled. Audio continuity is the current priority. Existing
logs show repeated drained refills, no dropped batches, and at most four of
eight NDSP waves occupied. Synthesis elapsed time exceeds produced audio
duration in sustained recorded windows. A larger queue cannot compensate
indefinitely for that shortfall.

The audio producer already runs on Old 3DS core 1, priority 0x18, with a
128 KiB stack. New 3DS uses core 2 when available. The 2048-frame reservoir
holds 64 ms at 32 kHz; the independent producer refills below approximately
47 ms. The measured synthesis timer excludes its outer mutex and NDSP
submission, but includes internal locks, resource loading and preemption.

## Options

| Approach | Potential benefit | Main constraint |
| --- | --- | --- |
| More ARM11 mixer assembly | Reduce CPU time in remaining sample loops | ADPCM prediction, resampling, envelope and gain mixing already have measured assembly paths; remaining cost must be located |
| Full synth assembly rewrite | Additional control/sequence optimizations | Assembly cannot remove scheduling limits or SD/cache waits; extensive sequencing/state code needs exact behavior preservation |
| Change Old CPU scheduling | Give audio more predictable execution | Moving work to core 0 competes with rendering; compare audio continuity and game speed together |
| Use more of stock NDSP | DSP handles playback, interpolation, mixing, filters and supported decoding | N64 voice/reverb/envelope semantics do not map directly to the exposed NDSP API |
| Custom DSP synth | Offload software synthesis from ARM11 | Requires an XpertTeak program, communication/buffering, decoder and effect validation, and replacement of the current NDSP path |

Prioritize measured ARM11 work and scheduling experiments. Full DSP offload
is possible in principle and is a separate backend project, not a switch in
libctru. Normal builds keep their existing CPU assignment and audio quality.

## Physical capture and core-0 experiment

The 8 September capture reports core 1, priority 0x18, and a successful APT
CPU-limit query of 30. Its audio log has the exact previous 4970-byte prefix;
the appended session contains 15 sampled-stage windows. Window 1 has a
10.28-second command-stage total, including a 10.24-second maximum batch.
Do not treat this startup stall as steady-state mixing cost.

Windows 2–13 contain 180 sampled batches: 3.000 seconds of produced audio
required 5.709 seconds of elapsed synthesis time (190.3%). Mixing accounts
for 90.03%, sequencing 9.09%, and the other stages together 0.88%. The
corresponding 3072 full batches give a similar 190.0% ratio. Between queue
calls 512 and 3072, 2046 of 2560 refills found the queue drained (79.9%), with
no dropped batches. The final two windows are lighter workloads and are
reported separately; the audio log cannot establish their exact scenes.

The filter is active (5263 calls in those sampled batches), but its individual
cost is not timed. A whole synth rewrite is not justified by these aggregate
wall times. Test the restricted-core hypothesis first:

```
SOH3DS_OLD_AUDIO_CORE0=ON SOH3DS_AUDIO_PROFILE=ON \
  SOH_BUILD_TARGET=soh_3ds_old scripts/build-3ds.sh
```

This default-OFF experiment creates the Old audio thread on application core
0 at priority 0x18 and the existing 128 KiB stack size. New hardware retains
its existing core-2/core-1 policy; failed core-0 creation retains the existing
fallbacks. After four consecutive core-0 refill batches it releases the
synthesis mutex and sleeps for 1 ms, so an overloaded high-priority producer
cannot keep the game thread continuously off the CPU. Thread ownership, PCM
kernels and buffer size are unchanged. The pause adds about 1.5% delivery
overhead during continuous refill, outside the synthesis timer. It bounds the
number of batches between opportunities for the game to run, not each batch's
wall time. The canonical build script resets both experimental options OFF
when omitted. APT's reported percentage concerns system core 1, so a value
of 30 in a core-0 capture must not be interpreted as a core-0 execution cap.

Compare sustained synth time, drained refills, audible crackling and game
frame rate on the same route. Core 0 gives audio priority over game work and
can therefore lower game speed; this is an experiment, not a proven fix.
Capture Logging stays ON for this comparison, with detailed/frame/texture
logging OFF. After the capture, switch Logging OFF to check normal listening.

Reproducible analysis and raw evidence live in
`builds/old3ds-audio-crackle-20260908T171535Z/analyze-capture.py` and
`capture-20260908T181234Z/analysis.json` under the same directory.

## Core-0 feedback and filter candidate

The user reports better audio and lower FPS in the core-0 test. Its next
capture confirms core 0 / priority 0x18. The later high-voice windows 13–31
spent 2.904 seconds synthesizing 4.767 seconds of audio (60.9% elapsed/budget),
with 91.0% of that elapsed time in mixing. Queue calls 512–7680 include 57
drained refills out of 7168 (0.80%). The capture ends normally. The route and
note counts differ from the core-1 capture, so these are observations rather
than a controlled speedup/FPS comparison. Logging was still enabled; it has
now been switched OFF through a fresh remote-config read and verified.

Next candidate: `audio_filter.S`, an exact eight-tap FIR using signed dual
16-bit multiply/accumulate into 64 bits (`SMLALD`). Eight maximum positive
products plus rounding can reach `2^33 + 0x4000`, so a 32-bit accumulator
would change the output. The assembly preserves rounding, saturation and
original-input history, and supports halfword-aligned buffers. The C wrapper
keeps coefficient averaging, pointer guards and zero/wrapped-count behavior.

`SOH3DS_ARM11_FILTER_ASM` defaults OFF and the canonical build script resets it
OFF unless requested. The complete production mixer compiles with the option off,
on, and with the existing force-C override; symbol checks verify dispatch.
18433 ARM11 checks cover PCM, history, full production state, alignment,
canaries and callee-saved registers. Removing the rounding term is detected.

Run the standalone benchmark before enabling it in a game package:

```
python3 scripts/build-arm11-filter-benchmark.py
```

It compares extracted production filter wrappers at 8/64/128/544 samples and
two alignments after correctness checks. Both paths use the existing `-O2`
and unroll-loops policy, identical initial data, warm-up, five alternating
timing orders, and two clock reads per timed batch. SD/screen output occurs
outside timing. Valid-pointer fixtures omit real pointer-range checks, so a
measured ratio is fixture-wrapper throughput; shared guard overhead dilutes
the complete in-game wrapper improvement. Kernel timing cannot establish FPS
recovery or total synthesis gains.

The physical Old 3DS run passed all 18433 checks. All 40 timing cases (five
rounds, four sizes, two alignments) favored assembly. Median time reductions:

| Samples | Offset 0 | Offset 1 |
| --- | ---: | ---: |
| 8 | 9.13% | 7.45% |
| 64 | 26.91% | 24.74% |
| 128 | 28.59% | 26.51% |
| 544 | 29.93% | 27.89% |

Offsets are additional halfwords in the benchmark's actual wrapper buffers.
These are reductions in elapsed filter-fixture time, not FPS increases.
Results and the reproducible analyzer are retained in the candidate evidence
directory. Game package `ea9097e0` enables the filter with the same core-0
policy and sampled profiling as the control. No buffer, PCM quality, thread
priority, vertex kernel or other synthesis behavior is changed. Game timing
and listening remain pending; normal build defaults stay OFF for this option.

The canonical build completed with all 299 enhancement initializers retained.
Final disassembly confirms the filter call, existing audio assembly, profiling,
core-0 selection and the four-batch/1 ms fairness pause. The experimental
vertex kernel is absent. The CIA retains main priority 0x30 and CPU descriptor
0x9E. Package hashes and transfer status are in the evidence `game-test/` folder.

The CPU descriptor is a separate next lead: retrieved makerom source writes
`MaxCpu` into descriptor 0, and retrieved Luma PM source interprets 0x9E as
"multi" scheduling with a maximum requested core-1 allowance of 30. Its
resource-limit handler rejects larger requests. This could explain the earlier
30 result; a larger descriptor plus core-1 audio needs a separate controlled
test. The installed Luma version and hardware effect are unverified. See
`research-cpu-limit/FINDINGS.md` in the filter evidence for source snapshots.

The benchmark on SD is `/3ds/soh-arm11-filter-benchmark.3dsx`; its CSV is
`/3ds/soh/arm11-filter-benchmark.csv`. Capture and candidate evidence are in
`builds/old3ds-audio-core0-20260908T181743Z/capture-20260908T184007Z/` and
`builds/old3ds-audio-filter-20260908T184643Z/`.

## Memory reduction and core-1 CPU80 comparison

The next comparison retains the measured ARM11 filter and the memory changes
in control build `b289a13b`: removal of the duplicate statistics display array,
one shared embedded font, and block-based 3DS Settings widget storage. The
control's CIA maps 675,840 fewer bytes (660 KiB) of code/static data than
`ea9097e0`. This is a package measurement; physical heap headroom and Settings
stability still need a run. A recent captured allocation failure reaches
Settings widget vector growth, but its requested size and heap fragmentation
are unknown.

```
SOH3DS_ARM11_FILTER_ASM=ON SOH3DS_OLD_AUDIO_CORE0=OFF \
SOH3DS_OLD_CPU80=ON SOH3DS_AUDIO_PROFILE=ON \
SOH_BUILD_TARGET=soh_3ds_old scripts/build-3ds.sh
```

`SOH3DS_OLD_CPU80` is a default-OFF packaging experiment. It changes only the
Old CIA's `MaxCpu` descriptor from `0x9E` to `0xD0`, retaining bit 7. Retrieved
makerom and Luma PM sources identify the low seven bits as the maximum allowed
core-1 CPU-time request. The existing audio initialization already tries
80, 70, 50, then 30 and checks each APT result. This experiment removes the
package's 30-request ceiling and returns the Old audio producer to core 1,
priority 0x18, with its existing 128 KiB stack. It does not change clock speed.
New hardware keeps its existing core selection. The canonical build script
explicitly resets CPU80 OFF when it is omitted.

Moving synthesis away from core 0 could recover game-thread time while a
larger core-1 allowance could sustain audio. Neither the actual allowance nor
an FPS/audio gain is established until the console runs this package; upstream
Luma behavior may differ from the installed version. Host tests check the real
CMake descriptor generation (default 30, opt-in 80, reset 30), unchanged other
permissions, and the existing successful 80-request initialization path.

Compare against the memory control using the same save, scene, route and
settings for about 60 seconds per build. Open Settings after gameplay to test
its first construction under game memory pressure. Record core/allowance,
audio budget, drained refills, memory headroom and FPS with Logging ON and
frame/detailed/texture tracing OFF. Then repeat the listening/FPS check with
Logging OFF. The two packages vary both core selection and allowed CPU time,
so this comparison tests the combined scheduling policy, not each variable
independently.

Build verification, package fields, transfer hashes and run instructions are in
`builds/old3ds-cpu80-20260908T195107Z/`; preserved source evidence is in its
`research/` directory. The control and memory evidence are in
`builds/old3ds-memory-20260908T193057Z/`.

## Diagnostic build

```
SOH3DS_AUDIO_PROFILE=ON SOH_BUILD_TARGET=soh_3ds_old scripts/build-3ds.sh
```

The option defaults OFF; the canonical build script resets it OFF when omitted.
It compiles probes only into the affected audio source files. Enable Logging;
leave Frame Timing, Detailed Profiling and Texture Tracing OFF. Play the
crackling scene for about 60 seconds, then exit and return to FTP.

The profiler samples every 17th synth batch, recording five disjoint wall-time
stages: initial load processing, command/reset handling, sequencing, mixing,
and the final tail. Sampled frame count defines the reported audio-time budget;
per-operation counts cover those same batches. No per-mixer-operation clock
reads are added. Reports occur after completed PCM is submitted and outside
the timed synthesis. Runtime metadata records the current core, priority and
APT CPU-limit query, including error codes. Normal builds contain no probes.

Interpret these as phase wall times, not pure CPU execution. Command/reset and
sequence stages can also load resources or wait for cache locks; a low initial
load-stage time does not exclude resource delays. Report SD writes and APT IPC
can affect later delivery even though they are outside timed stages. The
profile is for locating work, not proving improved playback/FPS.

## References checked on 8 September 2026

- [libctru thread API](https://github.com/devkitPro/libctru/blob/master/libctru/include/3ds/thread.h): core 1 needs an APT-granted application share; core 2 is New-only.
- [APT CPU-time limit](https://www.3dbrew.org/wiki/APT:SetApplicationCpuTimeLimit): documented range 5–89%, bounded by the exheader; the page reports that values above 30% do not seem to improve Old performance. This is not a measured hard cap for this console.
- [libctru NDSP channel API](https://github.com/devkitPro/libctru/blob/master/libctru/include/3ds/ndsp/channel.h): exposed channel mixing, interpolation, biquad filters, ADPCM coefficients and queued audio waves.
- [Teakra](https://github.com/wwylele/teakra): emulator/assembler tools and architecture research for the 3DS XpertTeak DSP. Its documentation cautions that the exact Teak-family identification is uncertain.
- [Arm ACLE](https://github.com/ARM-software/acle/blob/main/main/acle.md): `__smlald` performs two signed 16-bit products and accumulates both into a 64-bit value; fetched for the FIR candidate.
- [Luma GetThreadInfo hook](https://github.com/LumaTeam/Luma3DS/blob/master/k11_extension/source/svc/GetThreadInfo.c): its extension exposes TLS, not CPU execution accounting. No undocumented runtime counter is assumed here.
- [makerom resource-limit descriptor](https://github.com/3DSGuy/Project_CTR/blob/master/makerom/src/exheader.c): copies the RSF `MaxCpu` value into resource-limit descriptor 0.
- [Luma PM resource limits](https://github.com/LumaTeam/Luma3DS/blob/master/sysmodules/pm/source/reslimit.c): interprets bit 7 and the maximum requested allowance, rejecting requests above that maximum. Runtime behavior on the user's installed version remains to be checked.

Fetched source copies and the raw audio evidence are retained locally in
`builds/old3ds-audio-crackle-20260908T171535Z/`.
