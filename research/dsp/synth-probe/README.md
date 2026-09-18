# Experimental 3DS DSP synthesis

The first gain-mixing kernel executes actual Teak DSP instructions in Teakra.
It is an isolated research probe, not a game backend. A standalone CIA now embeds
a DSP1 component and benchmarks correctness and transfers on the console.
No production audio path has been changed by this probe.

## Reproduce

From the repository root:

```sh
python3 research/dsp/synth-probe/run_probe.py
```

Requires CMake and a C++17 compiler. The runner builds the existing Teakra checkout,
extracts the current production `aMixImplRef` body, builds the probe, and writes
results, instruction binaries/listings and reference provenance to
`builds/dsp-synth-probe-20260909/`.
The host harness is compiled with undefined-behavior sanitization; the separately
built Teakra library is not sanitizer-instrumented.

Verified on 2026-09-09:

- 20,729 individual samples, covering boundary values and random input/gain/output.
- 1,512 buffers / 259,200 samples checked against the extracted production function.
- Lengths 16, 32, 64, 128, 192, 256 and 512 samples.
- Exact alias, forward/backward overlap, and separate buffers.
- Whole 4,096-word test memory checked after every job, including untouched words.
- Consecutive jobs without resetting the DSP between jobs; final pointers and PC checked.
- Both rounded Q15 gain mixing and the special saturated subtraction operation.

The reference adapter maps production byte addresses onto test memory; the function
body is unchanged. The raw kernel tests use reset modes. The standalone firmware
explicitly initializes arithmetic, linear addressing and interrupt modes, and its
252-job test includes dirty entry modes, zero length, invalid lengths, sequence
replies, idle behavior and three boot/shutdown cycles. Raw kernel `.bin` files are not DSP1 files;
`synth-probe.cdc` is the packaged component.

## Standalone hardware test

Build with `python3 research/dsp/synth-probe/build_hardware.py` (devkitPro required).
Install `builds/dsp-synth-probe-20260909/soh-dsp-synth-probe.cia` with FBI, then launch
**DSP Synth Probe**. It has separate title ID `0004000005348D00`, uses 64MB/Legacy
memory settings, and works with the same executable on Old and New 3DS. It uses
the repository's existing HOME Menu banner with a distinct name in the SMDH.
Keep the lid open for the test. It is intentionally silent. Wait for DONE or FAILED,
then press START. Results: `/3ds/soh/dsp-synth-probe.csv` (overwritten on each run).

The CIA explicitly maps DSP memory. The initial 3DSX emulator run failed because
Azahar's default 3DSX permissions omit that mapping; it is retained as failure
evidence, and the 3DSX is not the hardware test deliverable.

The app verifies guards and PCM, runs five alternating-order timing rounds, then
unloads/reloads firmware and repeats. Each timing row measures 128 jobs. `arm_ticks`
is existing ARM11 assembly arithmetic; `dsp_transfer_ticks` includes both buffer
uploads, cache operations, command publication, completion polling and readback;
`dsp_resident_ticks` omits sample transfers but includes publication and polling.
These are latency measurements, not CPU-utilization or full-synth benchmarks.
The ARM kernel accumulates in place; transfer jobs reset destination on each upload.
Both use the same gain and sample count, and the ARM saturation path is branchless.

Command arguments are flushed with busy zero, then busy is published in a second
flush. Firmware writes status and sequence before clearing busy. Shutdown command 0x8000 on
APBP channel 2 receives a fresh reply before firmware parks; an earlier revision
incorrectly relied on the startup reply and hung on emulator unload. The regression
test consumes startup reply before requesting shutdown. Host completion
polling has a 0.5-second timeout; synchronous DSP service load/unload calls do not
have an application-enforced timeout. Sleep cancels the benchmark; suspend/resume
for the eventual audio backend is still unfinished. Physical hardware must establish
unsigned firmware acceptance, cache visibility and load/unload reliability.

