# Resident synthesis protocol (experimental)

This is a correctness prototype with fixed buffers, not the production N64 DMEM
command interpreter. Current production CPU/NDSP audio remains unchanged.

## Versions and completion

`makeTypedFirmware(false)` is v5 (ready word `0x4454`). The opt-in `true` variant
is v6 (`0x4455`) and adds envelopes and filtering. Neither can run alongside
Nintendo NDSP firmware on the same DSP. CSND output feasibility is a separate,
still hardware-unverified v4 experiment.

All addresses below are DSP **16-bit word addresses**, not ARM byte addresses.
Mailbox word 0 is busy, 3 is request sequence, 5 is type, 1 is count, 2 is
argument, 6 is flags, and 7 is queue count. Zero queue count uses those mailbox
fields as one command. Counts 1 through 32 use four-word descriptors at `0x100`:
`type, count, argument, flags`. The last valid descriptor ends at `0x17f`.
Firmware-private queue pointer/count occupy `0x30/0x31`.

ARM publishes payload/descriptors with cache flushes, then request fields with
busy zero, then busy one with a second flush. ARM invalidates the completion
cache lines before checking busy zero and the matching sequence at `0x11`.
Status at `0x12` is zero for success, 3 for invalid command, 4 for invalid
resampler history. An invalid later descriptor aborts **after any prior command
mutations**. The next request resets queue bookkeeping, not the audio state.
CPU fallback must discard/restore a batch snapshot before replaying such work.

These probes use bounded busy polling to establish correctness. Game integration
needs asynchronous submission and scheduled completion, with resident state and
intermediate buffers, to free ARM11 time. A sequence completion alone does not
prove that audio reached speakers or met its output deadline.

## Commands

| Type | Operation | Count | Argument | Flags |
|---|---|---|---|---|
| 0 | Saturating gain mix | 0,16,32,64,128,192,256,512 samples | signed Q15 gain; -32768 means subtraction | 0..2, unused |
| 1 | Stateful four-tap resample | 0,8,16,32,64,128,192,256,512 output samples; 0 processes 8 | unsigned pitch | 0 continue, 1 init, 2 history adjustment |
| 2/3 | ADPCM 4-bit/2-bit | exactly 16 decoded samples | unused | 0 continue, 1 init, 2 loop |
| 4 | Copy decoded block to resampler | exactly 16 samples | unused | 0..2, unused |
| 5 (v6) | Dry/wet stereo envelope | same count list as resample; 0 processes 8, positive 8 processes 16 | five bits: swap, wet-left XOR -4, wet-right XOR -2, left XOR -1, right XOR -1 | 0..2, unused |
| 6 (v6) | In-place eight-tap filter | same count list as resample; 0 processes 8 | unused | 0 continue, 1 init |

The accepted count lists are deliberately bounded prototype subsets. They are
not a replacement for production byte-count rounding and validation. The host
must validate producer/consumer extents too: the fixed copy stages only 16 input
samples, so it cannot satisfy arbitrary resample lengths/pitches. The complete
chain tests use 16 output samples and pitch `0x8000` with persistent history.

## Data map

| Words | Data |
|---|---|
| 0x400 | Resampler input, with preceding history workspace |
| 0x800 | Packed compressed ADPCM bytes |
| 0x1000 | Resampled PCM / envelope input; ADPCM delta scratch during decode |
| 0x1400 | Gain output / envelope dry L / filter input-output |
| 0x1800 | Envelope dry R |
| 0x1c00 | Envelope wet L |
| 0x2000..207f | Eight ADPCM predictor books |
| 0x2500 | Selected predictor scratch |
| 0x2800..280f | ADPCM power-of-two factors |
| 0x2c00 | Envelope wet R (512 samples end before 0x2e00) |
| 0x3000..301f | ADPCM previous history and one decoded block |
| 0x3800..380f | ADPCM persistent state |
| 0x3900..390f | ADPCM loop state |
| 0x3a00..3a0f | Resampler persistent state |
| 0x3b00..3b06 | Initial envelope volumes/rates and runtime flags |
| 0x3b10..3b19 | Envelope working volumes, sample pair, masks, swap |
| 0x3c00..3c0f | Filter history and saved coefficients |
| 0x3c10..3c17 | Filter coefficient setup, updated after blending |
| 0x3c20..3c2f | Filter original-input/history scratch |
| 0x4000..40ff | Production resampling coefficient table |

