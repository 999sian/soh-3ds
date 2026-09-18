#!/usr/bin/env python3
"""Exercise active renderer plans with a provisional byte-rounded TEV model.

This checks colour arithmetic, not GPU execution. Hardware characterization is
still required; see patches/PICA-PRECISION-AUDIT.md. A missing complement on the
HUD split or a premature clamp in a folded plan must fail the numerical checks.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "platform/3ds/source/gfx_citro3d.cpp").read_text()
enums = source[source.index("enum CombinerSource"):source.index("enum ShaderOption")]
plans = source[source.index("std::array<float, 4> ConstantForSource"):
               source.index("// The one constant colour a stage reads:")]
packing = source[source.index("uint8_t ToByte("):
                 source.index("// SoH-3DS telemetry:")]
code = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "compiled_channel_3ds.h"
enum GPU_COMBINEFUNC {GPU_REPLACE,GPU_MODULATE,GPU_INTERPOLATE,
                     GPU_MULTIPLY_ADD,GPU_ADD,GPU_SUBTRACT};
''' + enums + packing + plans + r'''
// Integer reference: nearest-byte rounding, saturation, no scale. Inputs to
// each operation are bytes; a multiply keeps its fraction until stage output.
int rounded(int numerator) {
    return std::clamp((std::max(0, numerator) + 127) / 255, 0, 255);
}
int execute(const ChannelPlan& plan, std::array<int, 16> values, int channel) {
    values[SourceZero] = 0;
    values[SourceOne] = 255;
    values[SourceFolded] = ToByte(plan.folded[channel]);
    for (size_t i=0; i<plan.size(); ++i) {
        const auto& op=plan[i];
        const int a = values[op.source[0]];
        const int b = op.oneMinusSecond ? 255-values[op.source[1]] : values[op.source[1]];
        const int c = values[op.source[2]];
        int numerator = 0;
        switch (op.function) {
            case GPU_REPLACE: numerator = a*255; break;
            case GPU_MODULATE: numerator = a*b; break;
            case GPU_INTERPOLATE: numerator = a*c+b*(255-c); break;
            case GPU_MULTIPLY_ADD: numerator = a*b+c*255; break;
            case GPU_ADD: numerator = (a+b)*255; break;
            case GPU_SUBTRACT: numerator = (a-b)*255; break;
            default: assert(false);
        }
        values[SourceCombined] = rounded(numerator);
    }
    return values[SourceCombined];
}
int main() {
    // Independent hand-calculated boundaries for the model and real packing.
    assert(rounded(0)==0 && rounded(127)==0 && rounded(128)==1);
    assert(rounded(128*128)==64 && rounded(255*255)==255);
    assert(rounded(-255)==0 && rounded(256*255)==255);
    assert(ToByte(-0.1f)==0 && ToByte(0.5f)==128 && ToByte(1.1f)==255);
    std::array<std::array<float, 4>, 7> constants{};
    std::array<int, 16> values{};
    const int multiply[4]={SourceTexel0,SourceZero,SourceTexel1,SourceZero};
    const auto mod=BuildChannelPlan(multiply,-1,constants,false);
    for(int a=0;a<256;++a) for(int c=0;c<256;++c) {
        values[SourceTexel0]=a; values[SourceTexel1]=c;
        assert(execute(mod,values,0)==rounded(a*c));
    }
    // All byte-valued HUD colours and masks. The original expression is
    // (A-B)*C+B. Extra stage rounding may cost one byte, never more.
    const int hud[4]={1,2,SourceTexel0,2};
    unsigned differences=0; int maximum=0;
    for(int a=0;a<256;++a) for(int b=0;b<256;++b) {
        constants[0].fill(a/255.0f); constants[1].fill(b/255.0f);
        const auto split=BuildChannelPlan(hud,-1,constants,false);
        const auto direct=BuildChannelPlan(hud,0,constants,false);
        for(int c=0;c<256;++c) {
            values[1]=a; values[2]=b; values[SourceTexel0]=c;
            const int expected=rounded(a*c+b*(255-c));
            assert(execute(direct,values,0)==expected);
            const int delta=std::abs(execute(split,values,0)-expected);
            assert(delta<=1);
            differences += delta!=0; maximum=std::max(maximum,delta);
        }
    }
    std::printf("HUD: 16777216 cases; %u differ; max error %d/255\n", differences,maximum);
    // General subtract fold: probe RGB and alpha independently, including
    // negative intermediate A-B. This must not regress to premature clamping.
    const int general[4]={SourceTexel0,1,2,3};
    unsigned foldedCases=0; int foldedMax=0;
    for(int b : {0,1,64,128,254,255}) for(int c : {0,1,64,128,254,255})
    for(int d : {0,1,64,128,254,255}) {
        if(d*255<b*c) continue; // Outside the renderer's safe fold domain.
        constants[0].fill(b/255.0f); constants[1].fill(c/255.0f);
        constants[2].fill(d/255.0f);
        for(int channel : {0,3}) {
            const auto plan=BuildChannelPlan(general,-1,constants,channel==3);
            for(int a=0;a<256;++a) {
                values[SourceTexel0]=a; values[1]=b; values[2]=c; values[3]=d;
                const int expected=rounded((a-b)*c+d*255);
                const int delta=std::abs(execute(plan,values,channel)-expected);
                assert(delta<=1);
                foldedMax=std::max(foldedMax,delta); ++foldedCases;
            }
        }
    }
    std::printf("Folded RGB/alpha: %u cases; max error %d/255\n",foldedCases,foldedMax);
}
'''
with tempfile.TemporaryDirectory(prefix="soh-tev-precision-") as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(code)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-O2", "-Wall", "-Wextra",
                    "-I" + str(ROOT / "platform/3ds/include"), str(path / "test.cpp"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
