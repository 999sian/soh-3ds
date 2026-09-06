#!/usr/bin/env python3
"""Compile the real sun projection, coordinate setup and depth callback.

Identity projection math and the renderer/glow boundaries are supplied by the
host fixture. Both 3DS and desktop callback branches execute against asymmetric
sun positions, including the existing five-pixel probe offset.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/shipwright/soh/src/code"


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


environment = (SOURCE / "z_kankyo.c").read_text()
projection = function((SOURCE / "z_play.c").read_text(), "void func_800C016C(")
get_depth = function(environment, "u16 Environment_GetPixelDepth(")
callback = function(environment, "void Environment_GraphCallback(")
setup_start = environment.index("            func_800C016C(play, &pos, &screenPos);")
setup_end = environment.index("            if (D_8011FB44", setup_start)
setup = environment[setup_start:setup_end]

source = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>
using f32 = float;
using s16 = int16_t;
using s32 = int32_t;
using u16 = uint16_t;
constexpr int SCREEN_WIDTH = 320, SCREEN_HEIGHT = 240, MTXMODE_NEW = 0;
struct Vec3f {float x, y, z;};
struct Projection {float ww = 1, wx = 0, wy = 0, wz = 0;};
struct PlayState {Projection viewProjectionMtxF;};
struct GraphicsContext {};
// The geometric fixture is an identity view-projection matrix.
void Matrix_Mult(Projection*, int) {}
void Matrix_MultVec3f(Vec3f* src, Vec3f* dest) {*dest = *src;}
s16 D_8015FD7E, D_8015FD80;
u16 D_8011FB44;
using Coordinate = std::pair<float, float>;
std::vector<Coordinate> prepared, queried;
PlayState* expectedPlay;
Coordinate expectedSun;
bool SamePixelPosition(Coordinate a, Coordinate b) {
    return std::fabs(a.first-b.first)<.001f && std::fabs(a.second-b.second)<.001f;
}
void OTRGetPixelDepthPrepare(float x, float y) {prepared.emplace_back(x, y);}
float OTRGetAspectRatio() {return 400.f / 240.f;}
u16 OTRGetPixelDepth(float x, float y) {
    queried.emplace_back(x, y);
    return SamePixelPosition(Coordinate(x, y), expectedSun) ? 0 : 0xFFFC;
}
void Lights_GlowCheckPrepare(PlayState* play) {
    assert(play == expectedPlay);
    OTRGetPixelDepthPrepare(31, 41);
}
void Lights_GlowCheck(PlayState* play) {
    assert(play == expectedPlay);
    OTRGetPixelDepth(31, 41);
}
''' + projection + '\n' + get_depth + '\n' + callback + r'''
void PositionSun(PlayState* play, Vec3f pos) {
    Vec3f screenPos;
''' + setup + r'''
}
int main() {
    PlayState play;
    expectedPlay = &play;
    const Vec3f positions[] = {{-0.5f, 0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}};
    const Coordinate screenWithOffset[] = {{80, 55}, {240, 175}};
#ifdef __3DS__
    // NDC +/-0.5 renders at physical X120/280 after Fast3D's .8 aspect
    // correction. The depth bridge scales native query X by1.25, so use96/224.
    const Coordinate depthPositions[] = {{96, 185}, {224, 65}};
#else
    const Coordinate depthPositions[] = {{80, 55}, {240, 175}};
#endif
    for (int i = 0; i < 2; ++i) {
        prepared.clear();
        queried.clear();
        PositionSun(&play, positions[i]);
        assert(Coordinate(D_8015FD7E, D_8015FD80) == screenWithOffset[i]);
        expectedSun = depthPositions[i];
        Environment_GraphCallback(nullptr, &play);
        assert(prepared.size() == 2 && queried.size() == 2);
        if (!SamePixelPosition(prepared[0], expectedSun) || !SamePixelPosition(queried[0], expectedSun)) {
            std::fprintf(stderr, "sun expected (%g,%g), prepared (%g,%g), queried (%g,%g)\n",
                         expectedSun.first, expectedSun.second, prepared[0].first, prepared[0].second,
                         queried[0].first, queried[0].second);
            return 1;
        }
        assert(D_8011FB44 == 0); // The occluder at the actual sun probe was found.
        assert(prepared[1] == Coordinate(31, 41) && queried[1] == Coordinate(31, 41));
        assert(Coordinate(D_8015FD7E, D_8015FD80) == screenWithOffset[i]);
    }
    std::puts("sun depth: projection, offset, matching prepare/query and unchanged glow pass");
}
'''
with tempfile.TemporaryDirectory(prefix="soh-sun-depth-") as temporary:
    temporary = Path(temporary)
    cpp = temporary / "test.cpp"
    cpp.write_text(source)
    for name, flags in (("3ds", ["-D__3DS__"]), ("desktop", [])):
        executable = temporary / name
        subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-O1", *flags,
                        str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
