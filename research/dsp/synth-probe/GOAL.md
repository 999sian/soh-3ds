# Active goal: free ARM11 time through DSP audio synthesis

User instruction: continue autonomously; avoid repeated questions. Goal is CPU time
freed, with correct stable audio, one Old/New build, and Old 3DS 20 FPS without
frame interpolation. Do not mistake faster individual kernels for the goal.

## Established evidence

- Previous ARM11 optimization work is preserved and remains production baseline.
- v2 DSP gain mixer runs correctly on physical Old 3DS, including unload/reload.
- Physical v2 single-operation protocol is slower than ARM11: at 512 samples,
  gain mix ARM11 30.68 us, DSP resident dispatch/wait 52.17 us, full transfers 207.94 us.
- These times establish latency, not CPU utilization or game FPS.
- v3 firmware batches 1/2/8/32 repeated operations with resident data. 1,764
  firmware jobs pass numerical, bounds, sequence and boot/shutdown tests.
- v3 host splits submission from waiting and runs independent CPU work in between.
  It validates both PCM and work checksum, compares serial versus overlapping
  elapsed time, and records submission/work/finish separately. Generated ARM was
  reviewed to ensure CPU work stays inside both timers after fixing pure-call
  optimization with a volatile seed.

## Current state (2026-09-09, v6)

- v4 CSND one-shot output feasibility probe is uploaded. Physical result file
  `/3ds/soh/dsp-output-probe.txt` is still absent; existing user action remains
  pending. Azahar CSND active-state implementation cannot validate playback.
- Production-oracle ADPCM and stateful resampling kernels are implemented.
- Typed firmware supports a resident queue of up to 32 commands. Decode/copy/
  resample/mix runs as one submission. Bounds, partial-abort behavior, subsequent
  queue/single recovery and the descriptor-end sentinel are tested.
- v5 passes 1,200 sample/state-verified chains per Old/New emulator configuration,
  across separate/queued/queued-plus-CPU modes and two firmware boots.
- Physical v5 is uploaded/readback verified:
  `/cias/soh-dsp-pipeline-probe-v5-39394f2e.cia`.
  It records `/3ds/soh/dsp-pipeline-probe.txt`. This is a silent standalone probe,
  not a game build. Evidence is `builds/dsp-synth-probe-20260909/typed-v5/`.
- Envelope kernel matches production across 2,560 static/runtime-flag jobs,
  including all 32 flag combinations, aliased buffers, volume ramps and guards.
  Review caught output-store ordering; an added regression failed before the fix
  and now passes with production dryL/wetL/dryR/wetR ordering.
- Filter kernel passes 800 production PCM/state jobs with coefficient truncation
  toward zero and preserved original-input history.
- Opt-in v6 typed firmware adds envelope/filter commands. 256 consecutive
  five-stage chains and 54 command count/guard cases pass production oracles.
  Positive envelope count8 rounds to16; zero processes8. This distinction has
  a reproduced regression test. V6 ready4455, 9,982 DSP words.
- V6 hardware build has atomic APT cancellation and temporarily disables sleep
  and HOME during the bounded test, restoring both settings after teardown.
  Full production suspend/resume still requires lifecycle serialization.

## Next work

1. Retrieve v4 physical output result without repeating the pending request.
   CSND output continuity/streaming remains unproven. Do not replace game NDSP.
2. Validate the expanded v6 chain on hardware and add remaining synth commands.
3. Generalize resident scratch layout and single-block decoding into actual
   command-DMEM mapping with per-voice state, validated dependencies and complete
   audio-update queues. Envelopes/filter are in the opt-in v6 dispatcher,
   not the released game backend.
4. Build asynchronous backend and buffered output with bounded failure recovery;
   keep CPU/NDSP fallback. Stop custom DSP before reloading NDSP firmware.
   A partially executed queue needs transactional state/discard handling before
   CPU replay. Do not enable the experimental backend by default.
5. Validate physical audio continuity, ARM11 cost, RAM and matched game frame
   times. Preserve unified Old/New executable and Old 3DS no-frame-interpolation.

Physical v3 validation on Old 3DS passed (hardware-v3-20260909T032006Z).
At 512 samples × 32 resident gain operations with 65,536 synthetic CPU work
iterations, median elapsed fell from 2450.2 to 1495.3 us (39.0%). Short workloads
still lose because DSP completion is on the critical path. Staging/readback are
excluded. This establishes actual overlap, not whole-synth or game speedup.

No full synthesis backend, CPU utilization gain or FPS gain has been demonstrated.

Final v6 CIA uploaded/readback verified: `/cias/soh-dsp-chain-probe-v6-468e4ba2.cia`.
Final Old/New emulator runs each pass 1,536 chain iterations over two boots.
Evidence and next physical action: `builds/dsp-synth-probe-20260909/extended-v6/RESULTS.md`.

## Physical v6 and subsequent work (2026-09-09)

User said FTP up; retrieved `hardware-v6-20260909T044041Z`:
Old 3DS passed all 1,536 five-stage chain checks and both boots. Average resident
chain elapsed: separate 103.65 us, queued 51.15 us (50.65% reduction). Overlap mode
submission 12.7–12.9 us, finish 9.5–9.9 us. These are 16-sample DSP chain measurements,
exclude transfers and do not compare against an ARM-only synth or establish FPS.

V7 `/cias/soh-dsp-output-probe-v7-b553df4d.cia` uploaded/readback verified.
A single new async hardware question asks to install/run it and report the quiet
tone, then start FTP. It supersedes the older v4 output test; do not repeat it.
V7 combines the chain proof with the existing CSND gain-tone output check.
Old/New emulator chains pass; CSND still reports inactive as expected from
Azahar source, so output is NOT verified. V7 console files are
`dsp-chain-output-probe.txt` and `dsp-output-probe.txt`.

Dynamic ADPCM now accepts 0..32 blocks and validates all headers before any
PCM/history writes. 374 production-oracle jobs include 0/1/32 blocks × all flags,
invalid later headers, odd-byte boundaries and recovery. V7 firmware is 7,563 words
versus v6's 9,982. 192 resident 16/160/176/192/256/512-sample chains and 102 additional
count/guard cases pass. Larger chains are Teakra evidence, not yet hardware.

V8 in development: optional DR0 completion IRQ after mailbox publication
(ready signature 0x4457). `resident_session.h` enforces one-in-flight/no-copy ownership,
explicit output acknowledgement, faults requiring stopped recovery, timeout and
late completion handling. Real Teakra tests cover 65,536 sequence-rollover jobs.
`resident_transport_3ds.h` implements sticky events, readiness validation before
RecvData and bounded sleeping waits. It compiles for ARM11; native event harness
build/emulation is underway. Do not claim physical DR0 support yet or upload v8
while the user is testing v7. Production backend integration is still unfinished.

## Native event debugging (2026-09-09 05:15 UTC)

FTP is reachable; v7 result files were absent at 05:11 UTC. Existing v7 action
remains pending; no replacement artifact uploaded.

V8 failure localized with stage diagnostics and Service.DSP trace:
`emulator-v8-old-20260909T051344769072Z` fails `empty-register`, result0;
`emulator-v8-old-20260909T051413372426Z` shows one successful receipt followed
by an empty-readiness check before the next completion. Root cause: kernel wait
timeout `0x09401bfe` is positive Info, incorrectly caught by R_SUCCEEDED before
RD_TIMEOUT. `resident_transport_probe.cpp` with mocked native SVC/IPC reproduces
failure before the fix and passes afterward. Transport now classifies timeout
first and requires zero for a signaled wait. Full rebuild/Old-New emulation
verification underway. Diagnostic stage logging retained in standalone probe.

V8 timeout fix is verified: full arithmetic/session suite and ARM11 build pass;
Old/New native LLE each pass 1,536 chain checks over two boots. Evidence:
`events-v8/RESULTS.md`, emulator runs `051743999408Z` (old) and `051743979017Z`
(new). Whole probe remains FAIL at expected unsupported CSND active-state check;
this is NOT verified playback. Reviewer confirmed timeout classification.
No v8 upload; v7 physical action still pending. FTP directory inventory confirms
v7 CIA present, only older CSV/v6 result logs. Next: real hardware output/event
validation when available, plus per-voice/command-DMEM mapping, complete command
coverage, continuous output and transactional lifecycle/fallback integration.

## Resident DMEM work (2026-09-09)

Added `dmem_layout.h` with production byte-address translation and checked16-byte
rounding. `dmem_transfer_program.h` is real DSP code for arbitrary resident
byte clear/memmove with preflight extent validation, odd-byte preservation,
forward/backward overlap and aligned word fast paths. 1,080 complete-memory
cases pass extracted production clear/move oracles and explicit raw byte cases.
The word path finishes aligned tests within40k Teakra cycles; the byte-only
version failed that budget at1024 bytes. This is not a hardware timing claim.
Review added missing distinct aligned source/destination coverage.

Opt-in fourth `makeTypedFirmware` argument adds ready4458 and queue commands7/8,
without changing existing versions by default. `mapped_queue_probe.cpp` covers
one/32 command batches, partial abort/recovery and completion notification.
Full umbrella validation is in progress (`/tmp/soh-dsp-mapped-suite.log`).
Native component initialization/transport still ends4100, so do not deploy this
extension until both cover5600. No new hardware artifact uploaded. Next genuine
backend work: DSP-side bridges between DMEM and kernel scratch, per-voice state
mapping, complete command lowering, async batching and transactional output/
fallback lifecycle. Hardware v7 output action remains pending.

Validation complete for this step: full run_typed suite passes, including all
existing arithmetic/queue/session tests. New mapped tests pass1,080 transfers
and101 queues; final mixed queue places the real gain/subtraction kernel between
move/clear and checks both PCM and mapped memory. Evidence is
`builds/dsp-synth-probe-20260909/mapped-validation.json`. Reviewer verified prior
v5/v6/v7/v8 generated firmware byte-for-byte unchanged with mappedDmem=false.
Mapped firmware is7,960words. Native integration remains explicitly unfinished.

## Direct resident arithmetic (2026-09-09)

Added immutable16-word parameter records at6000..63ff and command lowerers in
`mapped_parameters.h`; types9/10/11 run gain/envelope/stereo interleave directly
on resident DMEM. No intermediate ARM copies. Independent DSP validation checks
all offsets/extents, normalized counts, parameter slots and reserved flags before
PCM/synthesis scratch writes. Interleave preserves production4L+4R read-before-
store groups with8 scratch words at3d00, including aliased buffers.

607 production-oracle arithmetic/invalid cases and64 gain->envelope->interleave
resident batches pass, including distinct immutable records, all envelope flags,
aliased buffers, odd N64 address rounding and failed lowerer output preservation.
`mapped-arithmetic-validation.json` binds results to source hashes. Full
`run_typed.py` passes (`/tmp/soh-dsp-addressable-suite.log`), and lowerer compiles
with the ARM11 SDK. Regenerated v5/v6/v7/v8 firmware bytes match packaged versions
exactly. Current opt-in v9 is9,132words; no native v9 build/upload yet.

Native memory initialization/mapping/transport must now reach6400, not5600, for
the parameter table. Existing native bound remains4100. Follow
`BACKEND-INTEGRATION.md`: map stateful decode/resample/filter next. In particular,
resampler history alignment is relative to original input and prefix writes need
bounds checks; blindly replacing fixed addresses is wrong. Complete producer
capture, per-voice state, command coverage, transactional CPU/NDSP recovery,
continuous output and CPU/RAM/frame measurements remain unfinished.

FTP was checked again and both v7 output logs remain absent. Existing physical
output question stays pending; no repeat question or new artifact sent.

## Direct stateful resampling (2026-09-09)

Added type12 in opt-in mapped firmware: reads/writes actual resident DMEM and
64 persistent16-word state slots at6400..67ff. Paramrecord0in/1out sampleoffsets,
2stateslot,3pitch,4flags0/1/2; host lowerer normalizes bytecount topositive multiple8.
DSP preflight predicts phase/pitch/count-dependent end, validates prefix/history
writes/reads and output extents, then preserves input-relative history alignment
and alias order. This replaces fixed-input alignment assumptions for this path.

