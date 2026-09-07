#!/usr/bin/env python3
"""Prove the game's archive link policy retains constructor-only enhancements."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
cmake = (ROOT / "CMakeLists.txt").read_text()
link = cmake.split("target_link_libraries(soh_3ds PRIVATE", 1)[1].split(")", 1)[0]
link = re.sub(r"#[^\n]*", "", link)
# Exercise the actual production archive policy with the real ShipInit registry.
policy = link.split("soh_decomp", 1)[0].split()
assert "soh_enhancement" in policy

with tempfile.TemporaryDirectory(prefix="soh-enhancement-link-") as temp:
    temp = Path(temp)
    (temp / "used.cpp").write_text('''
#include "ShipInit.hpp"
extern int registered;
void UsedEnhancement() {}
static RegisterShipInitFunc init([] { ++registered; });
''')
    (temp / "optional.cpp").write_text('''
#include "ShipInit.hpp"
extern int registered;
extern int updated;
static RegisterShipInitFunc init([] { ++registered; ++updated; }, {"gCheats.Test"});
''')
    (temp / "main.cpp").write_text('''
#include "ShipInit.hpp"
#include <iostream>
int registered = 0;
int updated = 0;
void UsedEnhancement();
int main() {
    UsedEnhancement();
    ShipInit::InitAll();
    if (registered != 2 || updated != 1) {
        std::cerr << "constructor-only enhancement missing: " << registered << " of 2\\n";
        return 1;
    }
    ShipInit::Init("gCheats.Test");
    return registered == 3 && updated == 2 ? 0 : 2;
}
''')
    cxx = os.environ.get("CXX", "g++")
    common = [cxx, "-std=c++20", "-Os", "-ffunction-sections", "-fdata-sections",
              "-I", str(ROOT / "third_party/shipwright/soh/soh")]
    for unit in ("used", "optional", "main"):
        subprocess.run(common + ["-c", str(temp / f"{unit}.cpp"), "-o",
                                 str(temp / f"{unit}.o")], check=True)
    archive = temp / "libenhancements.a"
    subprocess.run([os.environ.get("AR", "ar"), "rcs", str(archive),
                    str(temp / "used.o"), str(temp / "optional.o")], check=True)
    policy = [str(archive) if token == "soh_enhancement" else token for token in policy]
    executable = temp / "test"
    subprocess.run([cxx, str(temp / "main.o"), "-Wl,--gc-sections", *policy,
                    "-Wl,--end-group", "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("enhancement archive registration regression passed")
