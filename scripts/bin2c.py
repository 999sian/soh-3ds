#!/usr/bin/env python3
"""Embed a binary file as a C array.

Stands in for devkitPro's bin2s, which lives in the general-tools package and is
not installed here. Emitting C rather than assembly avoids any assembler-syntax
differences and works identically.

    ./scripts/bin2c.py in.shbin out.c symbol_name

Produces `const unsigned char <symbol>[]` and `const unsigned int <symbol>_size`,
4-byte aligned because DVLB_ParseFile takes a u32*.
"""
import sys

if len(sys.argv) != 4:
    sys.exit(__doc__)

src, dst, sym = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(src, "rb").read()

# Pad to a 4-byte boundary; DVLB parsing reads u32 words.
if len(data) % 4:
    data += b"\0" * (4 - len(data) % 4)

with open(dst, "w") as f:
    f.write(f"/* Generated from {src} by scripts/bin2c.py -- do not edit. */\n")
    f.write("#include <stdint.h>\n\n")
    f.write(f"__attribute__((aligned(4))) const unsigned char {sym}[] = {{\n")
    for i in range(0, len(data), 12):
        row = ", ".join(f"0x{b:02x}" for b in data[i:i + 12])
        f.write(f"    {row},\n")
    f.write("};\n\n")
    f.write(f"const unsigned int {sym}_size = {len(data)};\n")

# devkitPro's dkp_add_embedded_binary_library also emits a declaring header, and
# vendored code from MK64-3DS includes it, so produce one alongside.
hdr = dst[:-2] + ".h" if dst.endswith(".c") else dst + ".h"
with open(hdr, "w") as f:
    f.write(f"/* Generated from {src} by scripts/bin2c.py -- do not edit. */\n")
    f.write("#pragma once\n\n#include <stdint.h>\n\n")
    f.write("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n")
    f.write(f"extern const unsigned char {sym}[];\n")
    f.write(f"extern const unsigned int {sym}_size;\n\n")
    f.write("#ifdef __cplusplus\n}\n#endif\n")

print(f"{dst} (+ header): {len(data)} bytes as {sym}")