Version 1 CIA SHA256: `3727466e363d1a6ee34a8d04ec9fcedcee2916a5ff685d7bc79bd0093d20c50f`.
Azahar LLE runs for Old and New profiles each completed 80 timing rows, two
correctness passes and reload with `failures=0 cancelled=0`. Emulator results
are correctness/lifecycle evidence only. The CIA content is byte-identical to the
tested CXI. Initial mapping and shutdown failures remain in separate evidence
folders. The first Old 3DS hardware run reached firmware startup but failed its first
DSP-service cache flush (`e0e3fff6`) before mixing. No physical timing rows were
produced. The log is preserved in `hardware-20260909T025657Z/` and v1 release
artifacts in `release-v1/`.

Version 2 replaces DSP service cache calls with `svcFlushProcessDataCache` and
`svcInvalidateProcessDataCache` on the current process, matching the upstream
Teakra DSP memory hardware test. Its CIA grants SVCs 0x54 and 0x52, and the build
verifies those bits in the packaged exheader. Hardware validation of this repair
and CPU time savings remain pending.

## Why this needs a new backend

The game currently synthesizes PCM on ARM11 and submits it to NDSP. NDSP already
uses the DSP for playback. Stock NDSP exposes channels, playback, resampling and
mixing controls; it does not expose a way to run arbitrary N64 synthesis commands
alongside its firmware. Loading custom firmware replaces that component.

ARM11 assembly cannot execute on the Teak DSP. Each operation needs a DSP-specific
implementation, together with a replacement output path. Moving just this gain
operation back and forth is unlikely to be useful; batching synthesis commands
and keeping intermediate buffers resident is the intended architecture.

The main acceptance criterion is ARM11 time freed, not just DSP throughput. The
production backend must enqueue batches asynchronously and return to game work,
using completion notification and double buffering instead of this test's busy-poll
loop. Measure CPU submission/completion cost separately from DSP elapsed time.
The standalone test is a prerequisite for that backend, not the offload itself.

## Implementation stages and acceptance checks

1. **Standalone command/transfer probe.** Wrap the validated kernels in DSP1 firmware;
   initialize arithmetic/addressing modes explicitly. Use fixed bounded DSP data
   buffers and a single-producer command mailbox with sequence numbers. Validate
   all counts/offsets before dispatch and in firmware. Handle zero-length jobs on
   the host. Include ready, completion and failure states, bounded host waits and
   orderly unload. Check boot, repeated jobs and reload first in emulation, then
   on Old 3DS. Measure total copy/cache/dispatch/wait/readback cost against the
   existing ARM11 gain kernel, not only DSP instruction count.
2. **Audio output proof.** Establish continuous custom-firmware PCM output and test
   underruns, silence, suspend/resume, reset and shutdown. DSP success at arithmetic
   alone cannot replace NDSP. Stop/unload custom firmware before restoring NDSP;
   CPU fallback is a backend transition, not simultaneous use of both firmwares.
3. **Resident synthesis.** Port ADPCM decoding, resampling and persistent history,
   envelopes, mixing, reverb and filters. Compare complete PCM and state against
   production references across consecutive audio frames, commands and resets.
   Keep assets/state DSP-resident where capacity allows; batch commands per audio
   update to avoid a synchronization/copy for every tiny mixer operation.
4. **Optional game integration.** Retain the current CPU/NDSP path. Select the backend
   at runtime within the same Old/New 3DS build. Preserve the Old 3DS no-frame-
   interpolation policy; synthesis resampling remains necessary for correct audio.
   Validate failure recovery, transitions and teardown before enabling by default.
5. **Hardware decision.** Compare matched gameplay routes for ARM11 audio time,
   frame median/p95, refill misses and memory usage. Target 20 FPS (50 ms/frame)
   on Old 3DS. Enable DSP synthesis only if end-to-end results improve and PCM,
   audio continuity and stability checks pass. No FPS or RAM gain is established
   by the current emulator correctness results.