553 production PCM/state/invalid cases plus32 multi-resample batches pass,
including mismatched parameter/state slots, same-state sequential commands,
max1536sample command withpitch0, odd input addresses, allinput alignmentresidues,
saved adjustments/phase65535, exactlastvalid/firstinvalid historybounds and full
memoryguards. Initial invalidfixture input1530 wasactuallyvalid; corrected to1533
and explicitly tested1532 asvalid. Reviewer found no arithmetic/clobber/bounds
blocker and suggested the added slot/count/batch cases. Firmware now9541words.

Full umbrella suite is running (`/tmp/soh-dsp-mapped-resample-suite.log`). Native
memory bound still4100 and now mustreach6800. Production integration and hardware
outputremain unfinished. Next: direct ADPCM state/book/input mapping, then connect
complete actual synthesis chunks, native mapping/output and transactional fallback.

Mapped resample step fully verified: full run_typed suite exits0, including
553 mapped resampler cases and32 batches; `typed-validation.json` source hashes
match. ARM11 host lowerer compiles cleanly. Regenerated v5/v6/v7/v8 firmware bytes
remain identical to their packaged artifacts. No new native hardware artifact.
Goal remains incomplete; proceed with ADPCM mapping and actual backend work.

## Direct mapped ADPCM and complete voice chain (2026-09-09)

Added types13/14 using runtime compressed/output/state/loop/book pointers. Books
16x128 words at5600..5dff, states64x16 at6800..6bff, loops64x16 at6c00..6fff.
Wrapper validates complete extents and rejects compressed/output overlap before
existing header preflight; zero compressed extents still perform history writes.
Default dynamic decoder path remains unchanged; mapped path supports0..95 blocks
subject to actual source/output capacity. Both compression formats use the proven
arithmetic and production-reference limitations remain documented.

Initial304 mapped PCM/state/guard cases passed. Reviewer found no pointer/clobber/
preflight blocker and confirmed defaultv5..v8 byte matches. Added original byte
counts1/31/33, exact adjacency both directions and16 mixed-codec queued batches.
64 decode-resample-envelope-interleave chains across8 persistent voices passed
full memory/state/PCM comparison. No ARM copies between these resident stages.
Mapped firmware is13,329words. Full expanded umbrella suite is running at
`/tmp/soh-dsp-mapped-adpcm-suite.log`.

Native mapping currentlystill4100 mustreach7000 for this layout. No new hardware
artifact uploaded; v7 physical output action remains pending. Full game backend,
other synthesis commands, capture/staging/commit, continuousoutput and fallback
remain unfinished. Multiple voices need compressed payload staging outside the
shared audio DMEM so earlier envelope writes cannot overwrite a later voice's
input; current one-voice chain test does not solve whole-update staging.

Expanded ADPCM step verified: full suite exits0 at
`/tmp/soh-dsp-mapped-adpcm-suite-fixed.log`. 316 mapped decoder cases,16 mixed-codec
batches and64 complete voice chains pass; all other arithmetic/queue/session tests
pass too. Fixed test-runner duplicate-symbol linkage by selecting the appropriate
envelope oracle object only for the new full-chain probe. `typed-validation.json`
hashes match current sources; regenerated v5..v8 binaries match packaged firmware.
Mapped ADPCM lowerer also compiles cleanly for ARM11. FTP was reachable but v7
output logs are still absent. Goal remains active/incomplete.


## Payload staging and whole-chunk composition (2026-09-09)

Implemented immutable7680-byte compressed payload bank at4100..4fff and explicit
ADPCM source region selection (parameter6). Direct DMEM input remains default;
unknown regions reject and payload extents are validated before header checks or
state writes. Mapped firmware now13399words.342 decoder cases and16 mixed-codec
batches pass. Six complete24/28-voice chunks (156 voices,18 bounded batches,
160/176/192 stereo samples) pass full PCM/state/memory comparison with one payload
staging step per chunk. Reviewer found no blocking bounds/ownership defect.

Coverage limitation: fixed416 decoded samples per voice and fresh randomized
payloads prove composition, not actual source-cursor/loop/demand bookkeeping.
Runtime capture/compiler, remaining commands, native mapping/output and
transactional CPU fallback remain unfinished. Added the chunk probe to the full
umbrella runner; current regression log is `/tmp/soh-dsp-payload-suite.log`.
FTP is reachable but both expected v7 output log paths still return450; directory
list attempts return425. No replacement installer uploaded or playback claimed.

Active-mode FTP resolved directory listing errors. Retrieved existing v6 chain,
boot and audio logs to `builds/dsp-synth-probe-20260909/ftp-20260909T061931Z`.
Directory listing confirms no v7 output log. Chain file reports probe=v6,
model=old, all1536 checks PASS; this is not new output-path validation.
Use `FTP.set_pasv(False)` for subsequent transfers on this console.

Payload regression suite completed with exit0. All recorded source/artifact hashes
match; regenerated v5/v6/v7/v8 firmware is byte-identical to packaged versions.
Six complete chunk cases and all prior arithmetic, mapped memory, session and
transport regressions pass. No game integration or new hardware playback claim.
Next implementation work: bounded runtime capture/staging with actual demand and
remaining producer commands, then native mapped transport and output/fallback.


## Mapped add mixer and interl (2026-09-09)

Added type15 saturating addition and type16 every-other-sample copy, with explicit
host lowerers preserving production rounding and do/while zero-count behavior.
Preflight validates counts, slots, flags and both regions before PCM writes.
Added extracted production C oracles,192 randomized alias/count cases plus22
invalid/boundary cases and host lowering/unchanged-on-error checks. Initial test
failed as expected with unsupported type15/status3; implementation passes expanded
arithmetic tests (821 cases and64 resident chains). ARM11 lowerer compile passes.
Replaced skipped-sample load with `modr [r0++]`; full umbrella suite now running
at `/tmp/soh-dsp-add-interl-suite.log` (session52015). Reviewer requested. No native
artifact or production backend changes; physical v7 output action remains pending.

Latest `modr` version passes821 arithmetic cases and64 resident chains during
the umbrella run, plus mapped DMEM/queues and transport. Firmware13613words.
Session52015 remains live; poll it rather than restarting. Full synthesis/session
regressions and source/artifact hash verification still pending, as is reviewer.

Add/Interl full suite completed exit0;59 source/artifact hashes match and v5–v8
firmware is byte-identical. Reviewer found no blocker; Interl input bounds are
intentionally conservative by one unread sample. Proceeding to ZOH resampling.


## Mapped zero-order resampling (2026-09-09)

Added type17 with input/output offsets, unsigned pitch and initial phase, exact
last-sampled-input bounds and full output preflight. Host lowerer preserves byte
rounding/minimum4 output and validates before changing caller-owned commands.
DSP uses full-width position arithmetic and sequential alias semantics; no
persistent state or additional synthesis scratch. Initial168 cases plus prior
arithmetic pass989 total. Expanded boundaries, maximum counts, host lowerer and
64 six-command mixed chains are running (`/tmp/soh-dsp-zoh-expanded.log`,6832).
ARM11 host compile passes. Reviewer requested; no new native build or deployment.

Expanded ZOH arithmetic suite passes1001 cases plus64 six-command mixed chains.
Reviewer found no arithmetic/lifetime/preflight/alias blocker and independently
confirmed unchanged packaged v5–v8 firmware. Full suite now running at
`/tmp/soh-dsp-zoh-suite.log`; poll its live session, do not restart on timeout.
Native mapping/output, remaining commands, actual capture and transactional replay
still required; no CPU/FPS improvement claim from these simulated tests.

Active umbrella process handle:11606. ZOH source is frozen pending full results.

ZOH umbrella suite completed exit0; recorded hashes match and v5–v8 firmware
remains byte-identical. Duplicate kernel now under independent development; not
yet imported into typed dispatcher or covered by this umbrella run.


## Resident snapshot duplication (2026-09-09)

Added mapped type18 and lowerMappedDuplicate, preserving production repeat+1
(including count0->one copy), exact odd byte addresses and snapshot-before-output
alias semantics. Scratch3e00..3e7f is validated before writes. Independent57-case
kernel suite passed, followed by57 resident cases with successive queued snapshots
and host lowerer checks. Firmware13884words; ARM11 lowerer compiles. Reviewer
found no blocker and suggested checking the entire unchanged Command on lowering
failure; assertions now use memcmp. Included duplicate reference/test in umbrella
runner and included generated arithmetic/duplicate oracles in source hashes.
No native deployment or game integration; full expanded regression still required.

Combined duplicate umbrella regression is running at
`/tmp/soh-dsp-duplicate-suite.log`, live session78523. Poll this handle.
Next remaining operations include S8 decode, mapped filter/coefficient lifecycle,
HiLoGain and uncommon commands, followed by bounded actual capture/commit and
native mapped output with transactional CPU/NDSP fallback. Goal remains active.


## Mapped S8 decoding (2026-09-09)

Added type19 and validated S8 kernel with16-sample history/init/loop lifecycle,
unsigned-byte to signed16 high-byte conversion, final state save and sequential
DMEM aliases. Both direct and immutable staged input regions work. Shared ADPCM
preflight has optional S8 mode, preserving default ADPCM code generation; S8
record4 is reserved0, input extent is one byte per decoded sample, and S8 bypasses
ADPCM-only predictor/header/overlap logic. Host lowerer rounds output bytes up32.

Initial test rejected unsupported type19 as expected.113 resident cases now pass
with successive states, zero/max/odd counts, alias mutations, boundaries, invalid
fields and full memory/state guards. Host lowerer comparisons cover success and
unchanged-on-rejection; ARM11 compile passes. Firmware14230words. Reviewer pending.
Duplicate umbrella run78523 finished exit0, but some later compilations included
new S8 changes; do not treat that mixed run as full S8 validation. A fresh umbrella
run will include run_s8.py and the S8 oracle in source hashes. Goal remains active.

Reviewer found no S8 blocker and independently verified default v5–v8 firmware
identity. Added30 explicit decode-output-overwrites-future-input alias cases from
review suggestion. Fresh umbrella is live session29797 at
`/tmp/soh-dsp-s8-suite.log`; S8 test stage has not run yet and will include these.
Poll this session rather than restarting. No native/game integration claim.


## Mapped filter lifecycle (2026-09-09)

Added type20 setup and type21 process,64 persistent16-word histories7000..73ff,
shared coefficients7400..7407 and normalized sample count7408. makeFilter's new
optional mapped mode addresses state through r4 while retaining history scratch;
default code generation remains intended unchanged. Shared coefficient averaging
persists between process commands, including independent states, matching CPU.
Host setup handles uint16 rounded-count wrap; process extent uses resident count.

Initial unsupported-type test rejected as expected.96 setup/two-state chains plus
19 malformed/boundary cases pass, including maximum count, zero/wrapped setup,
all states, signed averaging and full memory guards. ARM11 lowerers compile.
Firmware15867words. Reviewer requested; fresh umbrella must now include mapped
filter script/oracle. S8 umbrella29797 completed exit0 including143 S8 cases; it
is not evidence for the later mapped filter changes. Native mapping must extend
through7408 (aligned7420), but no native artifact or production integration yet.

Mapped-filter reviewer found no blocker and independently confirmed v5–v8
firmware byte identity. Full suite is live session37889 at
`/tmp/soh-dsp-mapped-filter-suite.log`; poll rather than restart. Remaining work:
HiLoGain/uncommon commands and actual bounded capture, native mapping/output,
transactional replay/NDSP fallback and matched hardware CPU/FPS/audio validation.


## Remaining scalar kernels (2026-09-09)

