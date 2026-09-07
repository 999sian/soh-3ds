#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
code=r'''
#include "decal_depth_3ds.h"
#include <cassert>
#include <cmath>
int main() {
 float a[4]={-1,-1,0,1}, b[4]={1,-1,0,1}, c[4]={-1,1,0,1};
 float flat=DecalDepthBias3DS(a,b,c,400,240);
 assert(flat>0 && flat<0.0001f); // equal surfaces pass; nearer foreground still occludes
 b[2]=0.2f; // reversed depth changes 0.1 across 400 pixels
 float slope=DecalDepthBias3DS(a,b,c,400,240);
 assert(std::fabs(slope-(0.0004999f+flat))<1e-7f);
 assert(std::fabs(DecalDepthBias3DS(a,b,c,800,480)-(0.00024995f+flat))<1e-7f);
 // Homogeneous scaling does not change the projected plane.
 for(int i=0;i<4;++i)b[i]*=3;
 assert(std::fabs(DecalDepthBias3DS(a,b,c,400,240)-slope)<1e-7f);
 assert(DecalDepthBias3DS(a,a,c,400,240)==flat);
 a[3]=0;
 assert(std::isfinite(DecalDepthBias3DS(a,b,c,400,240)));
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-I'+str(root/'platform/3ds/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Decal slope: flat occlusion, slope, resolution, homogeneous coordinates and degenerate controls pass')
