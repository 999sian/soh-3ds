#!/usr/bin/env python3
"""Exercise the real menu-free 3DS startup branch: it must initialize seed settings."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root/'third_party/shipwright/soh/soh/OTRGlobals.cpp').read_text()
a = source.index('    SOH3DS_INIT_TRACE("SetupGuiElements");')
b = source.index('    SOH3DS_INIT_TRACE("AudioCollection");', a)
branch = source[a:b]
program = r'''
#include <cassert>
#include <memory>
#define SOH3DS_INIT_TRACE(x) ((void)0)
namespace SohGui {
void SetupGuiElements() {}
void SetupMenuElements() { assert(false && "desktop widgets must stay skipped"); }
}
namespace Rando {
struct Settings {
    bool created = false, updated = false;
    int startingHearts = 0;
    static auto GetInstance() { static auto s = std::make_shared<Settings>(); return s; }
    void CreateOptions() { assert(!created); created = true; startingHearts = 2; }
    void UpdateAllOptions() { assert(created); updated = true; }
};
}
int main() {
''' + branch + r'''
    auto settings = Rando::Settings::GetInstance();
    assert(settings->created && "3DS skipped the functional randomizer settings initialization");
    assert(settings->updated);
    assert(settings->startingHearts + 1 == 3);
}
'''
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p/'test.cpp').write_text(program)
    subprocess.run(['g++', '-std=c++20', '-D__3DS__', str(p/'test.cpp'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
print('Menu-free 3DS startup initializes randomizer definitions and dependencies')