Added mapped HiLo gain(type22) and snapshot table multiplication(type23). Preserve
actual CPU count quirks/minimums, signed saturation and source alias semantics.
Table multiplies clamp full signed products before conversion to avoid DSP40
shift overflow. Scratch3f00..3f1f contains32 table samples per command. Initial
unsupported test failed as expected;298 valid jobs and expanded316 total jobs
with repeated queues and malformed descriptors pass. ARM11 lowerers compile.
Reviewer found no arithmetic/register/preflight blocker. Firmware16143words.

Mapped-filter umbrella37889 completed exit0 at15867words; fresh scalar umbrella
now needed. Production Cmd3 is empty; DMEMMove2 is only an unused macro. Next:
ordered payload loads and bounded real command/resource capture, saves/host
dependencies, native mapping/output and transactional replay with NDSP fallback.
These simulated kernel results do not establish game CPU/FPS/audio performance.

Fresh scalar umbrella active session80189 at `/tmp/soh-dsp-scalar-suite.log`.
Poll this handle; source/artifact hashes and default firmware identity require
verification when it finishes. Goal remains active with production integration
and all hardware output/fallback/performance gates unfinished.


## Ordered payload loads (2026-09-09)

Added type24 exact-byte load from immutable payload to audio DMEM using optional
payload mode in the existing transfer generator. Both banks validate before any
write; clear mode is rejected for this kernel variant. Byte/word source addresses
use payload base while destination remains audio DMEM. Shared constants moved to
dmem_layout.h. lowerMappedLoad leaves caller command unchanged on failure.

Initial unsupported test failed as expected.50 resident load-clear-load cases pass
with odd endpoints, zero/end/max extents, invalid descriptors, exact CPU memcpy
semantics and complete immutable-source/memory guards. ARM11 lowerer compiles.
Firmware16526words; native capacity to verify rather than assume16K-word limit.
Scalar umbrella80189 completed exit0 at16143words. Reviewer requested for payload
mode. Fresh umbrella includes run_payload_load.py; actual capture/staging/saves,
native mapping/output, transactional fallback and performance proof remain ahead.

Payload-load reviewer found no blocker. Local Teakra processor_general.md
documents3DS program space0x20000words, supported by hardware tester program/data
mapping separation;16526words is not a known16K limit. This is source evidence,
not new hardware validation. Full regression live session48207 at
`/tmp/soh-dsp-payload-load-suite.log`; poll it rather than restarting. Next concrete
step is bounded chunk command/payload/save capture and completion ownership.


## Bounded capture ownership (2026-09-09)

Payload-load umbrella48207 finished exit0; recorded source hashes matched before
adding capture to the runner, and regenerated v5–v8 byte identity passes. No live
umbrella process remains. Added chunk_capture.h with192 entries,7680 captured
source bytes and4096 tentative save bytes. Immutable command params/load snapshots,
ordered overlapping save overlays, explicit save barriers,32-command batch limit,
deferred commit, stop-before-reset fault ownership and fixed storage caps pass
ASan/UBSan + Werror in run_capture.py.24280 bytes host storage; ARM11 header build
passes its24KiB cap. Evidence: builds/dsp-synth-probe-20260909/capture-validation.json.

Reviewer found no blocker in component scope. Runner must remap repeated parameter
slots, stage resolved loads, use Session-verified completion, collect saves/state
before commit and retain actual CPU replay. No voice-state transaction or game
hooks implemented by this class. Next concrete step: asynchronous captured-chunk
runner using Session/Transport, DSP execution equivalence and injected failures,
then production frontend/resource identity and native/hardware integration.


## Asynchronous captured-chunk runner (2026-09-09)

`chunk_runner.h` executes captured commands through Session without spinning. Each
step stages at most32 commands or collects one save barrier. Parameter records
are remapped to distinct batch slots. Adjacent odd payload loads are resolved in
private scratch, invalidated once before writes, then flushed before submission.
Completion must pass Session validation and acknowledgement before collecting
saves. Host output remains private until the lifecycle owner also validates and
collects persistent voice state and explicitly commits.

The real Teakra firmware runner passes ASan/UBSan CPU PCM comparison, overlapping
save/load dependencies, repeated parameter slots, batch splits, five flush fault
positions, payload/completion invalidation errors, receive errors, timeout, save
readback error and DSP rejection after a tentative save. A call-order assertion
rejects payload invalidation with unflushed writes; this is not a physical cache
model. Review found no blocker and its requested fault/order checks now pass.
Runner storage is7992 bytes on the host; every method also compiles instantiated
with CtrTransport for ARM11 under an8KiB cap. Evidence:
`builds/dsp-synth-probe-20260909/chunk-runner-validation.json`.

Standalone `run_chunk_runner.py` refreshes the scalar CPU oracle. The umbrella
now runs it with `--reuse-scalar` after its own scalar refresh. Targeted rerun
`/tmp/soh-dsp-chunk-runner-faults.log` finished exit0; no test remains running.
The umbrella was not rerun for this runner-only addition.

Remaining: persistent state/resource identity transaction, complete CPU replay
capture, production entry hooks, native mapped bounds/load/output and recovery,
and matched physical CPU/RAM/FPS/audio measurements. Native CtrTransport still
ends at0x4100; ARM11 compilation does not establish native mapped execution.
FTP192.168.1.48:5000 returned “No route to host” during this continuation.
The previously uploaded v7 installer remains unchanged; no new hardware result.


## Chunk-local persistent-state transaction (2026-09-09)

`state_transaction.h` maps64 records per resample/ADPCM/filter/loop bank and16
complete predictor books. Mutable pointer identities reuse slots; partial and
cross-bank aliases reject before submission. Rebinding a producer-mutated state
also rejects. Books deduplicate immutable complete-table snapshots by content,
so the frontend must reconstruct partial aLoadADPCM updates before binding.
Sealing rejects captured loads/saves overlapping mutable state, and saves that
would mutate loop sources. These cases require CPU fallback, not skipped commands.

State is staged before runner execution. Only after the captured chunk completes
and Session returns Ready can all mutable banks be invalidated/read privately.
Commit publishes ready saves and host histories once; host ranges are exclusive
and state/save aliases were rejected. Host baseline remains untouched on failure.
The staged image is reused for readback, avoiding a redundant8KiB copy. Fixed
host size16424 bytes; native ARM11 stage/collect instantiations pass with a17KiB
cap. Combined capture+runner+transaction is48696 host bytes, so integration must
use persistent worker-owned storage rather than the audio thread's small stack.

`run_state_transaction.py` passes ASan/UBSan: bank capacity/identity/partial and
cross-bank aliases, mutable-state load/save rejection, book snapshots/dedup,
four persistent filter chunks and four ADPCM/resample chunks (init/continue/loop)
against extracted production PCM/history, immutable loop/books, staging failure,
later-bank readback failure and actual firmware-stop/reset. The ADPCM extracted
oracle retains its documented signed-shift portability limitation. The expanded
fixture initially omitted the ADPCM power table at2800; adding that required boot
initialization resolved the CPU mismatch. Native bootstrap must upload/flush that
table and the resample coefficients at4000 before execution.

Final targeted test84075 finished exit0 at
`/tmp/soh-dsp-state-transaction-final.log`; no live test remains. Evidence:
`builds/dsp-synth-probe-20260909/state-transaction-validation.json`.
The full standalone refreshed filter/resample/ADPCM oracles; final targeted rerun
reused them. Umbrella now calls `--reuse-oracles` after its own fresh oracle runs;
no full umbrella rerun was needed for this host-only component addition.

Reviewer found no blocker in the transaction's stated scope. Requested active
ADPCM/resampler mutation, later-bank readback failure and loop/book immutability
checks now pass. Still required: coordinator must abort Session, capture and
transaction together on failure and stop firmware before reclaim/reset. Direct
capture.commitSaves would bypass transaction ownership and is not an integration
path. Global mixer state (especially filter coefficients/count and DMEM) is not
covered by these pointer banks and must be reconciled for CPU replay. No original
command replay stream, producer interception, native mapped output or physical
CPU/RAM/FPS improvement has been established. Goal remains active.

Next concrete step: retain original implementation-entry calls and mixer-global
baseline for ordered CPU replay, preserving load snapshots/save dependencies.
Capture overflow should replay the captured prefix and execute the remaining
producer calls on CPU without rerunning the producer's already-mutated scheduling
state. Runtime DSP failure requires stop before the same replay. Only then wire
transactional capture into AudioSynth_DoOneAudioUpdate and native playback.


## Original-call CPU replay (2026-09-09)

`cpu_replay.h` retains384 original implementation-entry calls and a4096-byte full
mixer-global baseline, with12288 source snapshot bytes and3072 replay scratch.
`cpu_replay_dispatch.h` dispatches all23 non-OPUS entry types with their original
argument semantics, before DSP lowering or count rounding. Load, partial book
and filter coefficient sources are captured. Replay overlays bytes written by
preceding CPU saves/history updates, preserving source snapshots elsewhere.
Mutable histories and loop pointers must remain live/exclusively owned and
unchanged by the producer during recording and DSP ownership. The future frontend
must enforce this contract or fall back before ownership is transferred.

Refused calls do not enter the journal. Capacity/unsupported fallback replays the
retained prefix and executes the current call plus remaining producer calls live
on CPU, avoiding a second pass through mutated note scheduling. OPUS deliberately
has no journal opcode: prefix replay must happen before the live OPUS call.
A DSP-owned journal cannot replay until the lifecycle owner confirms actual stop.
Successful DSP commit consumes the journal without CPU replay. Copy/move are
deleted; once CPU execution starts it cannot replay twice. The restore adapter
must validate before mutation when returning false. Unsigned argument widths are
validated before read/write dependency classification (Filter256 must not become
process0 at dispatch; Save65536 must not advertise writes it will not perform).

`run_cpu_replay.py` extracts current scalar production mixer bodies and complete
rspa memcpy snapshot/restore adapters for testing; OPUS and SIMD bodies are omitted.
It compares dispatcher replay against independent named production calls for all23
operations, including full DMEM/book/env/filter/buffer/loop globals, state updates,
source mutation after capture, partial books, state-to-load/filter dependencies,
overlapping saves, capacity prefix/suffix order, restore failure, stop gating and
exactly-once behavior. ASan/UBSan pass; the production oracle preserves signed
ADPCM shifts with only the shift sanitizer category disabled. ARM11 compile passes
under32KiB journal storage;64-bit host size34832 bytes (35KiB cap). Combined host
capture+runner+state+journal storage is83528 bytes; use persistent worker-owned
storage, not an audio thread stack. These allocations are research components,
not a measured increase/decrease in game RAM.

Final test62864 finished exit0 at `/tmp/soh-dsp-cpu-replay-final.log`; no test is
running. Evidence: `builds/dsp-synth-probe-20260909/cpu-replay-validation.json`.
Review identified copy ownership and narrowing/dependency hazards; both fixed,
with independent-dispatch expectation tests and malformed-width coverage added.
Umbrella now runs this host-only suite; no full firmware rerun for the journal.

Still absent: production snapshot hooks, actual implementation-entry interception,
metadata-to-mapped-command frontend, source/state ownership enforcement, shared
failure coordinator, native mapped initialization/output and NDSP recovery. Next:
a frontend that journals original calls first, lowers them using captured buffer,
envelope/book/loop/filter metadata, and switches to prefix CPU replay on refusal.
It must reconcile final DSP filter coefficients/count and DMEM with CPU globals
on success, and disable interception during replay. Then wire chunk begin/end at
AudioSynth_DoOneAudioUpdate and validate actual producer streams. Hardware gates
and the full CPU-offload goal remain open.


## Original-call frontend and coordinated replay (2026-09-09)

`chunk_frontend.h` now composes CpuReplay, ChunkCapture, StateTransaction and
ChunkRunner. It journals original calls before lowering, retains buffer/env/book/
loop/filter metadata, stages the initial full CPU DMEM and filter globals, executes
queued DSP batches, and reconciles final DSP DMEM/filter coefficient/count with
final host metadata on success. Complete history/save collection and Cpu::canApply
validation precede publication. Cpu::apply must then be infallible. All component
failures cancel Session and fail capture/state; CPU replay remains gated on actual
firmware stop and Session reset. OPUS entry must flushToCpu before running live.

