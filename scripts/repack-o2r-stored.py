#!/usr/bin/env python3
"""Repack an .o2r archive with no compression, for the 3DS.

    ./scripts/repack-o2r-stored.py oot.o2r [out.o2r]

Why this exists
---------------
libzip opens a deflate entry by setting up a zlib inflate stream, and zlib's
window plus state is about 44 KB per open file. On the 3DS the app heap runs at
its ceiling once SoH's managers, the archive index and the game arenas are
resident, so those 44 KB allocations start failing. libzip then reports
"Malloc failure" from zip_fopen_index, LoadFileProcess returns null, and the
texture or vertex resource silently does not load - Link and the scene geometry
render untextured with no crash and nothing in the log naming the cause.

Measured on a vanilla NTSC oot.o2r (39,057 entries):

    deflate 32.3 MB : 140 zip_fopen_index failures, 54 assets never loaded
    stored  58.7 MB :   0 failures,                  0 assets missing

Stored entries need no inflate stream at all, so the allocation disappears
rather than being made more likely to succeed. It is also faster: a 268 MHz
ARM11 does not spend CPU inflating every asset read. The cost is SD card space,
which is the one resource this platform has to spare.

Duplicate names
---------------
SoH's packer emits a couple of dozen genuinely duplicated entry names. libzip's
zip_name_locate resolves a name to the first match, so the duplicates are
preserved in order rather than dropped, keeping lookup behaviour identical.
"""

import os
import shutil
import sys
import zipfile


def repack(src: str, dst: str) -> None:
    with zipfile.ZipFile(src) as zin:
        infos = zin.infolist()
        # Writing through ZipInfo keeps each entry's original name and order,
        # including deliberate duplicates, and only changes the storage method.
        with zipfile.ZipFile(dst, "w", compression=zipfile.ZIP_STORED) as zout:
            for info in infos:
                out = zipfile.ZipInfo(info.filename, date_time=info.date_time)
                out.compress_type = zipfile.ZIP_STORED
                out.external_attr = info.external_attr
                zout.writestr(out, zin.read(info))

    print(f"{len(infos)} entries")
    print(f"  in : {os.path.getsize(src) / 1048576:.1f} MB")
    print(f"  out: {os.path.getsize(dst) / 1048576:.1f} MB")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__.strip().splitlines()[0], file=sys.stderr)
        print(f"usage: {os.path.basename(argv[0])} <in.o2r> [out.o2r]", file=sys.stderr)
        return 2

    src = argv[1]
    if not os.path.isfile(src):
        print(f"no such archive: {src}", file=sys.stderr)
        return 1

    # Repack via a temporary file so an in-place run cannot destroy the input
    # halfway through and leave an unusable archive on the SD card.
    dst = argv[2] if len(argv) > 2 else src
    tmp = dst + ".tmp"
    repack(src, tmp)
    shutil.move(tmp, dst)
    print(f"wrote {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