Envelope writes preserve production dry-L/wet-L/dry-R/wet-R order; standalone
kernel tests include aliased outputs and input/output overlap. Fixed firmware
buffers do not alias. Filter blending uses signed division toward zero, and its
40-bit accumulator preserves all eight 16-bit products before saturation.

## Evidence and limits

Run `python3 research/dsp/synth-probe/run_typed.py` from the repository root.
It extracts the current production C oracles and runs real assembled Teak DSP
instructions. Envelope/filter/resampler reference and harness code use UBSan.
The unmodified ADPCM oracle has signed shifts and is compiled without UBSan;
`-fwrapv` handles accumulator overflow but does not define those shifts. Separate
explicit modulo-arithmetic tests supplement that oracle.

v5 passed Old/New emulator configurations and was uploaded/readback verified as
`/cias/soh-dsp-pipeline-probe-v5-39394f2e.cia`. v6 adds a five-stage chain and is
built separately with `build_hardware.py --extended`; it does not overwrite v5.
Emulator timing is not a physical CPU or FPS benchmark. Neither chain currently
establishes ARM-only synthesis savings, continuous output, or a game speedup.

Remaining integration includes per-voice state mapping,
remaining command coverage/reverb scheduling, whole-update batching, continuous
PCM output, suspend/reset/teardown, transactional fallback and real game timing.

## v7 multiblock and v8 event extensions

`makeTypedFirmware(false, true)` enables v7 (signature `0x4456`); enabling the
third argument also enables v8 (`0x4457`). Both include envelope/filter support.
V7 accepts ADPCM/copy/gain counts in multiples of 16 up to 512; resample,
envelope and filter counts in multiples of 8 up to 512. Existing zero-count
semantics remain: decoder performs history lifecycle, copy is a no-op,
resample/filter/envelope process 8. Positive envelope counts round up to 16.
ADPCM history remains at `0x3000`; decoded output occupies up to
`0x3010..0x320f`. All compressed predictor headers are validated before decoding
mutates PCM/history, including later odd-byte headers. This does not make a
whole queued batch transactional: earlier commands may already have executed.

V8 publishes status (`0x12`), completion sequence (`0x11`), and busy=0 before
writing that sequence to DR0. The host owns a sticky interrupt-0 event, consumes
DR0, then clears the event. Loader/shutdown still use DR2. Only one request can
be in flight; collecting outputs and explicitly acknowledging completion are
required before the next request. Failures require stopping firmware before
resetting the session. No automatic CPU replay is safe after partial mutation.

A 3DS kernel wait timeout is informational (`0x09401bfe`), so it must be checked
before a signed success test. A timeout means Pending and must not read DR0.
The native transport regression covers this plus spurious events and IPC errors.
Hardware DR0 service behavior and continuous audio output remain unverified.

## Experimental mapped DMEM dispatcher (v9, not a hardware build)

The optional fourth `makeTypedFirmware` argument enables mapped byte transfers
and implies v8 event completion; its signature is `0x4458`. The game scratch
buffer is 0xc00 bytes (`mixer.c`: 0x1000-0x3c0-0x40), representing N64 byte
addresses `[0x3c0,0xfc0)`, stored at DSP words `[0x5000,0x5600)`.

Queue type7 moves and type8 clears exact byte extents. Descriptor count is bytes,
argument is the source byte offset within DMEM, and flags is the destination
byte offset. Clear ignores source. The separate `lowerDmemTransfer` host helper
applies production clear/move rounding to16 bytes and converts N64 addresses to
offsets; firmware independently validates offsets and complete extents before
any mutation. Invalid commands return3. A failed later descriptor still leaves
earlier mutations; transactional replay remains a backend responsibility.

The transfer kernel handles odd offsets/lengths and both overlap directions,
preserving the neighboring byte of partial words. Even aligned extents use
forward/backward word loops. Parameters50/51/52/54 and status53 are private to
this dispatcher extension. Existing synthesis commands retain their old layout.