Unjournaled refusal executes retained prefix then current call once; lowering
refusal replays the already-journaled current call as part of the prefix. Metadata
sources overlapping prior saves or mutable DSP histories force CPU fallback rather
than reading stale host memory. Ordinary saved-buffer load dependencies still run
through DSP. State-to-load aliases reject at sealing. Empty/metadata-only chunks
and successive DSP/CPU/DSP chunks preserve mixer-global state, including filters
continued without a new setup. Original flag/count quirks use existing lowerers.

`mixer_image.h` mirrors the private rspa image without defining a file/DSP ABI.
The extracted production C fixture now statically verifies its size and every
field offset. `run_chunk_frontend.py` passes ASan/UBSan full-image/PCM/history
comparison with production scalar mixer calls: ADPCM/loop, resampling, S8 and
mixed filtering/envelope/etc in a fully offloaded chunk with zero CPU kernel
calls; four stage/timeout/final-readback/apply-validation failures replay after
actual Teakra shutdown; five metadata/state-alias/capacity/unsupported-entry
barriers fall back to CPU; six successive empty/metadata/filter-continuation
chunks exercise success-to-CPU-to-DSP lifecycle. Oracle retains its documented
production signed-shift sanitizer exception. ARM11 instantiation passes Werror.

Initial test44086 passed numerical tests but ARM11 Werror caught misleading book
branch indentation; fixed. Expanded final56813 completed exit0 at
`/tmp/soh-dsp-chunk-frontend-full.log`; no test remains live. Evidence:
`builds/dsp-synth-probe-20260909/chunk-frontend-validation.json`.
Frontend including runner is11440 host bytes; combined capture/state/replay/frontend
fixed host storage86976 bytes. Place it in worker-owned heap/static storage.
Umbrella now includes the targeted frontend suite after fresh CPU fixture creation;
no full firmware rebuild/regression was needed for this host component addition.
Reviewer found no blocker; requested metadata/empty, repeated lifecycle, early
staging fault, metadata dependency and filter continuation checks are covered.

Production interception is STILL NOT wired. Next concrete work: actual optional
mixer snapshot/restore and implementation-entry hooks plus begin/end at
AudioSynth_DoOneAudioUpdate, with recursion bypass during CPU replay. Preserve
existing 3DS invalid-pointer semantics: validate host memory before journal or
state capture dereferences; on bad pointers flush to CPU and let original guarded
bodies handle the command. This includes unused/zero-length pointer cases where
existing guard behavior may still zero state. SetLoop metadata needs validation
before a later decoder dereferences it. Do not execute untracked guard side effects
inside a recording chunk. Native mapped bounds/tables, custom output, NDSP/suspend
recovery and physical CPU/RAM/FPS/audio evidence all remain required. Goal active.


## Opt-in production mixer hooks (2026-09-09)

Production mixer.c now has23 opt-in implementation-entry hooks and barriers before
OPUS decode/free. AudioSynth_DoOneAudioUpdate brackets the full chunk with begin/
end callbacks. SOH3DS_DSP_CAPTURE is OFF by default; CMake defines it and adds the
research include path only for mixer.c/audio_synthesis.c, and build-3ds.sh exposes
the option. No native runtime callback installer or output backend is linked yet.
With the option OFF, the hook statements disappear at preprocessing; existing
per-kernel oracle extraction remains compilable without any hook definitions.

`dsp_mixer_hooks.h` shares the original-call enum with CpuReplay and declares the
single-worker callback contract. `dsp_mixer_hooks_impl.h` lives inside mixer.c,
checks actual sizeof(rspa) and every field offset, exports image read/write and
resample table access, and controls install/chunk/bypass ownership. CPU replay
uses a saved/restored bypass state. Installing/replacing hooks during active
capture or bypass is rejected. Callbacks must not throw; end/barrier must finish
ownership/fallback before returning. This is a bridge contract, not native DSP
lifecycle implementation.

Host pointer guards run BEFORE capture snapshots. Rejected pointers flush the
recorded prefix, then the original guarded CPU body performs its existing zero/
skip behavior. Review caught initial invalid loop pointers that predated capture;
ADPCM/S8 LOOP-without-INIT now guard the baseline loop before recording, falling
back conservatively even if a captured valid SetLoop supersedes it. SetLoop's own
capture guard still validates later metadata. No untracked guard side effects
run while the frontend remains Recording.

`run_mixer_hooks.py` passed the actual production C hook bodies with ASan/UBSan
and real Teakra frontend callbacks: supported C calls defer their CPU bodies and
offload, callback record0 preserves live-suffix ordering, initial invalid loop0x10
and null source/zero-byte-null-book calls preserve CPU guard behavior, OPUS decode/
free barriers precede live execution, transfer failure stops firmware before
replay, and nested bypass/install/uninstalled lifecycle checks pass. OPUS uses a
small ordering stub, not codec/audio-quality validation. Host guard simulates
rejected ranges; native guard code is retained unchanged. Original ADPCM signed
shifts retain the documented sanitizer exception.

The unmodified production include/define flags from build-3ds-mk were used to
compile actual mixer.c and audio_synthesis.c ARM11 objects with hooks ON and OFF
into evidence files; game objects/cache were not replaced. ON has hook symbols;
OFF contains none. Targeted test16128 finished exit0, log
`/tmp/soh-dsp-mixer-hooks.log`, evidence
`builds/dsp-synth-probe-20260909/mixer-hooks-validation.json`.
Earlier frontend baseline39218 also passed after hook insertion with option OFF.

## Native mapped bootstrap and package (2026-09-09)

Full production-hook umbrella session70740 completed exit0. Log:
`/tmp/soh-dsp-production-hooks-suite.log`. The 104 source/artifact hashes were
verified in `production-hooks-suite-audit.json` before the native transport
extension below. This earlier aggregate manifest does not attest to that later
extension. Legacy v5–v8 firmware regenerated byte-identically.

`mapped_bootstrap.h` validates the service-converted DSP words0 and741f as a
32-byte-aligned contiguous mapping within 0x1ff40000..0x1ff80000. The required
extent is 0x7420 words. Bootstrap checks signature4458 and idle state, then uploads
ADPCM powers and resample coefficients with cache maintenance. Failures fault the
Session until actual firmware stop/reset.

`resident_transport_3ds.h` now accepts explicit legacy0x4100 or mapped0x7420 spans;
cache accesses obey the selected span and close/reopen clears expanded access.
`run_mapped_bootstrap.py` passed ASan/UBSan/Werror. `run_native_transport.py` passed
mock IPC/cache/mapping checks and real SDK ARM11 compilation. Evidence:
`mapped-bootstrap-validation.json` and `native-transport-validation.json` under
`builds/dsp-synth-probe-20260909`. Both scripts are wired into `run_typed.py`.

`build_mapped_firmware.py` packages v9 signature4458: 16526 program words,
29728 data words, 93276 total component bytes. Artifacts are in
`builds/dsp-synth-probe-20260909/mapped-v9/`. Packaging reparses the written DSP1
file and validates segment content and hashes. All 28 compiler-discovered
dependency/artifact hashes matched after the final successful build.
No native v9 executable, installer, loader acceptance, or upload is claimed.
No test process remains live.

Next: build a distinct native v9 correctness probe using validated service
mapping, mapped transport, bootstrap tables, actual frontend/hooks and sleeping
DR0 waits. Exercise two boots and stop/unload before reclaim/reset. Allocate the
86976-byte host context in heap/static storage. Then implement the native callback
owner and serialized custom streaming output/NDSP recovery, including suspend and
resume. Actual SoH output is libultraship NdspAudioPlayer.cpp; the platform Mk64
adapter is separate. Custom firmware cannot coexist with NDSP firmware.

Latest FTP retry at192.168.1.48:5000 returned No route to host. The previously
verified v7 installer is unchanged; physical output logs remain outstanding.
The goal remains active: actual game offload and matched physical CPU/RAM/FPS/audio
validation are still required. One Old/New build and Old3DS20FPS/no-frame-
interpolation policy remain constraints. No hardware performance gain is claimed.


## Native v9 correctness and recovery probe (2026-09-09)

`mapped_hardware_probe.cpp`, `mapped_probe_cpu.h/.cpp`, and
`build_mapped_hardware.py` now build an isolated native v9 probe with actual
production mixer.c entry hooks. The C++ CPU adapter is a separate translation
unit to avoid conflicting libctru/libultra integer typedefs. The CPU oracle uses
production scalar kernels with ARM11 assembly disabled; this is a correctness
reference, not the current optimized game's CPU performance baseline.

Native boot uses DSP_LoadComponent followed by bounded cache-invalidated
signature4458/idle polling. The DSP service consumes the DR2 boot handshake;
a second direct DR2 receive would hang. Service-converted endpoints validate
word0..741f before mapped transport attach and lookup-table initialization.
The context (90944 bytes including fixtures) lives on the heap. Normal completion
sleeps on DR0 events; the standalone probe disables HOME/sleep temporarily.
This does not implement production suspend/resume.

Each of two boots compares16 successive DSP chunks against direct CPU execution,
carrying decoder, resampler, S8, filter histories and full mixer DMEM/globals
forward. CPU and DSP begin each comparison from identical saved state. Every DSP
success requires exact full-image/history/PCM equality and zero CPU replay calls.
Filter coefficients are explicitly reloaded each chunk. A17th comparison cancels
only after a real batch submission, unloads before Session reset/event closure,
and requires successful CPU replay with exact output/history equality. This
proves submitted-batch recovery, not guaranteed partial DSP execution before
cancellation. Allocation is retained if unload cannot confirm stop.

Final native build4004 finished exit0. Old/New LLE sessions28943/88345 both
finished exit0:34 comparisons and2 successful recoveries per model across2 boots.
Final evidence:
- `builds/dsp-synth-probe-20260909/mapped-v9/native-validation.json`
- `builds/dsp-synth-probe-20260909/emulator-v9-old-20260909T165315327243Z/verification.json`
- `builds/dsp-synth-probe-20260909/emulator-v9-new-20260909T165315507927Z/verification.json`

The distinct installer is `mapped-v9/soh-dsp-mapped-probe.cia`, title
0004000005348e00, SHA256
933cf9d746fdfec4cce1b20e3a78ad95cd6792294ac5ed46c8f623699057f58c.
Packaging verifies CIA content equals CXI, title ID, cache SVC82/84 and adjacent
DSP IO mapping descriptorsff81ff00/ff81ff80. All127 source/artifact hashes in
hardware-manifest.json were checked after the final build. Actual native compiler
depfiles include production mixer/header dependencies. Reviewer found no remaining
blocker. No tests remain live. No v9 upload: FTP retry returned No route to host.
The existing v7 physical output installer remains unchanged.

Next required work: production callback owner with custom streaming output,
serialized NDSP fallback and suspend/resume, then actual game capture/offload and
physical matched CPU/RAM/frame-time/audio validation. The v9 probe has no audio
output, and emulator elapsed ticks do not establish physical CPU savings. Keep
the goal active; preserve one Old/New game build and Old3DS20FPS/no-interpolation.


## Experimental continuous CSND output ownership (2026-09-09)

Added `stream_timeline.h` and `csnd_stream_3ds.h` as the next native output
component. Neither is installed in the game or included in the v9 installer.
Online libctru csnd.c and installed SDK establish that public CSND channel info
has activity but no PCM cursor. The fetched current blargSnes audio source uses
NDSP and does not validate a CSND ring. Sources/hashes are recorded under
`builds/dsp-synth-probe-20260909/output-research/sources.json`; 3dbrew requests
returned HTTP403. No undocumented cursor field is used.

The timeline models an interval between command submission and completion,
using4*CSND_TIMER(rate) base system ticks per sample (same clock source on both
models). Absolute-frame admission guards the fastest possible reader through the
write deadline and the slowest possible reader before ring wrap. It rejects late
publication, clock reversal, underrun and excessive startup uncertainty. Full
rings return backpressure. Restart requires owner-confirmed stop.

