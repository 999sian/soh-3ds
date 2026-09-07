#!/usr/bin/env python3
"""Reject a 3DS ELF that drops enhancement static initialization functions."""
import argparse
from collections import Counter
import subprocess


def initializers(nm, path):
    symbols = subprocess.check_output([nm, "--defined-only", path], text=True)
    return Counter(line.split()[-1] for line in symbols.splitlines()
                   if "_GLOBAL__sub_I_" in line)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nm", required=True)
    parser.add_argument("archive")
    parser.add_argument("elf")
    args = parser.parse_args()
    expected = initializers(args.nm, args.archive)
    if not expected:
        parser.error("no enhancement initializers found in archive")
    missing = expected - initializers(args.nm, args.elf)
    if missing:
        parser.exit(1, "enhancement initializers missing from game:\n" +
                    "\n".join(sorted(missing.elements())) + "\n")
    print(f"enhancement link verified: all {sum(expected.values())} initializers retained")


if __name__ == "__main__":
    main()
