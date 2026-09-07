#!/usr/bin/env python3
"""Check production depth-map calls keep coplanar decals in front in reversed Z."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'platform/3ds/source/gfx_citro3d.cpp').read_text()
calls = re.findall(r'C3D_DepthMap\(true, -1\.0f, ([^;]*\?[^;]*)\);', source)
assert len(calls) == 2, 'check setter and framebuffer-restoration depth maps'
cpp = r'''
#include <cstdio>
#include "decal_depth_3ds.h"
#include <algorithm>
float scale, offset;
void C3D_DepthMap(bool, float s, float o) { scale=s; offset=o; }
struct Impl { bool decal; } state, *mImpl=&state;
int main() {
int failures=0;
'''
for expr in calls:
    cpp += '\nfor (bool decal : {false, true}) {\nmImpl->decal=decal;\n'
    cpp += f'C3D_DepthMap(true, -1.0f, {expr});\n'
    cpp += r'''
// Perspective-divided PICA clip Z is [-1,0]; depth test is GREATER.
for (float z : {-0.9f, -0.5f, -0.02f, -0.0005f}) {
 float base=-z;
 float overlay=std::clamp(scale*z+offset,0.f,1.f);
 if (decal && offset >= 0.0001f) {
  std::fprintf(stderr,"FAIL: flat decal crosses a foreground surface 0.0001 depth units nearer\n"); ++failures;
 }
 if ((decal && !(overlay > base)) || (!decal && overlay != base)) {
  std::fprintf(stderr,"FAIL: decal=%d z=%g base=%g overlay=%g\n",decal,z,base,overlay);
  ++failures;
 }
}
}
'''
cpp += 'return failures ? 1 : 0;\n}\n'
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path/'test.cpp').write_text(cpp)
    subprocess.run(['c++','-std=c++17','-I'+str(root/'platform/3ds/include'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Decals pass reversed-depth tests in setter and framebuffer restoration')