The native adapter allocates32768 bytes for an8192-frame stereo planar ring,
claims two channels, starts normal-loop PCM16, deinterleaves incoming PCM and
flushes whole cache lines. It requires a single serialized owner for ALL libctru
CSND calls; a class-wide owner prevents two instances opening together, but cannot
control unrelated code calling libctru directly. An owner must service it at
least every4ms and stop before switching output to NDSP. The adapter does not
create that worker or implement the NDSP switch yet.

Command execution uses the first command header+4 completion byte with bounded
sleeping polls; no csndExecCmds(true) busy wait. Timeout/uncertain IPC retains the
same marker and DMA-visible PCM. Stop must drain that marker before queuing stop,
and confirm the stop before releasing channels/freeing memory. A late stop
acknowledgement is handled without issuing duplicate stop commands. Failed owner
objects must themselves remain alive while storage/commands are retained.

Critical physical gate: CSND has no observed hardware cursor, so this remains an
experimental clock model. Deadline checks detect but cannot undo a copy delayed
past its safe window by preemption. Clock/start/prefetch behavior, stereo start
skew, scheduling stalls and underflow behavior require physical validation before
game use. No stable streaming playback or CPU/RAM/FPS improvement is claimed.

`run_csnd_stream.py` completed exit0 (session94849): ASan/UBSan/Werror tests cover
10000 timeline publications/ring wraps, cursor extremes, backpressure, overflow,
clock/deadline/underrun faults, stereo plane data and cache alignment, exclusive
instances, startup/stop timeouts and late acknowledgement, allocation/init/flush
failures, and retained memory until stop. Real SDK ARM11 methods compile with
Werror. Evidence: `builds/dsp-synth-probe-20260909/csnd-stream-validation.json`.
Reviewer found no arithmetic/lifecycle blocker for the stated bounded-stall
experimental model and highlighted the whole-CSND exclusive ownership contract,
now explicit. Umbrella run_typed.py invokes this targeted suite; no full rerun
solely for this independent addition. No live test process remains.

Next: add a distinct native streaming-output probe while mapped synthesis runs,
with active-channel observations and output/underrun telemetry. Do not overwrite
v7 or reinterpret Azahar's known CSND inactive limitation as playback success.
Then production callback/output worker, serialized NDSP/suspend recovery, real
producer traces and physical matched performance/audio validation remain required.
Goal remains active.


## Native v10 streaming probe (2026-09-09)

Added `stream_hardware_probe.cpp` and StreamProbeFixture in the native CPU adapter.
Shared unchanged v9 context/boot code moved to `mapped_native_context.h`; v9 main
now includes it. `build_mapped_hardware.py --stream` creates a distinct v10 output
folder/title5348f. Without the option it retains v9 behavior/title5348e. Native
build manifests now hash actual compiler-discovered local dependencies plus build
inputs/artifacts instead of unrelated research source globs.

Each streaming block runs actual production resample/filter512-sample calls on
CPU, saves expected full mixer/history/PCM, restores the exact baseline, then
executes through the DSP hooks and sleeping transport. Publication requires exact
equality and zero CPU replay calls. The generated source is a quiet triangle;
this tests synth kernels/output coexistence, not a full game music sequence.
Eight blocks prime4096 stereo frames; continuation histories carry forward.
The prefill must be non-silent (observed peak2028). Then the probe aims for4seconds
of refills per boot, services before/after rendering with2ms sleeps, and rejects
service gaps above4ms, including the duration of the final CSND service call.
A two-boot run is required for native streaming success.

Successful startup/counter SD logging is deferred until CSND stops. Timing reports
include min-buffered-known, DSP-only hook elapsed/max-block ticks and maximum
service gap. Oracle CPU time is excluded from DSP-only timing but included in
service-gap observation; this is not a game performance benchmark. Unknown queue
depth reports known0/value0. Stop confirms CSND teardown before freeing its ring,
then unloads DSP; the whole context is retained if either cannot confirm stop.
CsndStream now exposes whether channel activity was actually observed and its
left/right mask, so emulator failures can be classified narrowly.

Final streaming build11029 completed exit0. Installer:
`builds/dsp-synth-probe-20260909/stream-v10/soh-dsp-stream-probe.cia`
SHA256 d8f0dacf7c4e11c6e572eebc75590fdf9047f9b8f7b705b46007949473b8be9a.
Title0004000005348f00,64MB/Legacy mode, DSP IO mapping and cacheSVC82/84 were verified
in the packaged CIA/CXI. All46 native source/build/artifact hashes matched.
No upload occurred; FTP retry returned No route to host. Existing v7 on console
was not overwritten or replaced with a repeated test request.

Final Old/New streaming LLE sessions66286/27393 completed exit0 as an explicitly
recognized unsupported-output observation, NOT playback success. Each verified
8 DSP prefill blocks, successful CSND activity query with both channels inactive,
and successful CSND/DSP teardown with no retained memory. App log says
finished=FAIL; evidence says expected_csnd_limitation=true, streaming_verified=false.
No active streaming loop or second streaming boot ran in Azahar.
Evidence: `builds/dsp-synth-probe-20260909/stream-v10/native-validation.json`,
with referenced final emulator-v10-old-20260909T171248655924Z and
emulator-v10-new-20260909T171248678635Z verification.json files.

The refactored v9 regression passed both models again (sessions91418/81248):
32 DSP chunks and2 recoveries each. CXI content hash remained identical to the
previous v9 package; regenerated CIA hash is
88f56fd8004509bf6291fe59501c2ac289ad836bdcba62771cb443b112e8f324.
Its44 actual dependency/artifact hashes passed. Updated native-validation.json
records the current package and final v9 runs. Updated CSND adapter tests56859
also passed ASan/UBSan and native Werror compilation. Reviewer found no remaining
blocker after the final service-duration check. No test process remains live.

Next: production callback owner/output worker and serialized CPU/NDSP/suspend
recovery, retaining opt-in isolation while physical CSND clock/prefetch/continuous
playback remains unverified. The v10 installer is ready for transfer when FTP is
reachable. Actual game capture capacity/coverage, matched hardware CPU/RAM/frame
measurements and stable game audio are still required. Goal remains active;
Old3DS20FPS/no-frame-interpolation and one Old/New game build remain constraints.


## Producer/lifecycle owner and sticky native unload (2026-09-09)

Added `mixer_owner.h`: reusable owner over the actual ChunkFrontend and mixer C
callbacks. It holds the lifecycle lock from begin through end, including recording,
CPU barriers, DSP execution and any replay. It starts in existing CPU/NDSP mode;
custom enable stops NDSP, loads/attaches DSP and starts custom output. Runtime
faults confirm DSP unload before CPU replay, then confirm custom-output stop before
NDSP initialization. Unsupported-command barriers keep Custom mode for future DSP
chunks. Failed NDSP initialization requires confirmed cleanup before Silent mode;
Silent can later retry CPU output or custom enable without overlapping resources.

Control methods support idle disable, suspend/resume and terminal shutdown.
Shutdown stops resources without restarting NDSP and enters Closed; late producer
entry or uncertain resource ownership invokes a nonreturning fatal callback.
This is not a permission request or an ordinary fallback: returning with a failed
chunk whose DSP writes cannot be stopped would violate sample/history ownership.
Native fatal/recovery service implementation remains to be supplied.

Lifecycle callbacks must not re-enter owner methods/getters. The separately
joined output worker must never acquire the lifecycle lock, since stopOutput is
called while that lock is held; it needs a separate queue mutex/event. Native
sleep integration is NOT solved merely by this lock: libctru aptDspSleep calls
DSP hooks then automatically unloads, and aptDspWakeup automatically reloads the
saved component before wake hooks. Native lifecycle must coordinate this behavior
with the game's APT hook order rather than assuming the owner alone controls DSP.
The native NDSP stop port must establish actual stop, not just a cached SDK flag.

`run_mixer_owner.py` reuses hash-verified packaged v9 firmware, regenerating its
package if missing/stale. It uses actual production C hooks with Teakra and mocked
output/service lifetimes. Final session59137 completed exit0; source/evidence49
hashes matched. ASan/UBSan/Werror coverage includes exact PCM/mixer/history checks,
CPU barriers then suspend/resume, state transfer and submitted-wait failures,
timeout, cancellation/output loss during submitted work, timeout reaching zero
between poll/wait, failed custom restart on resume, partial DSP/output/NDSP init,
Silent-to-custom restart after NDSP cleanup, real host mutex exclusion of suspend
until the producer chunk ends, CPU/custom terminal shutdown, and subprocess fatal
paths for unconfirmed stop. Original CPU ADPCM signed-shift sanitizer exception
remains. Actual owner template methods compile against ARM11 SDK with Werror.
Evidence: `builds/dsp-synth-probe-20260909/mixer-owner-validation.json` and
`/tmp/soh-dsp-mixer-owner.log`. Initial host generator Werror warnings were avoided
by using the already-verified firmware artifact; Cpu template-name shadowing was
fixed. Reviewer found no remaining blocker in owner/shutdown/stop-guard logic.

A separate native safety bug was found through online libctru source AND installed
library disassembly: DSP_UnloadComponent clears its loaded flag BEFORE IPC success.
After failure, a second call can return0 without IPC. This could turn a failed
callback stop followed by the probe's outer cleanup into false stop confirmation.
Added `component_stop_guard.h`; mapped_native_context.h now permanently retains
uncertainty after failed unload, so retries cannot reset Session or free the
context. `component_stop_probe.cpp` models that SDK flag mutation and misleading
no-op success. Keep the same guard for each ownership lifetime; never recreate it
merely because libctru says unloaded. SDK source/library hash/disassembly evidence
is saved under output-research/sources.json, libctru-dsp.c and
libctru-unload-disassembly.txt.

Guarded v9/v10 native builds51062/12417 passed. Final v9 Old/New regressions
19949/41796 passed32 chunks and2 recoveries per model. Final v10 runs90075/17994
again verified8 non-silent DSP blocks and clean teardown, then reported the known
unsupported inactive CSND result. No active streaming/second streaming boot was
validated in Azahar. Refreshed per-package native-validation.json files point to
final emulator-v9-{old,new}-20260909T172908... and
emulator-v10-{old,new}-20260909T173010... evidence. Package input/artifact45/47 hashes
matched. Current installer hashes:
- v9:28ec049c6012cf681b69031750a719a6309a8a0e1c9e0dd8f8af24dd1e495980
- v10:31e460ee9ebf296d081d40321a407c1e866004d575094858e0b0edbe3fe528f3
No upload: latest active-mode FTP192.168.1.48:5000 returned No route to host.
No process remains live. Umbrella now invokes owner tests after refreshing mixer
hooks; Python syntax passed. No full umbrella rerun solely for these independent
host/native lifecycle additions.

Next required work: concrete native lifecycle ports and exclusive CSND output
worker, careful APT/DSP-hook coordination, then install owner callbacks and output
routing in the opt-in game build. Actual producer capture coverage/capacity and
physical continuous output, CPU/RAM/frame-time/audio measurements remain unproven.
Do not substitute mocked lifecycle tests or emulator kernel correctness for those
requirements. Keep one Old/New build and Old3DS20FPS/no-frame-interpolation.
Goal remains active.

## Dedicated output worker validation (2026-09-09)

Completed pending session56625: run_output_worker.py exited0. The bounded stereo
FIFO and dedicated CSND worker passed real host-thread tests with ASan/UBSan and
Werror, and actual ARM11 SDK template compilation. Evidence is in
builds/dsp-synth-probe-20260909/output-worker-validation.json and
/tmp/soh-dsp-output-worker.log. No process remains live.