The existing audio profiling records motivated this investigation, but predate
recent CPU/scheduling changes. They are not measurements of today's remaining cost.

## References

Retrieved source snapshots and URL/hash manifest are in the build output directory.

- [libctru NDSP channel API](https://github.com/devkitPro/libctru/blob/master/libctru/include/3ds/ndsp/channel.h)
- [libctru NDSP initialization](https://github.com/devkitPro/libctru/blob/master/libctru/source/ndsp/ndsp.c)
- [libctru DSP service API](https://github.com/devkitPro/libctru/blob/master/libctru/include/3ds/services/dsp.h)
- [Teakra DSP emulator and tools](https://github.com/wwylele/teakra)
- [Teakra hardware probes](https://github.com/wwylele/teakra/tree/master/hwtest)

## Resident chain progress (2026-09-09)

See [RESIDENT-PROTOCOL.md](RESIDENT-PROTOCOL.md) for the implemented command
interface, fixed data map, count semantics, tests and integration limitations.
The v6 prototype executes decoding, resampling, dry/wet envelopes and filtering
in one resident batch; production CPU/NDSP audio remains unchanged.

Build the expanded standalone probe with:

```sh
python3 research/dsp/synth-probe/build_hardware.py --extended
python3 research/dsp/synth-probe/emulator_smoke.py old --extended
python3 research/dsp/synth-probe/emulator_smoke.py new --extended
```

V6 outputs are isolated in `builds/dsp-synth-probe-20260909/extended-v6/`; the
console result is `/3ds/soh/dsp-chain-probe.txt`. The probe temporarily disables
sleep and HOME during its short DSP test and restores their previous settings
after teardown. It is silent and does not establish continuous audio output.

## A failed start locked the stream out permanently (fixed 2026-09-11)

The hardware run that reported `startFail=4` (`StartFail::Timeline`) also
recorded that every retry failed. That is not a coincidence, and it is provable
without hardware:

- `StreamTimeline::start` rejects unless the state is `Idle` or `Stopped`
  (`stream_timeline.h:19`), and it can enter `Fault` **itself** on its startup
  uncertainty guard (line 23).
- The only exit from `Fault` is `stopped()`, whose only caller is
  `CsndStream::stop()` - which also calls `csndExit()`, clears `initialized_`
  and frees the ring.

So any failed `start()` left the timeline faulted, and every later `start()`
was rejected at its first guard for the rest of the process. A single transient
cache-flush error killed the DSP output path with nothing to recover it short of
a full teardown that the caller had no reason to perform.

`csnd_stream_probe.cpp` never caught it because every failure case it covered
called `stop()` immediately afterwards. The added case reproduces it: open,
fail a start with `flushFail`, then start again - which failed before the fix
(`FAIL a failed start locked out every later start`).

The fix is a private `abortStart(reason)` that all five start failure paths now
return through. It releases whatever channels the attempt configured, confirms
the release through the same `execute` path `stop()` uses, and hands the
timeline back as `Stopped` so a retry can proceed. The class comment's rule is
respected: only a confirmed channel stop resets ownership, and a start that
failed before configuring anything has nothing to confirm. While a command list
is still outstanding its entries may still be owned, so that case deliberately
stays faulted for the owner's `stop()` to drain the same marker.

Verified under ASan/UBSan, and both halves are mutation-checked:

| mutation | result |
|---|---|
| never reset the timeline (old behaviour) | `FAIL a failed start locked out every later start` |
| ignore the outstanding command list | `FAIL aborted start issued commands while a list was outstanding` |

The second mutation initially passed - the existing timeout tests captured
their command count *after* the start, so extra commands issued during the
abort were invisible. The probe now snapshots `stops` before the stalled start.

This does **not** address the separate `outputError=5` (Publish) underrun, which
is a servicing-cadence problem: the output worker must be serviced every <=4 ms.
It does mean that when a start fails for any reason, the path can recover
instead of staying dead for the session.

Azahar cannot exercise any of this - a 3DSX's permissions omit DSP memory
mapping, so CSND is never attempted there (`mode=0`, `tlState=0` = Idle). The
host probe is the proof; `abortStart` is confirmed present in the shipped ELF.

## The publish deadline was a scheduling deadline, not a safety one (2026-09-11)

`outputError=5` (`Publish`) on hardware was the ring's write deadline being
missed. The budgets did not line up with the safety margins at all:

| quantity | value |
|---|---:|
| ring capacity | 8192 frames = **256 ms** |
| guard | 256 frames = **8 ms** |
| `publish` deadline (`push`'s writeBudget) | **1 ms** |
| worker service deadline | **4 ms** |

The transfer itself measured ~208us on hardware, so 1 ms covers the *work*
comfortably - but this thread shares two cores with a game thread whose ticks
run 20-30 ms, so ordinary preemption blew the window and `publish` faulted
permanently. The audio invariant it was protecting has an 8 ms margin.

`push` now passes a 4 ms budget, matching the worker's own service deadline and
staying inside the 8 ms guard. This does **not** weaken the invariant:
`reserve()` proves safety across the whole window, so a longer budget is
*stricter* there - it demands more headroom and yields `Push::Full` sooner - and
`publish()` independently re-checks the pessimistic reader bound against the
written region and its guard.

A mutation experiment confirmed that independence rather than assuming it. With
the budget widened to an effectively unbounded 100 ms, the probe's 5 ms-stall
case **still faults** - `publish`'s window check, not the deadline, is what
rejects a write the reader could have overtaken. The deadline really is a proxy
evaluated at reserve time, and removing its over-tightness costs no safety.

The probe now pins both sides:

| case | expected |
|---|---|
| 2 ms stall (inside budget) | `Push::Accepted` |
| 5 ms stall (beyond budget) | `Push::Fault`, ownership retained |

Mutation-checked: reverting the budget to 1 ms fails with
`a 2ms stall faulted inside the write budget`.

Still not addressed: whether the worker's own 4 ms service cadence is met under
load. That one needs hardware - `outputError` and `maxGap_` report it.

## The worker's 4 ms deadline: measured, not changed (2026-09-11)

`OutputWorker3ds::service` kills the output path when the service gap exceeds
`SYSCLOCK_ARM11/250` (4 ms), which sits in front of a ring holding 256 ms with
an 8 ms guard and in front of the stream's own `healthy()` underrun check. That
looks like the same over-tight scheduling proxy as the publish deadline, and it
was tempting to widen it by the same argument.

Two corrections stopped that.

**The high-water mark is not a latch in practice.** Line 80 tests the gap
*before* the call, line 84 the duration *including* it, and on the first trigger
`run()` breaks the loop and tears down - so `maxGap_` never gets a second chance
to matter. A non-latched `after-lastService_` test would behave identically.
There is nothing to fix there.

**The threshold is a documented posture, not an oversight.** `stream_timeline.h`
states the intent plainly: no software timeline proves uninterrupted audio
during an unbounded stall, so the owner fails closed. Unlike the publish
deadline - where `reserve()`/`publish()` independently re-prove the invariant, as
the 100 ms mutation demonstrated - there is no second check here saying the
audio was fine. Changing the threshold needs to know the gap the real device
sees, and that number did not exist.

So the change made was to produce the number instead of guessing it. `maxGap_`
was measured and then discarded: no accessor, never reported. It is now
published as `maxGapUs` (32-bit atomic, so the reader cannot tear) through
`outputMaxGapUs()` into the `dsp synth:` telemetry line, and the worker probe
requires a deadline fault to report the gap that caused it - mutation-checked by
making the publish a constant 0.

One hardware run now answers it: `maxGapUs` against the 4000 us threshold, with
`outputError` saying whether that is what stops playback.