This has Teakra coverage only. The current native component initializes memory
only through word0x4100 and its transport bounds also end there. Both MUST be
extended and verified through0x5600 before deploying mapped commands. This is
not a game backend: bridging resident DMEM to synthesis scratch and per-voice
state, remaining synth commands, staging/commit ownership, continuous output and
failure recovery still need implementation.

### Direct mapped arithmetic

Unreleased v9 now also accepts types9 (gain),10 (envelope),11 (interleave).
Their descriptor count is the normalized number of samples per channel, argument
is a parameter slot0..63, and flags must be0. Gain count is a multiple16 (zero is
no-op); envelope a positive multiple8; interleave a multiple4 (zero is no-op).
Every offset and full extent is checked before PCM or synthesis-scratch writes.

Each immutable parameter record has16 words at `0x6000 + slot*16`. Fields0..4
are sample offsets within audio DMEM, not byte addresses. Gain uses0=input and
1=output, with signed gain in12. Envelope uses0=input,1=dryL,2=dryR,3=wetL,4=wetR,
5=flags0..31,6..8=initial unsigned volumesL/R/wet,9..11=unsigned ratesL/R/wet.
Interleave uses0=left,1=right,2=stereo destination; its output extent is twice
its count. Interleave scratch3d00..07 preserves production groups of four samples
from each channel before stores, including partial aliasing.

`mapped_parameters.h` lowers actual mixer arguments: gain's encoded count is
multiplied16 bytes and rounded32; envelope zero processes8 and other counts round
up16; interleave rounds channel bytes up8. `BUF_S16` halfword rounding is retained
for odd N64 addresses. Failed lowering leaves command and parameter outputs
unchanged. Records must remain immutable until completion/acknowledgement, just
like queued descriptors and payloads.

Native initialization/mapping/transport now needs to cover throughword0x6400,
not merely0x5600. That expansion has not been implemented; no v9 CIA was built or
uploaded. Numerical coverage is in `mapped-arithmetic-validation.json`, including
resident multi-command stereo chains. Decoding/resampling/filter still use fixed
scratch/state and are not yet connected to these addressable commands.

### Direct mapped resampling and voice state

Type12 uses normalized positive sample counts (multiples8), a parameter slot,
and reserved descriptor flags0. Record fields0/1 are input/output sample offsets,
2 is a persistent state slot0..63,3 is unsigned pitch,4 is flags0/1/2. State slots
contain16 words each at6400..67ff. Parameter and state slots are independent.
The host lowerer rounds production byte count up16 and treats zero as8 samples.

DSP preflight validates initial prefix writes (4 samples normally,8 forflag2),
saved adjustment (0 or-9..-15 underflag2), complete output extent, and predicted
input/history extents using the resident phase. End advancement is
`(phase + 2*pitch*count) >>16`. The final8 history samples align relative to the
original input, matching production even when its offset is not aligned8.
A_INIT zeros the prefix/phase; other flags preserve state and aliased-buffer
mutation order. Rejected requests leave PCM and all persistent states unchanged;
private70..76 and status61 are dispatcher scratch.

Native mapped memory must now extend through0x6800; it still ends0x4100 in the
current hardware harness. No v9 native artifact or physical playback claim is
made. The mapped resampler is covered by `mapped_resample_probe.cpp` in the
umbrella suite, including independent state slots, maximum count, phase/bounds
edges, complete memory guards and queued state reuse.

### Direct mapped ADPCM decoding

Types13/14 decode4-bit/2-bit ADPCM. Count is normalized decoded samples, a
multiple16 including zero; argument is a parameter slot and descriptor flags0.
Record fields:0=compressed byte offset within the selected source region,1=output sample offset,
2=persistent state slot0..63,3=loop slot0..63,4=predictor-book slot0..15,
5=flags0/1/2,6=source region (0 audio DMEM,1 immutable payload). Other source
regions reject before mutation. Output includes16 history samples before PCM.
Predictor books occupy5600..5dff (128 words each), ADPCM state6800..6bff and
loop records6c00..6fff (16 words each). Current native mapping does not cover these.