The worker exclusively owns Stream calls, separately locks its2048-frame FIFO,
primes at1024 frames, and retains queued data until publication is accepted.
It uses a16KiB thread stack, checks4ms service deadlines, and requires exact-zero
threadJoin success. Uncertain DMA stop or join retains ownership and prevents
reuse/discard. Pending recovery cannot guarantee audible exactly-once playback
after partial publication. Tests cover FIFO wrap/order, priming, streaming,
backpressure, pending recovery, startup/publication/service/deadline/stop faults,
and positive join-timeout retention. Deliberately blocked publication concurrent
with enqueue/stop remains useful additional coverage.

run_typed.py now invokes the worker runner after CSND stream tests and hashes its
runner/mock SDK inputs. The full umbrella was not rerun for this addition.
Latest FTP check192.168.1.48:5000 returned No route to host; no upload occurred.

Remaining: native worker scheduling/output validation; concrete lifecycle ports
with APT/DSP-hook coordination; opt-in game callback installation/output routing;
actual game capture coverage and matched hardware CPU/RAM/FPS/audio benchmarks.
Host tests and ARM11 compilation do not prove physical playback or FPS gains.
The goal remains active with one Old/New build and Old3DS20FPS/no-interpolation.

## Reusable native component owner (2026-09-09)

Added mapped_component_3ds.h, used by both v9/v10 through mapped_native_context.h.
This extracts the actual firmware load, converted-address validation, bounded
signature wait, event attachment/table upload and confirmed stop into an owner
usable by the future native game lifecycle ports. It explicitly requires a
caller-held DSP reference, stopped NDSP, serialization and APT exclusion.

The SDK's LoadComponent can return success for an already-loaded foreign image;
startup now refuses that state without unloading it. A failed load without a
reliable SDK loaded flag remains uncertain (a subsequent SDK unload may be a
no-op). Known partial loads require unload. Stop refuses externally cleared SDK
flags, requires exact-zero unload through a sticky guard, closes the transport
before resetting Session, and retains uncertainty after close failure.
No destructor implicitly releases uncertain ownership.

run_mapped_component.py passed16 fault/startup cases under ASan/UBSan/Werror and
actual ARM11 compilation. Tests include repeated lifetimes, existing component,
load refusal/uncertainty, cancellation/signature timeout, map/table/event/attach
failures, failed and positive-timeout unload, unregister/close failure, and
external flag clearing.11 evidence hashes match. Reviewer found no ownership
blocker under the declared contract; requested event/cleanup tests were added.
The umbrella invokes this runner after native transport tests and includes its
mock/runner hashes; syntax verified, full umbrella not rerun for this addition.

Final native builds65320/94977 exited0, package46/48 hashes verified. Final v9
runs99181/62221 passed32 successful DSP chunks plus2 forced recoveries per model.
Final v10 runs44622/17909 verified8 non-silent prefill blocks and clean teardown,
then encountered Azahar's expected inactive CSND limitation. No streaming or
physical FPS gain is claimed. Refreshed native-validation.json links final runs:
- builds/dsp-synth-probe-20260909/emulator-v9-old-20260909T175224207793Z/verification.json
- builds/dsp-synth-probe-20260909/emulator-v9-new-20260909T175223964487Z/verification.json
- builds/dsp-synth-probe-20260909/emulator-v10-old-20260909T175255693801Z/verification.json
- builds/dsp-synth-probe-20260909/emulator-v10-new-20260909T175255629189Z/verification.json

No process remains live. No upload during this change.

Fresh upstream libctru apt.c and ndsp.c evidence is saved in output-research with
lifecycle-sources.json URLs/hashes. APT's event thread invokes aptDspSleep on
APTSIGNAL_SLEEP_ENTER BEFORE setting FLAG_SHOULDSLEEP, so APTHOOK_ONSLEEP in the
game main loop is too late to protect custom DSP ownership. Direct DSP sleep/wake
commands also run there; HOME/system-applet transitions add paths. Automatic
wake loads the saved component before DSPHOOK_ONWAKEUP. NDSP sleep finalization
and ndspExit have blocking waits; do not naively call owner.suspend/ndspExit in a
DSP callback without proving callback ordering and worker wake/join behavior.
These are upstream-source observations, not a new native sleep verification.

Next: concrete native lifecycle/output ports and explicit SDK DSP-hook/APT
coordination, game callback installer and routing; worker native streaming;
actual producer capture coverage; matched physical CPU/RAM/FPS/audio validation.
Keep the complete goal active, one Old/New build and Old3DS20FPS/no-interpolation.

## SDK lifecycle interception in the opt-in build (2026-09-09)

Added prepare_sdk_lifecycle.py: extracts installed libctru.a's single dsp.o into
local build output and renames ONLY aptDspSleep/Wakeup/Cancel to SohSdkDsp*.
Verifies strong expected definitions, all other globals/address/types preserved,
and exact original bytes after inverse rename. The installed archive is never
modified. sdk_lifecycle_bridge.c/h supplies the strong original entry names,
delegating to SDK behavior before installation and to immutable once-published
handlers afterward. Handler gets the original SDK continuation for CPU/NDSP mode.
This permits custom sleep/wake control BEFORE SDK's automatic unload/reload.

cmake/DspLifecycle3DS.cmake attaches the extracted object and bridge to soh_3ds
only under SOH3DS_DSP_CAPTURE (default remains OFF). Actual runtime handler
installation is not yet connected. A standalone ARM11 CMake link test exercises
this same function; disassembly confirms actual aptEventHandler sleep/wake and
aptWaitForWakeUp cancel call sites branch to the bridge symbols, not renamed
SDK continuations. Evidence: sdk-lifecycle/apt-call-routing.json and linked-symbols.
No complete opt-in game link/run was claimed.

run_sdk_lifecycle.py passed host ASan/UBSan/Werror checks of SDK forwarding,
incomplete hook refusal,16-way racing immutable installation, custom-path
suppression, explicit CPU continuations, SDK symbol adaptation, ARM11 compilation
and real CMake link.11 validation hashes match. Umbrella invokes it after native
component validation; Python syntax passed. No full umbrella rerun for addition.
Reviewer found no blocker. Contract is explicit: install BEFORE first component
load with no lifecycle transition in flight, keep table immutable/context alive
to process exit, serialize handlers with producer/control, and preserve which
backend owned sleep through matching wake/cancel. The bridge itself does NOT
gate producer/output or implement game mode pairing.

New v11 lifecycle_hardware_probe.cpp installs a static lifetime handler, runs
actual SDK entry points, confirms custom stop closes mapping/event, reboots
without automatic SDK reload, and checks mixer/PCM/history continuity. It tests
sleep/wake and sleep/cancel with explicit reenable,32 successful DSP chunks plus
2 forced recoveries over2 boots for each model. Four sleeps/two wakes/two cancels
per model. Deliberate native calls are not physical lid/HOME transitions.
build_mapped_hardware.py --lifecycle packages title0004000005349000 and
emulator_smoke.py --lifecycle validates exact totals.

Native v11 first build19520 failed Werror because the shared DSP cancellation
hook was unused; registering it also guards against accidental stock callback
routing. Build22937 passed; final37461 after header contract comments passed and
CXI is byte-identical to tested image. Old/New sessions8941/33594 exited0:
- emulator-v11-old-20260909T180016105335Z/verification.json
- emulator-v11-new-20260909T180016020973Z/verification.json
v11 native-validation.json records evidence hashes; hardware-manifest51 hashes
match. CIA060a954dcd93cdea23ea46c2fbf96c5d7c3b225c29ea4e474ebd90ac883b58b3.
No live processes remain. No upload occurred. Prior v9/v10 manifests have older
build-driver hashes because the driver gained v11; their tested binaries were
not changed this turn.

Next integration can now provide concrete SDK lifecycle handlers instead of
trying to stop custom DSP from APTHOOK_ONSLEEP (which is too late). A dedicated
producer gate must close before taking/waiting for the chunk lock and remain
closed through transition; wake/control cannot acquire that producer gate.
Remember CPU-vs-custom sleep ownership independently of current requested mode.
A custom handler bypasses SDK DSP-hook delivery and therefore owns full stop,
restart/cancel and output-worker handling. NDSP mode must retain SDK ordering.
Consider keeping an extra runtime dspInit reference across NDSP shutdown: then
ndspExit's internal dspExit decrements but does not perform unobservable unload;
explicit guarded DSP_UnloadComponent can confirm teardown. Do not call ndspExit
on an already-sleeping worker before SDK wakes/cancels its wait. These are
integration design notes, not verified game code.

Still required: concrete runtime lifecycle/output ports and game callback
installer/routing; native output-worker streaming; game capture coverage and
matched hardware CPU/RAM/FPS/audio plus physical sleep/HOME validation. Goal
remains active, one Old/New build and Old3DS20FPS/no-frame-interpolation policy.

## Full-batch producer gate and SDK coordinator (2026-09-09)

Added producer_gate.h/producer_gate_3ds.cpp to the opt-in game build. The actual
OTRGlobals inner refill loop takes a nonblocking SohDspProducerScope after
locking audio.mutex, covering Buffered, synthesis, Play and batch diagnostics.
Failure returns to existing WaitForWork (5ms after priming), so a paused producer
does not wait indefinitely for lid wake and audio.Stop can still join it.
OTRAudio_Exit sets permanent SHUTDOWN pause after audio.Stop. Separate LIFECYCLE,
CONTROL and SHUTDOWN bits prevent control completion or late wake from reopening
a different pause. Pause waits for the current whole batch to release its gate.
LightLock_TryLock zero/nonzero semantics match installed SDK documentation.
CMake capture definitions/include path now also cover OTRGlobals.cpp; the shared
SDK attachment compiles the gate. Default capture remains OFF. gpucmd copies
soh_3ds source list, so it inherits this source/object attachment too.

Added sdk_lifecycle_owner.h, a coordinator around the existing MixerOwner and
actual batch gate. Its control mutex serializes SDK callbacks and explicit
backend changes, and records Awake/CpuSleeping/CustomSleeping/Closed. Custom
sleep/wake uses owner.suspend/resume; CPU sleep/wake preserves original SDK
continuations. Backend switching is refused until matching wake/cancel. Duplicate
sleep is idempotent. If SDK sleep returnsfalse, gate reopens. Safe failed custom
restart can resume in owner Silent mode. CPU-sleep shutdown invokes saved SDK
cancel BEFORE owner.shutdown so ndspExit does not join a sleeping worker.
SDK cancellation is terminal for this audio lifetime; late wake cannot reopen it.

Lock order is explicit: producer audio.mutex -> batch gate -> MixerOwner lock.
Control coordinator mutex -> pause/wait batch gate -> MixerOwner lock. Native
callbacks/ports MUST NOT acquire audio.mutex while holding coordinator/owner
locks. Producer never acquires coordinator mutex. The existing Old core0 optional
1ms fairness sleep still holds the batch gate; safe but adds up to1ms pause delay.
Gate/coordinator implementation reviewed with no blocker. Suggested edge cases
are covered in tests: CPU-sleep shutdown, late wake/cancel, SDK sleep false,
failed custom resume toSilent and overlapping pause reasons.

run_producer_gate.py now covers real host threads using the actual native gate,
plus four isolated terminal coordinator lifetimes with mock backend modes and
actual C bridge dispatch, under ASan/UBSan/Werror. It compiles the native gate,
instantiates SdkLifecycleOwner against actual MixerOwner/CtrTransport with real
SDK LightLock shape (life/CPU method declarations only), and syntax-checks the
actual OTRGlobals opt-in call sites with cached ARM11 flags. Final session55800
exited0;17 evidence hashes verified in producer-gate/validation.json.
run_sdk_lifecycle.py CMake fixture now enables CXX for gate source. Session64010
passed;13 evidence hashes verified. Refreshed apt-call-routing.json confirms the
new linked image's real APT event/resume/cancel callers target bridge functions.
Umbrella invokes run_producer_gate after SDK bridge tests and hashes runner/mock;
Python syntax passed. No full umbrella or full game link/run was claimed.
No live processes remain. No FTP upload or new hardware installer this turn.

