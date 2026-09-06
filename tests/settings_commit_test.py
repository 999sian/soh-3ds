#!/usr/bin/env python3
"""Execute production Commit and the real dependent-setting callback on host.

The menu framework/3DS library cannot link into this host fixture. Extract the
two complete C++ bodies, providing only a small file-backed CVar boundary.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOH = ROOT / "third_party/shipwright/soh/soh"


def body(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


commit = body((SOH / "Enhancements/dualscreen3ds/SettingsBridge3DS.cpp").read_text(),
              "void Commit(WidgetInfo& w)")
menu = (SOH / "SohGui/SohMenuEnhancements.cpp").read_text()
callback = body(menu[menu.index('AddWidget(path, "Fix Megaton Hammer Crouch Stab"'):],
                "[](WidgetInfo& info)")
source = r'''
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#define CVAR_ENHANCEMENT(x) "gEnhancements." x
struct WidgetInfo { const char* cVar; std::function<void(WidgetInfo&)> callback; };
std::map<std::string, int> cvars;
const char* configPath;
int initializations = 0;
namespace ShipInit { void Init(const char*) { ++initializations; } }
int CVarGetInteger(const char* key, int fallback) {
    auto it = cvars.find(key);
    return it == cvars.end() ? fallback : it->second;
}
void CVarClear(const char* key) { cvars.erase(key); }
void CVarSave() {
    std::ofstream stream(configPath);
    for (const auto& [key, value] : cvars) stream << key << ' ' << value << '\n';
    assert(stream.good());
}
void Reload() {
    cvars.clear();
    std::ifstream stream(configPath);
    std::string key;
    int value;
    while (stream >> key >> value) cvars[key] = value;
}
''' + commit + r'''
int main(int argc, char** argv) {
    assert(argc == 2);
    configPath = argv[1];
    cvars[CVAR_ENHANCEMENT("CrouchStabHammerFix")] = 0;
    cvars[CVAR_ENHANCEMENT("CrouchStabFix")] = 1;
    WidgetInfo parent{CVAR_ENHANCEMENT("CrouchStabHammerFix"), ''' + callback + r'''};
    Commit(parent);
    assert(initializations == 1);
    assert(CVarGetInteger(CVAR_ENHANCEMENT("CrouchStabFix"), 0) == 0);
    Reload();
    if (CVarGetInteger(CVAR_ENHANCEMENT("CrouchStabFix"), 0) != 0) {
        std::cerr << "dependent crouch setting returned after restarting config\n";
        return 1;
    }
    assert(CVarGetInteger(parent.cVar, 1) == 0);
    parent.callback = nullptr;
    cvars[parent.cVar] = 1;
    Commit(parent);
    Reload();
    assert(CVarGetInteger(parent.cVar, 0) == 1);
    std::cout << "settings callback persistence regression passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix="soh-settings-test-") as directory:
    directory = Path(directory)
    cpp = directory / "test.cpp"
    cpp.write_text(source)
    executable = directory / "test"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-O1", str(cpp),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable), str(directory / "config.txt")], check=True)