The lowerer rounds decoded byte counts up32. Firmware independently validates all
slots, complete source and output/history extents, then rejects nonempty source/
output overlap before predictor-header preflight. This prevents output/history
writes from invalidating previously checked headers. Rejection is not CPU replay:
the eventual backend must retain the original state/commands for fallback. Zero
compressed extent still performs history/state lifecycle and can share an address
with output. Exactly adjacent regions are permitted.

The optional mapped mode in the existing variable-block decoder reads runtime
pointers from private26(output),2a(state),2b(loop),2c(compressed byte address),
2d(book). Blocks0..95 fit its counter; practical source/output extents determine
the smaller valid count for nonempty input. Selected-book and delta scratch remain
2500..250f and1000..1007. The unchanged default decoder retains its prior layout
and0..32-block contract.

`mapped_synthesis_probe.cpp` queues decode -> resample -> envelope -> interleave
with persistent voice state and compares every non-scratch word with extracted
production operations. This is a synthesis-chain proof, not the integrated game
backend or evidence of physical CPU/FPS benefit.

### Immutable compressed payload staging

Word4100..4fff supplies7680 bytes outside mutable audio DMEM. The host lowerer
`lowerStagedAdpcm` writes source region1 and validates payload offsets/extents.
Firmware repeats region/extent validation and header preflight. It omits the
DMEM overlap test only for this validated disjoint region. Zero-length input
permits the end offset, while nonempty input must fit completely.

Payload remains owned and immutable through every consuming batch. The chunk
probe stages it once, consumes18 bounded batches across six24/28-voice chunks,
and checks payload, final PCM and persistent state after each completion.
This is simulated command composition; native mapping must still extend through
word7000, and production source capture, loops and transactional fallback remain
unfinished. No native v9 artifact has been deployed.

### Mapped saturating addition and decimation copy

Type15 adds signed16 samples with signed16 saturation and sequential load/store
alias semantics. Parameter fields0/1 are input/output sample offsets; count is a
positive multiple16. Host `lowerMappedAdd` follows production count handling:
round byte count down16, then up64, divide by2; zero becomes16 samples because
the production loop always runs once. Unlike gain mixing, there is no Q15 scaling.

Type16 copies every second input sample into consecutive output samples. Fields
0/1 are input/output sample offsets, count is a positive multiple8. Host
`lowerMappedInterl` rounds sample count up8, including minimum8 at zero. The
source region contains2*count samples; stores and pointer increments preserve
sequential alias behavior. The skipped sample is not loaded.

Both reserve descriptor flags0 and accept parameter slots0..63. Firmware validates
complete regions and count/slot/flags before any PCM write. Error status is3.
These are opt-in mapped commands; released v5–v8 paths remain unchanged.

### Mapped zero-order resampling

Type17 uses count as positive output samples (multiple4), argument as parameter
slot0..63 and descriptor flags0. Record0/1 contain input/output sample offsets,
2 unsigned pitch,3 unsigned16 initial phase. Host `lowerMappedZoh` rounds bytes
up8 and uses minimum4 samples for zero, matching production's do/while.

For each sample, read input[position>>17], store output sequentially, then add
pitch*4 to position. Firmware preflight checks the final sampled source address
using `(phase+(count-1)*pitch*4)>>17` and the entire output extent before writing.
Validated count<=1536 keeps position within uint32 even at maximum pitch. Signed
count times unsigned pitch retains all product bits. Zero pitch repeatedly reads
the same source; overlapping output can affect subsequent reads, as on CPU.
There is no persistent state or external synthesis scratch. Result remains at61.

### Mapped128-byte snapshot duplication

Type18 uses count as copies1..24, argument as parameter slot0..63 and descriptor
flags0. Record0/1 are exact input/output byte offsets (odd offsets supported).
Host `lowerMappedDuplicate` adds one to the production repeat count without16-bit
wrap, checks input128 bytes and output copies*128 bytes, and preserves caller
outputs on failure. Firmware independently validates before scratch or PCM writes.

Snapshot128 source bytes once into scratch3e00..3e7f (one byte per DSP word),
then repeat that snapshot at consecutive output byte addresses. Each odd/even
store preserves the other byte of its DSP word. A subsequent command snapshots
current DMEM again; snapshots are not shared between commands. Tests compare two
successive queued duplicates with production C and all non-scratch memory.

