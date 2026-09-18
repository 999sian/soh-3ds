#!/usr/bin/env python3
"""Exercise the production decal submission path with a recording GPU backend."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'platform/3ds/source/gfx_citro3d.cpp').read_text()
begin = source.index('    if (mImpl->decal) {', source.index('// Record that this draw samples'))
end = source.index('    mImpl->triangleCount +=', begin)
code = r'''
#include "decal_depth_3ds.h"
#include <cassert>
#include <cstddef>
#include <vector>
#include <random>
struct Vertex { float position[4]; };
struct Draw { int first, count; float bias; };
std::vector<Draw> draws;
float currentBias;
constexpr int GPU_TRIANGLES = 0;
void C3D_DepthMap(bool, float scale, float bias) {
    assert(scale == -1.0f); currentBias = bias;
}
void C3D_DrawArrays(int, int first, int count) { draws.push_back({first,count,currentBias}); }
struct State {
    bool decal = true;
    Vertex* packedVertices;
    int viewportWidth = 400, viewportHeight = 240;
    unsigned drawCallCount = 0, sampleFogDrawCount = 0;
} state, *mImpl = &state;
struct Program { bool fog = true; } p, *program = &p;
void Submit(size_t firstVertex, size_t vertexCount) {
''' + source[begin:end] + r'''
}
void Check(const std::vector<float>& slopes) {
    // Nonzero offset guards against accidentally drawing earlier batches.
    std::vector<Vertex> vertices(6 + slopes.size()*3);
    for (size_t i=0;i<slopes.size();++i) {
        vertices[6+i*3] = {{-1,-1,0,1}};
        vertices[7+i*3] = {{1,-1,slopes[i],1}};
        vertices[8+i*3] = {{-1,1,0,1}};
    }
    state.packedVertices = vertices.data();
    state.drawCallCount = state.sampleFogDrawCount = 0;
    draws.clear(); Submit(6, slopes.size()*3);
    size_t cursor = 6, expectedRuns = 0;
    float previous = -1;
    for (size_t i=0;i<slopes.size();++i) {
        const auto* tri = vertices.data()+6+i*3;
        float bias = DecalDepthBias3DS(tri[0].position,tri[1].position,tri[2].position,400,240);
        if (i==0 || bias != previous) ++expectedRuns;
        previous = bias;
    }
    assert(draws.size() == expectedRuns);
    for (const auto& draw : draws) {
        assert(draw.first == static_cast<int>(cursor) && draw.count > 0 && draw.count%3==0);
        for (int i=0;i<draw.count;i+=3) {
            const auto* tri = vertices.data()+cursor+i;
            assert(draw.bias == DecalDepthBias3DS(tri[0].position,tri[1].position,tri[2].position,400,240));
        }
        cursor += draw.count;
    }
    assert(cursor == vertices.size());
    assert(state.drawCallCount == draws.size());
    assert(state.sampleFogDrawCount == (p.fog ? draws.size() : 0));
}
int main() {
    Check({}); Check({0}); Check(std::vector<float>(1000,0));
    Check({0,0,0.2f,0.2f,0,0.4f,0.4f,0});
    Check({0.2f,0.20001f,0.2f}); // Similar slopes must not be approximated.
    std::mt19937 rng(123);
    for (bool fog : {false,true}) {
        p.fog=fog;
        for (int n=0;n<100;++n) {
            std::vector<float> slopes(300);
            for (auto& slope : slopes) slope=static_cast<float>(rng()%6)/10;
            Check(slopes);
        }
    }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path/'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined', '-g',
                    '-I'+str(root/'platform/3ds/include'), str(path/'test.cpp'),
                    '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
print('Decal batches preserve exact bias, order, coverage and counters; 1000 flat triangles use one draw')
