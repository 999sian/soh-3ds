#!/usr/bin/env python3
"""Build a tiny synthetic .o2r so Phase 0 can run without an OoT ROM.

An .o2r is a plain ZIP (libultraship reads it with libzip), so this is just a
zip with a couple of known entries in it.

    ./scripts/make-test-o2r.py [out.o2r]

Copy the result to sd:/3ds/soh/test.o2r.
"""
import sys
import zipfile

out = sys.argv[1] if len(sys.argv) > 1 else "test.o2r"

with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("hello.txt", "phase 0 reached the archive layer\n")
    # Something big enough that it must actually inflate, not just sit in the
    # central directory.
    z.writestr("filler.bin", bytes(64 * 1024))

print(f"wrote {out}")