### Mapped signed8 sample decoding

Type19 shares the decoder record:0=input byte offset,1=output sample offset,
2=state slot,3=loop slot,4=reserved0,5=flags0/1/2,6=region0 DMEM/1 payload.
Count is decoded samples, multiple16 including zero, at most1520. Descriptor
flags are0 and argument is a parameter slot. Host `lowerMappedS8` rounds output
bytes up32; its input is an N64 address or a payload offset according to `staged`.

All slots, regions and extents validate before synthesis writes. The kernel
writes16 history samples (init zero, loop record or current state), converts each
input byte to the high byte of a signed16 sample, then saves the final16 samples
back to state. Zero count still runs history and state lifecycle. Direct DMEM
aliases retain sequential mutation, including history writes changing later
source reads. Unlike ADPCM, S8 has no predictor headers requiring preflight or
compressed/output overlap rejection. Payload inputs remain disjoint from output.
The shared decoder preflight retains prior ADPCM behavior when S8 mode is false.

### Mapped filter setup and persistent histories

Type20 sets global coefficients from record0..7; count is normalized sample count
(multiple8,0..1536), argument is a parameter slot and flags0. Host setup follows
production ROUND_UP_16 byte count stored in uint16, including65521..65535 wrapping
to zero. Coefficients live at7400..7407 and normalized count at7408.

Type21 processes the current setup. Descriptor count/flags must be0; record0 is
input sample offset,1 state slot0..63,2 init flag0/1. Histories occupy7000..73ff,
16 words each. Process reads the resident count; zero means8 samples. Full input
extent, state slot, flags and current count validate before scratch or state writes.
The lowerer cannot validate the full input extent until the DSP reads current setup.

Each process averages global coefficients with the previous coefficients in that
state (or zeros for init), rounding signed division toward zero. It filters in
place, saves final unfiltered history and updated coefficients to the selected
state, and retains updated GLOBAL coefficients for the next process. Tests queue
a setup followed by two independent histories without resetting coefficients.
Scratch remains3c20..3c2f; old fixed state3c00..3c1f is untouched in mapped mode.
Native mapping must cover7408 inclusive, preferably a cache-aligned bound7420.

### HiLo gain and table multiplication

Type22 applies in-place signed16 gain: record0 sample offset,1 unsigned8 gain,
positive count multiple8. `lowerMappedHiLo` preserves the CPU implementation's
unusual count: samples equal ROUND_UP_32(byte count), minimum8 at zero. Its loop
processes8 samples while decrementing its byte counter by8. DSP multiply then
shift12/high-word saturation implements clamp16((sample*gain)>>4); it fits DSP40.

Type23 snapshots32 input samples from record0 at3f00..3f1f, then multiplies output
(record1) by the repeated table with signed16 saturation. Count is positive
multiple32, normalized from ROUND_UP_64(bytes)/2 with minimum32. Host input adds
the unsigned8 offset before sample-address flooring and rejects address overflow.
Each command takes a fresh snapshot, preserving aliases across queued calls.
Full signed32 products are clipped before conversion; shifting them left16 first
would overflow the40-bit accumulator. Both commands require slots0..63/flags0,
validate complete extents before mutation and report status61.

### Ordered loads from immutable payload

Type24 uses count as exact byte length, argument as payload byte offset and flags
as destination byte offset in audio DMEM (same descriptor shape as move). No
rounding is applied: production aLoadBuffer copies its exact byte count.
`lowerMappedLoad` checks both extents and leaves its command unchanged on failure.

The common transfer kernel has optional payload mode: only move is permitted,
source bounds/base use the immutable bank and destination bounds/base remain
audio DMEM. Both byte and aligned-word paths preserve surrounding bytes. The
banks are disjoint, so either traversal direction is valid. Zero count permits
both end offsets. Clear, move and later loads execute in queue order while the
source payload remains unchanged; there are no ARM copies between these steps.
Payload-bank constants are shared in dmem_layout.h. Status uses53 internally and
normal queue completion externally. Existing default transfer code is unchanged.