Next is concrete native runtime assembly: CtrTransport/Session/MappedComponent3ds,
ChunkFrontend buffers/CPU adapter, OutputWorker3ds<CsndStream>, MixerOwner and
SdkLifecycleOwner; connect raw NDSP init/stop/buffer/play callbacks and install
immutable SDK/mixer callbacks at appropriate startup/producer boundaries.
Callbacks must be installed before first DSP component load with static lifetime.
Route Buffered/Play under the existing full-batch gate. Runtime controls must
use the coordinator, not bypass it. Maintain an extra DSP reference so NDSP exit
cannot hide the actual unload result; confirm unload before reclaiming ownership.
Existing libctru ndspInit also has a threadCreate-failure path returning prior rc
(which can be0); do not assume arbitrary startup failures are proven successful
merely from that return when designing native failure verification.

Still missing: concrete runtime and installer/output routing, native worker
continuous streaming, actual game capture coverage/capacity, physical lid/HOME,
and matched CPU/RAM/FPS/audio measurements. No performance gain is yet proven.
Keep full goal active, one Old/New build and Old3DS20FPS/no-interpolation.


## Native game runtime integration (2026-09-09, continuation)

The concrete opt-in runtime is now implemented in native_runtime_3ds.h,
native_ndsp_port_3ds.h, native_audio_backend_3ds.cpp and native_mixer_cpu.cpp.
It connects captured game chunks, mapped DSP execution, the exclusive CSND
output worker, CPU replay, raw NDSP fallback and the SDK lifecycle coordinator.
NdspAudioPlayer routes through a weak optional backend API; its backend pointer
is published once before producer startup. Device close parks the coordinator;
reopen reuses the immutable hooks. Activation excludes lifecycle transitions
while acquiring the retained DSP reference and installing mixer hooks.

Actual DSP load/unload IPC is now observed through a renamed SDK-object-local
svcSendSyncRequest reference. This includes SDK inlined unload operations.
Unknown IPC outcomes retain uncertain ownership; a cleared SDK loaded flag
alone cannot authorize wave release. Raw NDSP startup requires a real worker
frame-counter advance before trusting startup success, because the installed SDK
can return stale success after thread creation fails. prepare_runtime_firmware.py
verifies compiler dependency hashes and embeds bytes from the verified CDC.
Capture remains opt-in/default OFF; the current test build cache is ON.

Latest completed producer-gate/coordinator test session 68582 passed six isolated
lifecycle/reconfigure cases. The SDK observer and lifecycle runner passed after
its Python runner fix. Native lifecycle emulator sessions 15630 and 62215 both
exited zero: each model passed 32 chunks, two forced recoveries, four sleeps,
two wakes and two cancels, with exactly 13 observed ownership changes ending
STOPPED. Evidence directories:
- builds/dsp-synth-probe-20260909/emulator-v11-old-20260909T183302795406Z
- builds/dsp-synth-probe-20260909/emulator-v11-new-20260909T183302746631Z
These validate explicit lifecycle routing/correctness, not physical APT events,
continuous output, game capture coverage or performance.

Full game build session 62172 remains running (last inspected progress 67%).
Output: builds/dsp-synth-probe-20260909/game-runtime-20260909T183007Z.
It owns build-3ds-mk/.soh-build.lock. Do not restart it. After completion,
incrementally rebuild libultraship and relink the game under the build lock to
include the pointer-once fix made after its initial library compilation; refresh
copied artifacts and build evidence. No new runtime installer has been uploaded.

FTP retry at 192.168.1.48:5000 returned No route to host. Existing local emulator
assets were located under /home/sian/.local/share/azahar-emu/sdmc (oot.o2r,
soh.o2r, and 3ds/dspfirm.cdc), enabling isolated game startup/fallback checks once
the game link completes. Do not treat Azahar's inactive CSND as playback success.

Remaining: full game build/link, actual native runtime startup/fallback exercise,
real game capture capacity/counters, continuous native audio output, physical
lid/HOME handling, and matched CPU/RAM/FPS/audio measurements. The goal remains
active: free ARM11 CPU with DSP synthesis, stable fallback, one Old/New build,
and Old 3DS 20 FPS with frame interpolation disabled. No gain is proven yet.


## Integrated chunk telemetry and queued game validation

Added MixerOwner::Stats distinguishing committed DSP chunks, all CPU chunks,
and captured chunks replayed on the CPU. Backend selection alone cannot prove
offload. Stats are updated under the existing owner lock and snapshotted under
that lock after PCM publication. Every 256 batches, when General Logging is on,
the native backend writes `dsp synth:` into audio-perf.log and the emulator debug
stream. Records include cumulative dspChunks/cpuChunks/capturedCpuChunks,
recoveries, outputError, and ndspDrops. Mode values are Cpu=0, Custom=1,
Silent=2, Suspended=3, Closed=4, Poisoned=5. Output errors are None=0, Thread=1,
Open=2, Start=3, Service=4, Publish=5, Deadline=6, Stop=7. Logging off performs no
diagnostic file I/O. Counters add three unsigned words to the owner.

Regression test first failed because stats() did not exist, then passed under
ASan/UBSan with actual hooked C mixer and Teakra firmware. Assertions distinguish
successful DSP work, explicit CPU barrier replay, failed DSP replay, and live CPU
chunks after fallback. Final metrics test session 93187 exited zero; log:
/tmp/soh-dsp-owner-metrics.log. Final native backend diagnostics and CPU dispatcher
also passed syntax compilation with actual soh_3ds_old ARM11 game flags. This
is not full game/native output verification.

Live initial build remains session 62172. Follow-up session 4335 is confirmed
live and waiting for its build lock via /tmp/soh-finish-runtime-build.py. It will
require initial build-result.json success evidence, preserve initial artifacts,
rebuild libultraship for the pointer-once fix, relink the latest game, and refresh
artifact hashes. On success it releases the lock then invokes isolated Old/New
game observations using /tmp/soh-game-runtime-smoke.py. That runner creates a CXI,
copies local game archives/config/save/DSP firmware into isolated SD storage,
enables diagnostic logging only in the copied config, and observes 100 seconds
per model. Observation JSON records actual output without claiming playback or
performance. Poll these sessions; do not duplicate or restart running builds.
No new CIA has been uploaded. Physical playback/offload benefit remains unproven.


## FTP restored and usable native NDSP emulator firmware

FTP reconnected to 192.168.1.48:5000 in active mode. Captured listings, old
performance/boot/fatal logs, config and the user's existing DSP firmware in
builds/dsp-synth-probe-20260909/ftp-20260909T184847Z. The current SD listing still
had no dsp-chain-output-probe.txt, dsp-output-probe.txt, dsp-lifecycle-probe.txt or
dsp-stream-probe.txt. Do not interpret the old audio logs as DSP-runtime results.

Uploaded /cias/soh-dsp-stream-v10-53120bd9.cia and verified the complete FTP
readback SHA256 53120bd9ef6f83edaf6d6f152e4581651239885b2d1bf7b7766ca47009db357b.
Artifact matched stream-v10/hardware-manifest.json. User was instructed to install
and run DSP streaming v10, then START and reopen FTP. This is the physical
CSND/DSP continuous-output probe, not the game runtime or a performance proof.
Upload evidence: stream-v10/ftp-upload.json. No integrated game CIA uploaded yet.

The first isolated control game failed in Azahar LLE pipe-slot assertion during
NDSP initialization, using the old local 292KiB firmware. The physical device's
firmware is 49756 bytes, SHA256
8e213f3e71d2e3e45d1169bac6465a70eabeb22b303f1fa6d7679370ffad0f54.
A new isolated control run with this firmware completed the 100-second observation,
remained running and emitted game-frame/NDSP pacing records without that assertion.
Evidence: game-smoke-old-20260909T185053Z/control-validation.json. Original local
firmware was preserved. Future queued game runs use the copied physical firmware.

Control sessions 12626 (old firmware assertion) and 25283 (working physical
firmware) are terminal. The smoke helper now extracts the existing CIA's single
NCCH content for CXI rather than recompressing ELF, validates header/size, and
records firmware hash and actual frames/DSP telemetry/assertions. makerom fallback
uses supported '-f ncch', not '-f cxi'. Helpers remain in /tmp as previously noted.
Initial game build62172 is live (last progress88%). Follow-up4335 remains queued
behind its lock; it will rebuild latest LUS, relink and run Old/New observations.

CPU accounting research also confirmed installed svcGetThreadInfo and Luma's
extensions do not offer per-thread CPU runtime. Evidence and fetched source hashes
are in cpu-accounting-research/. Do not label wall synthesis timings or committed
DSP chunk percentages as CPU execution time savings. Full goal remains active.


## Full game link and native fallback observed on both models

Initial build62172 and final follow-up4335 both completed successfully. The latter
rebuilt libultraship for the pointer-once fix and relinked/repackaged the current
runtime. Final artifacts are in game-runtime-20260909T183007Z:
- soh_3ds_old ELF SHA256 c748f9dea32988b333c06f4c58fe2277057dfb1dfad98c4f33aaed8259632364
- soh_3ds_old.cia SHA256 3d826dff2e6688674149f71cbea4954de2f6e0c2953d1727517f5ff286cc447d
- build-result.json hashes were checked against current files.
- cia-validation.json verifies title0004000005348000, DSP IO mapping and SVC82/84.
- emulator-integration.json aggregates actual Old/New runtime observations.

Both initial 100-second integrated observations ended during initialization,
without game frames or DSP telemetry; do not call them passing gameplay tests.
They were intentionally terminal, then replaced by 300-second observations:
- Old: game-smoke-old-20260909T190506Z (session42810, exit0)
- New: game-smoke-new-20260909T190551Z (session95135, exit0)
Both used the exact same final ELF/CIA and the copied physical NDSP firmware.
Both remained running at the end, emitted game frames and native DSP telemetry,
and had no emulator fatal assertion. Each committed six actual game synth chunks
on DSP, then replayed one captured chunk during recovery after CSND startup error3
(Azahar reports inactive). SDK logs confirm actual custom-component unload BEFORE
loading NDSP firmware. Continued CPU/NDSP output reported zero ndspDrops.
Old reached logged frame1860, with19 telemetry reports and14586 CPU chunks;
New reached frame1980, with14 reports and10746 CPU chunks. Integration evidence
is in each observation's integration-validation.json. This validates startup,
limited real-game DSP execution and native fallback, not sustained DSP synthesis,
heavy-voice capture capacity, physical output, sleep/HOME or a speedup.

The first linked image's ELF sections grew by225884 bytes versus the saved control
(about221KiB code/static data); BSS grew98776 bytes. This is not peak runtime RAM
measurement. Preserve the original control artifacts for matched hardware checks.

No live build or emulator processes remain. FTP retry after the tests returned
No route to host. The only new hardware upload remains the v10 streaming probe;
the final game CIA is built and staged locally but HAS NOT been uploaded. User
already has instructions to run v10 and reopen FTP; do not repeat the old request.
Next: retrieve its physical streaming result when available, resolve any physical
CSND problem, then transfer/readback-verify the staged game for actual sustained
DSP use, capture capacity, CPU/RAM/FPS/audio and lifecycle checks. Maintain one
Old/New binary and Old20FPS/no-interpolation; full goal remains active.


## Hardware wait — goal blocked, not complete

Three consecutive goal turns with no remaining live build/emulator work checked
192.168.1.48:5000 at 19:14:02, 19:14:54 and 19:15:19 UTC on 2026-09-09. All
returned No route to host. The required physical v10 streaming log is unavailable;
continuous CSND playback, sustained game DSP use and matched hardware measurements
remain unverified. No further code change is justified by the missing physical
result. Goal marked blocked pending console/test access, not complete.

Resume by retrieving /3ds/soh/dsp-stream-probe.txt, then use the preserved staged
CIA/control and final native integration evidence described above. Do not rebuild,
restart finished emulator runs, or repeat installation requests merely to recover
context. No integrated game upload occurred. User has the v10 probe already.


## Hardware returned: incomplete first synthesis block; v12 diagnostics uploaded

