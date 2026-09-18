#!/usr/bin/env python3
"""Measure the selected game math at target O2/O3 and the ARM11 kernel."""
from pathlib import Path
import json
import re
import os
import subprocess
import tempfile
import hashlib

ROOT = Path(__file__).resolve().parents[1]
TOOL = Path(os.environ.get("DEVKITPRO", str(Path.home() / "dkp-root/opt/devkitpro"))) / "devkitARM/bin"
SOURCE = ROOT / "third_party/shipwright/soh/src/code/z_skin_matrix.c"
KERNEL = ROOT / "src/compat3ds/arm11/skin_matrix_mult.S"
EVIDENCE = ROOT / "builds/old3ds-cpu-batch-20260908T201258Z/math/codegen.json"


def body(source: str) -> str:
    matches = list(re.finditer(r"^void SkinMatrix_MtxFMtxFMult\([^;{}]*\) \{", source, re.M))
    assert matches
    match = matches[-1]
    start = source.index("{", match.start())
    pos, depth = start + 1, 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos]


def metrics(obj: Path, symbol: str) -> dict[str, int]:
    table = subprocess.check_output([str(TOOL / "arm-none-eabi-nm"), "-S", "--size-sort", str(obj)], text=True)
    row = next(line for line in table.splitlines() if line.endswith(" " + symbol))
    size = int(row.split()[1], 16)
    dump = subprocess.check_output([str(TOOL / "arm-none-eabi-objdump"), "-d", str(obj)], text=True)
    text = dump.split(f"<{symbol}>:", 1)[1].split("\n\n", 1)[0]
    instructions = sum(bool(re.match(r"\s*[0-9a-f]+:\s+[0-9a-f]+\s+", line)) for line in text.splitlines())
    return {"bytes": size, "static_instructions": instructions}


def main() -> None:
    source = SOURCE.read_text()
    oracle = "typedef float f32; typedef struct {float xx,yx,zx,wx,xy,yy,zy,wy,xz,yz,zz,wz,xw,yw,zw,ww;} MtxF;\n"
    oracle += "void candidate(MtxF* mfA,MtxF* mfB,MtxF* dest) " + body(source) + "\n"
    flags = ["-march=armv6k", "-mtune=mpcore", "-mfpu=vfp", "-mfloat-abi=hard", "-fno-fast-math", "-ffp-contract=off"]
    result = {"compiler": subprocess.check_output([str(TOOL / "arm-none-eabi-gcc"), "--version"], text=True).splitlines()[0]}
    with tempfile.TemporaryDirectory(prefix="soh-matrix-codegen-") as directory:
        path = Path(directory)
        cfile = path / "candidate.c"
        cfile.write_text(oracle)
        for level in ("O2", "O3"):
            obj = path / f"{level}.o"
            subprocess.run([str(TOOL / "arm-none-eabi-gcc"), f"-{level}", *flags, "-c", str(cfile), "-o", str(obj)], check=True)
            result[level] = metrics(obj, "candidate")
            binary = subprocess.check_output([str(TOOL / "arm-none-eabi-objcopy"), "-O", "binary", "--only-section=.text", str(obj), "/dev/stdout"])
            result[level]["text_sha256"] = hashlib.sha256(binary).hexdigest()
        asm = path / "asm.o"
        subprocess.run([str(TOOL / "arm-none-eabi-gcc"), *flags, "-c", str(KERNEL), "-o", str(asm)], check=True)
        result["handwritten"] = metrics(asm, "Soh3dsSkinMatrixMultArm11")
    EVIDENCE.parent.mkdir(parents=True, exist_ok=True)
    EVIDENCE.write_text(json.dumps(result, indent=2) + "\n")
    assert result["O3"]["bytes"] <= result["O2"]["bytes"] + 64, result
    assert result["O2"]["text_sha256"] == result["O3"]["text_sha256"], result
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
