#!/usr/bin/env python3
"""Build the production XML end-command emitter under real CMake GBI settings.

Uses the library's configuration declarations and target definition verbatim,
without configuring its unrelated platform dependencies. The sentinel after
the XML end node models the hardware crash: F3DEX2 must stop before heap data
can be decoded as a vertex command. This is a host encoding/configuration
regression, not a full renderer or hardware boot test.
"""

import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
LUS = ROOT / "third_party/libultraship"
cmake = (LUS / "CMakeLists.txt").read_text()
configuration = cmake[cmake.index("# ========= Configuration Options ========="):
                      cmake.index("# =========== Dependencies =============")]
target_cmake = (LUS / "src/CMakeLists.txt").read_text()
definition_start = target_cmake.index("target_compile_definitions(libultraship PRIVATE ${GBI_UCODE})")
definition = target_cmake[definition_start:target_cmake.index("\n", definition_start)]

factory = (LUS / "src/fast/resource/factory/DisplayListFactory.cpp").read_text()
end_start = factory.index('} else if (childName == "EndDisplayList") {')
end_start = factory.index("{", end_start) + 1
end_emitter = factory[end_start:factory.index("} else if", end_start)]

# Confirm the boot path's actual assets contain this node, including the first
# material sublist, where ignoring its end already walks beyond the buffer.
assets = ROOT / "third_party/shipwright/soh/assets/custom"
logo = assets / "textures/nintendo_rogo_static/gShipLogoDL"
root_node = ET.parse(logo).getroot()
first_call = root_node.find("CallDisplayList")
assert first_call is not None
first_material = assets / first_call.attrib["Path"]
assert list(root_node)[-1].tag == "EndDisplayList"
assert list(ET.parse(first_material).getroot())[-1].tag == "EndDisplayList"

fixture = r'''
#include <cstdint>
#include <cstdio>
#include "libultraship/libultra/gbi.h"
#include "fast/lus_gbi.h"
static_assert(sizeof(Gfx) == sizeof(Fast::F3DGfx), "encoder/interpreter command stride mismatch");
static Gfx EncodeXmlEndNode() {
    Gfx g{};
''' + end_emitter + r'''
    return g;
}
int main() {
    const Gfx commands[] = {
        { 0xE7000000, 0 }, // preceding material pipeline command
        EncodeXmlEndNode(),
        { 0x01006074, 1 }, // would decode as n=6, destination=52, vertices=1
    };
    for (unsigned i = 0; i < 3; ++i) {
        if (i == 2) {
            std::fprintf(stderr, "XML end opcode 0x%02x failed to stop F3DEX2 before vertex sentinel\n",
                         unsigned(commands[1].words.w0 >> 24));
            return 1;
        }
        if (uint8_t(commands[i].words.w0 >> 24) == uint8_t(Fast::F3DEX2_G_ENDDL)) {
            std::puts("XML end terminates F3DEX2 before vertex sentinel");
            return 0;
        }
    }
    return 1;
}
'''


def run(command):
    return subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


failures = []
with tempfile.TemporaryDirectory(prefix="soh-xml-ucode-") as temporary:
    temporary = Path(temporary)
    source = temporary / "source"
    source.mkdir()
    (source / "test.cpp").write_text(fixture)
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.20)\nproject(XmlUcodeContract LANGUAGES CXX)\n"
        + configuration
        + '\nadd_executable(libultraship test.cpp)\n'
        + 'target_compile_features(libultraship PRIVATE cxx_std_20)\n'
        + 'target_include_directories(libultraship PRIVATE "' + str(LUS / "include") + '")\n'
        + definition + "\n")
    for name, option, invalid in [
        ("fresh-default", None, False),
        ("explicit-f3dex2", "-DGBI_UCODE:STRING=F3DEX_GBI_2", False),
        ("legacy-bool-off", "-DGBI_UCODE:BOOL=OFF", True),
        ("legacy-bool-on", "-DGBI_UCODE:BOOL=ON", True),
    ]:
        build = temporary / name
        result = run(["cmake", "-S", str(source), "-B", str(build),
                      "-DCMAKE_CXX_COMPILER=" + os.environ.get("CXX", "g++"),
                      *([option] if option else [])])
        if invalid:
            if result.returncode == 0:
                failures.append(name + ": invalid boolean microcode cache was accepted")
            elif "Invalid GBI_UCODE" not in result.stdout:
                failures.append(name + ": configuration failed for an unrelated reason\n" + result.stdout)
            else:
                print(name + ": invalid boolean microcode cache rejected")
            continue
        if result.returncode != 0:
            failures.append(name + ": configuration failed\n" + result.stdout)
            continue
        result = run(["cmake", "--build", str(build), "--parallel", "1"])
        if result.returncode != 0:
            failures.append(name + ": compilation failed\n" + result.stdout)
            continue
        result = run([str(build / "libultraship")])
        if result.returncode != 0:
            failures.append(name + ": " + result.stdout.strip())
        else:
            print(name + ": " + result.stdout.strip())
if failures:
    raise SystemExit("\n".join(failures))
print("XML display-list microcode configuration regression passed")