User restored FTP. Read /3ds/soh/dsp-stream-probe.txt into
ftp-20260909T192305Z/: v10 Old reports stream_boot=0 and successful component
load (stage=ready, uncertain=0), then ends. It contains no prefill result, output
start, teardown or finished marker. This is NOT a streaming pass, and it does not
yet identify the stopping operation. The newest fetched crash dump35 belongs to
the game title0004000005348000, not the v10 probe0004000005348f00, so it cannot
explain this probe's incomplete log. Dump34 and35 were preserved in that capture.

Added --stream-trace to build_mapped_hardware.py: separate stream-v12 directory,
CIA title0004000005349100 / DSP Stream Trace v12. With the diagnostic-only
SOH3DS_DSP_STREAM_TRACE define, the first prefill block emits flushed checkpoints
around CPU oracle, capture, seal, submit, step and event wait, including current
phase, transport stage/result and tick. Tracing stops after that block, before
CSND starts. Normal probes have no-op trace methods; the staged game is unchanged.
The probe still writes /3ds/soh/dsp-stream-probe.txt and explicitly labels v12.
No root-cause fix or performance improvement is claimed from instrumentation.

Final v12 build39231 exited0. Emulator runs96932/48688 exited0 with expected
unsupported CSND outcome. Both completed all first-block checkpoints through
synthesis_end phase4, all8 prefill blocks (PCM/history reference equality), and
confirmed output/DSP stop with retained0. Evidence:
- emulator-v12-old-20260909T192711530663Z/verification.json
- emulator-v12-new-20260909T192713218853Z/verification.json

All v12 hardware-manifest source/artifact hashes matched before upload. Uploaded
/cias/soh-dsp-stream-trace-v12-a0a8bd0c.cia; complete readback SHA256
 a0a8bd0c79bf3b6ecfb6e006661a7380099206c5ae02125fe529e83d97051835
matched (293312 bytes). Evidence: stream-v12/ftp-upload.json. User's next physical
action is to run DSP Stream Trace v12 once and reopen FTP so its checkpoint log
can identify the failure. No live builds/emulator/transfers remain. The staged
integrated game CIA remains local and has not been uploaded. Previous blocked
hardware-access condition ended; this turn made concrete diagnostic progress.


## v12 physical result localizes stop to first ChunkRunner submission step

User ran v12; retrieved ftp-20260909T210726Z/dsp-stream-probe.txt. Old hardware
passed CPU oracle, capture, seal and frontend.start/shared-state staging. Last
checkpoint is step_begin phase3 transport=invalidate rc0, with no step_end or
wait_begin. This narrows the missing return to the first ChunkRunner::step Working
path (prepare captured loads, payload/parameter writes, cache calls or command
publication), NOT the completion-event wait. No hardware fix is established.

Added diagnostic-only --stream-io-trace (v13, title0004000005349200 / DSP Stream IO
v13). stream_io_trace.h compiles all trace calls away unless explicitly enabled.
The first prefill block logs entry/exit of native cache calls with word/count,
prepareLoad, payload/parameter writes and command publication. Its deadline is
5s only for this logged prefill block; physical v12 SD flush checkpoints consumed
roughly10ms each, so the extra diagnostics must not manufacture a500ms timeout.
Subsequent blocks and normal runtime retain the existing timeout. Tracing ends
before CSND playback. No production behavior fix or new game build is claimed.

Native transport mocked-SVC tests and ARM11 compile passed; actual game backend
syntax compilation with IO traces disabled passed. Final v13 build93232 exited0.
Final Old/New emulator runs47815/27280 exited0 with every IO checkpoint and all8
DSP prefill blocks complete, then expected unsupported CSND start/confirmed stop:
- emulator-v13-old-20260909T211221173824Z/verification.json
- emulator-v13-new-20260909T211221264146Z/verification.json

All hardware-manifest hashes matched. Uploaded and fully readback-verified:
/cias/soh-dsp-stream-io-v13-49efc0c1.cia
SHA256 49efc0c11b6c2ba21dc8cea5005744b7f83f7824d3fa9d97ed1a296d8b76b0d4
296896 bytes. Evidence stream-v13/ftp-upload.json. Next physical action: run DSP
Stream IO v13 once, then reopen FTP. No live jobs remain. The staged game CIA
remains local; newer source changes are probe tracing only and were not built into
that artifact. Full synthesis offload/output/performance goal remains unproven.

## v13 launch boundary and v14 mailbox comparison (2026-09-09 21:39 UTC)

Retrieved v13 physical log in ftp-20260909T213210Z; independently retrieved again
in ftp-20260909T213814Z (same final checkpoint). Payload, parameters, command
queue and metadata flush finish. Last trace is flush_end word0000 count16,
tick391218097358. No second flush_begin follows io.write(0,1). This localizes loss
of ARM progress to command launch; it does NOT establish that the store itself
hangs. Immediate DSP execution or interrupt/service effects remain hypotheses.
Earlier physical v6 used mailbox completion; DR0 signaling is newer and has not
passed hardware. SDK describes interrupt registration ambiguously; upstream
Azahar dsp_dsp.cpp confirms emulator event routing only, not physical semantics.
3dbrew HTTP fetch returned403; no physical IRQ conclusion was inferred.

Added diagnostic --stream-mailbox, title DSP Mailbox v14 / 0004000005349300:
- Firmware makeTypedFirmware fifth argument mailboxOnly omits DR0 completion
  store, retaining shared sequence/status/busy publication and loader/shutdown.
- Diagnostic CtrTransport omits interrupt registration/DR0 IPC, checks mailbox
  completion and uses sleeps capped1ms. Existing Session validates sequence/status.
- Production defaults are unchanged; regenerated default DSP1 is byte-identical
  to stream-v13 firmware. The separate mailbox package has16523 program words.
- New mailbox_transport_probe.cpp verifies bounded sleep, pending/completion,
  stale sequence and cache failure; absent DR0/registration function definitions
  make accidental linkage fail. It passed. Existing run_native_transport.py
  mocked SVC/IPC tests and normal ARM11 compilation passed.
- Full v14 CIA build52557 passed. Old/New emulator sessions89894/92410 exited0;
  emulator-v14-old-20260909T213822158809Z and
  emulator-v14-new-20260909T213823956992Z each verify8 correct PCM/history prefill
  blocks then expected unsupported CSND start and confirmed DSP/output teardown.
  This is diagnostic correctness, not hardware playback or performance evidence.
- All hardware-manifest source/artifact hashes matched before upload.

Uploaded /cias/soh-dsp-mailbox-v14-4c9d8bc9.cia,296896 bytes; complete readback
SHA2564c9d8bc9cac6734e4e673a3522a444f2dcc0cee1737d1ba791e6c1c3b0a66faf
matched. Evidence stream-v14/ftp-upload.json. User must install/run DSP Mailbox
v14 once and reopen FTP. Do not claim the freeze is fixed until that result.
No integrated game upload. No live build/emulator/FTP sessions remain. All prior
work and game artifacts preserved. Goal remains unfinished: physical synth/output,
CPU/FPS/RAM and lifecycle measurements still required.

## v14 physical streaming PASS; game mailbox integration (2026-09-09 22:07 UTC)

User ran v14. Retrieved ftp-20260909T220747Z/dsp-stream-probe.txt and wrote
validation.json: Old 3DS, both boots complete259 blocks each (518 total), both
8-block prefills match PCM/history, CSND active_mask3, minimum queued frames4028
and4031, stop/reload confirmed, finished=PASS. This is real standalone DSP/CSND
streaming evidence. Trace-heavy timings are not CPU/FPS measurements; subjective
audio quality and actual game workload remain unverified. Compared with v13,
omitting DR0 emission/registration gets past the command-launch freeze. Exact
physical interrupt/service mechanism remains unresolved, so avoid that path.

Game opt-in backend now selects the same mailbox firmware via
prepare_runtime_firmware.py --mailbox-only. Its generated header selects the
paired host completion mode before native_runtime includes and the game has a
compile-time mailbox requirement. SOH3DS_DSP_CAPTURE stays opt-in. Existing v14
probe switch aliases this mode. Transport checks mailbox and sleeps at most1ms;
Session retains sequence/status checking and timeout/stop-before-replay policy.
Native transport regression now includes mailbox pending/completion, stale
sequence, cache fault, timeout and no DR0 IPC linkage. Actual SDK compilation
caught svcSleepThread returns void; removed an attempted result check and fixed
the mock signature before proceeding.

run_mixer_owner.py --reuse-hooks --mailbox-only passed real hooked PCM/history,
barrier, submission/wait failure replay after unload, sleep/resume, lifecycle
serialization and fail-closed stop tests using actual mailbox firmware. That
harness explicitly rejects any DR0 notification in mailbox mode. No gain claimed.
Game rebuild staged under game-mailbox-20260909T221008Z, preserving prior ELF/CIA.

## Mailbox game build verified and uploaded (2026-09-09 22:26 UTC)

Final rebuild67019 exited0 after SDK void sleep signature fix. Artifacts in
game-mailbox-20260909T221008Z (prior ELF/CIA preserved as before-*):
- ELF SHA25688a3ea4f5e3fde2736d7f26b892a4de278aa2956784c16f16dbea598ca17234f
- CIA SHA2568c47d307cc224135fe8cd6140398193cf70b507691fec4f862fde7a4c05ca5cf
- CIA15176640 bytes, title0004000005348000, DSP mapping/cache SVC82/84 verified.
- backend-validation.json verifies compiled backend has no DR0 IPC/IO-trace
  references, does use svcSleepThread, and embeds byte-identical physical v14
  firmware (SHA2568258a8330b191722ebc1fa91e95650b16dcc06fea01ba2530b3c3f3b21b104e9).

run_native_transport.py now automatically runs mailbox tests and compiles both
DR0 and mailbox open/close/wait/poll paths against actual SDK. Passed. The
mailbox mixer-owner negative control rejects original DR0 firmware, proving the
new harness catches unwanted notifications (mailbox-negative-control.json).

Game emulator runs use identical final ELF/CIA:
- Old game-smoke-old-20260909T221455Z,300s,frame600,4 reports:6 DSP chunks,
  3066 CPU chunks,1 captured replay,1 recovery,outputError3,NDSP drops0.
- Initial New game-smoke-new-20260909T221457Z ended300s during startup without
  frame/audio telemetry; it is NOT a passing integration run.
- Extended/adaptive New game-smoke-new-20260909T222043Z,275.02s,frame600,
  3 reports:6 DSP chunks,2298 CPU chunks,1 captured replay,1 recovery,
  outputError3,NDSP drops0. Session25731 exited0 after telemetry.
Both passing runs explicitly verify custom unload before NDSP load and continued
frames/no fatal assertion. CSND startup is unsupported by emulator; these are
real game DSP chunk/fallback checks, not sustained custom game playback or gain.
Both have integration-validation.json. No live emulator/build/FTP sessions remain.

CIA uploaded to a .part staging path with full readback match, then renamed only
after both game checks passed. Ready remote installer:
/cias/soh-dsp-mailbox-game-8c47d307.cia
Evidence ftp-upload.json in game-mailbox-20260909T221008Z. This IS now a game
upload, superseding earlier notes that no game CIA had been uploaded. User's next
physical action: FBI install, run usual save/route for about2min, then reopen FTP.
The same CIA serves Old/New, retaining Old20FPS/no frame interpolation policies.

Saved baseline hardware config/logs in ftp-20260909T220747Z. Existing config
DebugLogging1/ProfileLogging1/FrameLogging0 was preserved, not edited. Next pull
bootinfo.txt, audio-perf.log and perf.csv; confirm mode1 with increasing committed
dspChunks under actual gameplay before claiming sustained offload. If fallback,
inspect outputError/recoveries. Then matched CPU/FPS/RAM/audio and physical APT
checks remain. Main goal is still incomplete; standalone518 blocks prove bounded
DSP/CSND streaming, not game CPU savings or subjective audio quality.
