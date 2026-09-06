# Provenance of `platform/3ds/`

Everything in this directory is **adapted from
[`EstebanPdN/mario-kart-64-3ds`](https://github.com/EstebanPdN/mario-kart-64-3ds)**,
vendored on 2026-08-30 from commit `HEAD` of its default branch.

Reuse was authorised by the project owner. Recorded here because the licence
position needs to be visible to anyone who reads this code, not buried in a
commit message.

## Licence position

That repository **declares no licence**. Under the Berne Convention that means
all rights reserved by default: there is no grant to copy, modify or
redistribute. This is therefore:

- fine for private local builds,
- **not** something that can be published without permission from its author.

It compounds with the gate already recorded in `SPEC.md` §0 L1 — `HarbourMasters/Shipwright`
also declares no licence — so this project is already unpublishable until both
are resolved. Adding this code does not create a new class of problem, but it
does add a second party whose permission would be needed.

**Before any public release**, either obtain permission from
`EstebanPdN`, or replace this directory with an independent implementation.
`src/pica/` already contains one such implementation (a from-scratch PICA200
Fast3D backend with an exhaustively verified combiner lowering), so that route
is open.

**Update 2026-09-03.** The project owner granted permission to reuse code from
MK64-3DS, the SM64-3DS lineage (sm64_3ds, Epic0522 Ultimate), DaedalusX64-3DS /
picaGL, and SoH / libultraship. The analysis above stays as history; reuse from
those sources is now authorised and needs no further sign-off.

## Why adapt rather than reimplement

MK64-3DS is the only Fast3D-on-PICA200 port over libultraship that is proven on
real 3DS hardware. Its `gfx_citro3d.cpp` is 2043 lines against the 1152 of the
from-scratch backend here, and the difference is not padding — it is a texture
cache with LRU recycling, frame interpolation, wide-mode presentation,
performance counters, and a set of hardware workarounds discovered by running on
a device rather than an emulator.

Two concrete bugs in the from-scratch backend were found by reading it:

- the NPOT vertical flip must be anchored to the power-of-two **backing** height,
  not the logical height (invisible on POT textures, wraps on NPOT);
- freshly uploaded textures must be bound and given the sampler defaults the
  interpreter assumes on a cache miss, or point-filtered draws get skipped.

## What was changed

Adaptation from SpaghettiKart to Ship of Harkinian. Divergences from upstream
are marked `// SoH-3DS:` so a diff against MK64 stays readable.

## Files kept from the original tree

`source/`, `include/` and `compat/` as vendored. Files that turn out to be
MK64-specific and unused by this port are deleted rather than left to rot; check
`git log` for what was removed and why.
