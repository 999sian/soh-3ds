# Handwritten ARM11 assembly

Explicit user goal, 2026-09-08: handwritten ARM11 assembly for both audio and parts of the game, in support of playable Old 3DS performance (20 FPS acceptable). Preserve automatic Old/New profiles.

First kernels: bit-exact four-tap audio resampling and game transformation matrix copying. Keep C reference implementations, compare outputs and state on ARM11 emulation, and provide a low-overhead standalone hardware benchmark. Emulator timings are not evidence of physical hardware speed. Enable kernels in the game after correctness validation; retain the ability to compare against C. Hardware throughput and whole-game benefit remain separate validation steps.

Future candidates require workload evidence: ADPCM reconstruction, animation/vector transforms and collision math. ARM11 has ARMv6K DSP instructions and VFPv2, but no NEON. Preserve float operation order, saturation, per-tap audio rounding, ABI registers, alignment and alias behavior.

First milestone: both kernels implemented and bit-exact on ARM11 tests. The physical Old 3DS benchmark reports 37.72% less resampling time and 51.27% less matrix-copy time against its C references, with zero correctness failures. Integrated game build 86b1e67e; whole-game validation remains distinct from isolated-kernel results.


Second pass covers additional audio kernels, vector transforms, separating
wake signalling from synthesis locking, and deferred interpreter matrix
composition. Keep only physically measured winning assembly paths in the
release candidate. Per-routine benchmarks do not establish stable gameplay;
validate audio continuity, save loading and scene transitions separately.

Revised second-pass hardware benchmark passed with zero correctness failures:
ADPCM 18.07%, envelope 23.45%, gain/reverb-buffer mixing 27.20%, and game vector
transforms 13.13% less time. Accepted game candidate: 4e690fde. Whole-game
crackling and the separate inaccessible-audio-stack crash remain to validate.

Current priority: user confirms very crackly audio in both vertex game builds,
including with logging off, and asks about full ARM11 synthesis or offload.
The vertex pilot establishes no FPS benefit; its option remains OFF. Investigate
the shared synthesis-delivery shortfall using sampled phase diagnostics before
choosing further assembly or a CPU/DSP backend change. See
[audio_strategy.md](audio_strategy.md) for evidence, alternatives and the next
physical capture. No crackling fix is claimed yet.
