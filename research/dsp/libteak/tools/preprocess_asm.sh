#!/bin/bash

# Note: Run this script from the root of the repository:
#
#     bash tools/preprocess_asm.sh

# This script preprocesses assembly files with includes and expressions to
# generate assembly files with all of them evaluated. This is done because the
# LLVM Teak assembler can't evaluate expressions that are used as arguments of
# instructions.

mkdir -p build

for filename in asm/*.s; do
    temp=${filename/asm/build}.cpp
    out=${filename/asm/source}

    # Expand macros
    cpp -Iinclude $filename -o $temp

    # Evaluate expressions
    python3 tools/preprocess_asm.py --input $temp --output $out
done
