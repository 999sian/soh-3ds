#!/usr/bin/env python3
"""Exercise the renderer's scale selection for a fixed background and door.

The image and geometry have already received Fast3D's aspect correction.
Their corresponding off-centre landmarks must retain identical screen positions.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'platform/3ds/source/gfx_citro3d.cpp').read_text()
start = source.index('    bool coverNativeFullscreenTexture = false;') if '    bool coverNativeFullscreenTexture = false;' in source else -1
selection = source[start:source.index('    std::array<std::array<float, 4>, 7> constants', start)] if start >= 0 else ''
scale = next(line for line in source.splitlines() if 'const float coverScale =' in line)
fixture = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdint>
constexpr int kNativeWidth = 320, kNativeHeight = 240;
constexpr float kFullscreenCoverScale = 1.25f;
struct Texture {bool initialized=true, hasTransparency=false; int sourceWidth=320, sourceHeight=240;};
struct Impl {
    int activeTarget=1, gameTarget=1;
    bool originalAspect=false;
    int selectedFramebuffers[2]={0,0};
    uint32_t selectedTextures[2]={0,0};
    std::vector<Texture> textures{Texture{}};
};
struct Program {int strideFloats=4; bool usedTextures[2]={true,false};};
// The fixture is a native fullscreen background after Fast3D correction.
bool IsNativeFullscreenQuad(const float*, size_t, size_t) {return true;}
float BackgroundScale(Impl* mImpl) {
    Program p; Program* program=&p;
    float vertices[24]={}; const float* drawVertices=vertices; size_t vertexCount=6;
''' + selection + scale + r'''
    return coverScale;
}
int main() {
    Impl impl;
    for (bool original : {false, true}) {
        impl.originalAspect=original;
        const float scale=BackgroundScale(&impl);
        for (float ndcX : {-0.6f, 0.4f, 0.7f}) {
            const float doorX=200.f + ndcX*200.f;
            const float backgroundX=200.f + ndcX*scale*200.f;
            if (std::fabs(doorX-backgroundX) > 0.001f) {
                std::fprintf(stderr,"Door/background mismatch: door %.1f, background %.1f\n",doorX,backgroundX);
                return 1;
            }
        }
    }
    std::puts("Fixed background and door alignment: PASS (wide and original)");
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'alignment.cpp'
    cpp.write_text(fixture)
    binary = Path(directory) / 'alignment'
    subprocess.run(['c++', '-std=c++17', str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
