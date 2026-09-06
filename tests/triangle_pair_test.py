#!/usr/bin/env python3
"""Differential test of extracted production triangle paths; SANITIZE=1 or BENCHMARK=1.

The oracle is the immutable pre-experiment source snapshot, not a handwritten
version of the reuse rule. Only derivation/import services and backend endpoints
are stubs. Clip/cull, viewport handling, key capture/equality, vertex emission,
capacity flush and all three paired-command decoders run from production source.
"""
from pathlib import Path
import hashlib
import os
import re
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path('third_party/libultraship/src/fast/interpreter.cpp')
HEADER = Path('third_party/libultraship/include/fast/interpreter.h')
BEFORE = ROOT / 'docs/evidence/fps60-optimization-2026-09-05/triangle-pair/before'
old = (BEFORE / SOURCE).read_text()
source = (ROOT / SOURCE).read_text()
header = (ROOT / HEADER).read_text()
assert hashlib.sha256((BEFORE / SOURCE).read_bytes()).hexdigest() == \
    'd07691443b626a921c2a16e866fcfce980bbea8242b84dafb177d0f726f91118', 'Oracle changed'


def function(text, signature):
    start = text.index(signature)
    # These production methods have their final brace at column zero.
    return text[start:text.index('\n}', start) + 2]


def structure(name):
    start = header.index('struct ' + name + ' {')
    return header[start:header.index('\n};', start) + 3]


# Require the candidate entry before compiling: the pre-change test is red.
function(source, 'bool Interpreter::GfxSpTri1Impl(')
mutation = os.getenv('MUTATE')
if mutation:
    replacements = {
        'allow_capacity': ('!needsDerive && !capacityFlush && !viewportWork', '!needsDerive && !viewportWork'),
        'allow_derivation': ('!needsDerive && !capacityFlush && !viewportWork', '!capacityFlush && !viewportWork'),
        'omit_recapture': ('CaptureTriStateKey(&mTriState.key);', '(void)0;'),
    }
    before, after = replacements[mutation]
    assert source.count(before) == 1
    source = source.replace(before, after)

PREAMBLE = r'''
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stack>
#include <vector>
constexpr unsigned MAX_TRI_BUFFER = TRI_CAPACITY;
enum { CULL_FRONT = 1, CULL_BACK = 2, CULL_BOTH = 3 };
constexpr unsigned G_EX_INVERT_CULLING = 1;
constexpr unsigned G_TL_LOD = 1u << 16;
enum { G_CCMUX_PRIMITIVE = 3, G_CCMUX_SHADE = 4, G_CCMUX_ENVIRONMENT = 5,
       G_CCMUX_KEY_CENTER = 6, G_CCMUX_CONVERT_K4 = 7, G_CCMUX_KEY_SCALE = 8,
       G_CCMUX_PRIMITIVE_ALPHA = 10, G_CCMUX_ENV_ALPHA = 12,
       G_CCMUX_LOD_FRACTION = 13, G_CCMUX_PRIM_LOD_FRAC = 14,
       G_CCMUX_CONVERT_K5 = 15, G_ACMUX_PRIM_LOD_FRAC = 16 };
static unsigned get_attr(unsigned value) { return value; }
struct ShaderProgram { unsigned id; };
struct TextureCacheNode { unsigned id; };
ShaderProgram programs[2]{{1}, {2}};
TextureCacheNode textureNodes[4]{{1}, {2}, {3}, {4}};
struct GfxClipParameters { bool z_is_from_0_to_1 = false, invertY = false; };
struct F3DGfx { struct { uint32_t w0, w1; } words; };
#define C0(pos, width) ((cmd->words.w0 >> (pos)) & ((1U << width) - 1))
#define C1(pos, width) ((cmd->words.w1 >> (pos)) & ((1U << width) - 1))
enum class Soh3dsProfileSection { Triangle, TriangleKey, TriangleEmit };
struct Counts { unsigned triangle = 0, key = 0, emit = 0, derive = 0; };
static Counts* activeCounts;
struct Soh3dsProfileScope {
    Soh3dsProfileScope(Soh3dsProfileSection section) {
#ifndef BENCHMARK
        if (section == Soh3dsProfileSection::Triangle) ++activeCounts->triangle;
        if (section == Soh3dsProfileSection::TriangleKey) ++activeCounts->key;
        if (section == Soh3dsProfileSection::TriangleEmit) ++activeCounts->emit;
#else
        (void)section;
#endif
    }
};
''' + '\n'.join(structure(n) for n in ['XYWidthHeight', 'RGBA', 'LoadedVertex',
                                        'ColorCombiner', 'TriStateKey', 'TriStateCache']) + r'''
struct RSP {
    unsigned geometry_mode = 0, extra_geometry_mode = 0;
    LoadedVertex loaded_vertices[68]{};
};
struct RDP {
    uint64_t combine_mode = 0;
    unsigned other_mode_l = 0, other_mode_h = 0;
    uint8_t first_tile_index = 0;
    bool grayscale = false, textures_changed[2]{};
    TriStateKey::Tile texture_tile[8]{};
    struct Loaded {
        uint32_t orig_size_bytes = 32, size_bytes = 32, full_image_line_size_bytes = 8, line_size_bytes = 8;
        struct { float h_byte_scale = 1, v_pixel_scale = 1; } raw_tex_metadata;
        bool masked = false, blended = false;
    } loaded_texture[2];
    RGBA env_color{17,18,19,20}, prim_color{21,22,23,24}, fog_color{25,26,27,28},
         blend_color{29,30,31,32}, grayscale_color{33,34,35,36}, key_center{37,38,39,40},
         key_scale{41,42,43,44};
    uint8_t prim_lod_fraction = 57, convert_k[6]{1,2,3,4,5,6};
    uint16_t prim_depth = 1234;
    XYWidthHeight viewport{0,0,400,240}, scissor{0,0,400,240};
    bool viewport_or_scissor_changed = false;
};
struct RenderingState {
    uint8_t depth_test_and_mask = 0;
    bool decal_mode = false, alpha_blend = false;
    XYWidthHeight viewport{0,0,400,240}, scissor{0,0,400,240};
    ShaderProgram* mShaderProgram = &programs[0];
    TextureCacheNode* mTextures[2]{&textureNodes[0], &textureNodes[1]};
};
static uint32_t bits(float f) { return std::bit_cast<uint32_t>(f); }
static void keyWords(std::vector<uint64_t>& v, const TriStateKey& k) {
    v.insert(v.end(), {k.combine_mode, k.other_mode_l, k.other_mode_h, k.geometry_mode,
        k.extra_geometry_mode, uint64_t(k.shader_id), uint64_t(uintptr_t(k.shaderProgram)),
        k.first_tile_index, k.depth_test_and_mask, k.grayscale, k.decal_mode, k.alpha_blend});
    for (unsigned i=0; i<2; ++i) {
        const auto& t=k.tile[i]; const auto& l=k.loaded[i];
        v.insert(v.end(), {bits(t.uls),bits(t.ult),bits(t.lrs),bits(t.lrt),t.line_size_bytes,
            t.siz,t.cms,t.cmt,t.masks,t.maskt,t.shifts,t.shiftt,t.tmem_index,
            l.orig_size_bytes,l.size_bytes,l.full_image_line_size_bytes,l.line_size_bytes,
            bits(l.h_byte_scale),bits(l.v_pixel_scale),uint64_t(uintptr_t(k.textures[i])),
            k.masked[i],k.blended[i],k.textures_changed[i]});
    }
}
'''

CLASS = r'''
struct Interpreter;
struct Backend {
    Interpreter* owner;
    std::vector<uint64_t> calls;
    uint64_t checksum = 0;
    void SetCurrentPrimDepth(float value) {
#ifndef BENCHMARK
        calls.insert(calls.end(), {1,bits(value)});
#else
        (void)value;
#endif
    }
    void DrawTriangles(float* data, unsigned len, unsigned tris);
    void SetViewport(int x, int y, unsigned width, unsigned height) {
#ifndef BENCHMARK
        calls.insert(calls.end(), {3,uint64_t(x),uint64_t(y),width,height});
#endif
    }
    void SetScissor(int x, int y, unsigned width, unsigned height) {
#ifndef BENCHMARK
        calls.insert(calls.end(), {4,uint64_t(x),uint64_t(y),width,height});
#endif
    }
};
struct Interpreter {
    RSP rsp; RSP* mRsp = &rsp;
    RDP rdp; RDP* mRdp = &rdp;
    RenderingState mRenderingState;
    TriStateCache mTriState{};
    GfxClipParameters mClipParameters;
    std::stack<uint32_t> mShaderStack;
    ColorCombiner comb{};
    float mBufVbo[MAX_TRI_BUFFER*3*32]{};
    unsigned mBufVboLen = 0, mBufVboNumTris = 0;
    Counts counts;
    bool mutateAtFlush = false, mutateToNan = false, importPending = false;
    unsigned flushMutationCount = 0;
    Backend backend{this}; Backend* mRapi = &backend;
    void GfxSpTri1(uint8_t,uint8_t,uint8_t,bool);
    bool GfxSpTri1Impl(uint8_t,uint8_t,uint8_t,bool,bool);
    void CaptureTriStateKey(TriStateKey*);
    void EmitTriangle(LoadedVertex* const[3],bool);
    void Flush();
    void DeriveTriState() {
#ifndef BENCHMARK
        ++counts.derive;
        backend.calls.push_back(5);
#endif
        // Model the documented import side effects before post-derive capture.
        if (importPending) {
            Flush();
            rdp.loaded_texture[0].size_bytes += 64;
            rdp.loaded_texture[0].line_size_bytes += 8;
            rdp.loaded_texture[0].raw_tex_metadata.h_byte_scale *= 2;
            mRenderingState.mTextures[0] = &textureNodes[2];
            mRenderingState.mShaderProgram = &programs[1];
            importPending = false;
        }
        rdp.textures_changed[0] = rdp.textures_changed[1] = false;
        auto& ts=mTriState;
        ts.comb=&comb; ts.prg=mRenderingState.mShaderProgram;
        ts.numInputs=2; ts.usedTextures[0]=true; ts.usedTextures[1]=true;
        ts.tm=15; ts.use_alpha=true; ts.use_fog=(rdp.other_mode_h&1)!=0;
        ts.use_blend_color=(rdp.other_mode_h&2)!=0; ts.use_grayscale=rdp.grayscale;
        ts.linear_filter=true;
        for (unsigned i=0; i<2; ++i) {
            const unsigned tile=i==1&&rdp.first_tile_index>=2 ? rdp.first_tile_index : rdp.first_tile_index+i;
            ts.effective_tile[i]=tile;
            ts.uMul[i]=1.0f/(rdp.loaded_texture[i].size_bytes+1);
            ts.vMul[i]=ts.uMul[i]*0.5f;
            ts.uAdd[i]=rdp.texture_tile[tile].uls; ts.vAdd[i]=rdp.texture_tile[tile].ult;
            ts.halfU[i]=ts.uMul[i]*0.5f; ts.halfV[i]=ts.vMul[i]*0.5f;
            ts.clampS[i]=0.75f; ts.clampT[i]=0.875f;
        }
    }
    Interpreter() {
        comb.shader_input_mapping[0][0]=G_CCMUX_SHADE;
        comb.shader_input_mapping[1][0]=G_CCMUX_SHADE;
        comb.shader_input_mapping[0][1]=G_CCMUX_PRIMITIVE;
        comb.shader_input_mapping[1][1]=G_CCMUX_PRIMITIVE;
        for (unsigned i=0; i<68; ++i) {
            rsp.loaded_vertices[i]={float(i%3)-1.0f,float((i/3)%3)-1.0f,0.25f,1.0f,
                                    float(i*7),float(i*11),{uint8_t(i),128,192,255},0};
        }
        rsp.loaded_vertices[0].x=0; rsp.loaded_vertices[0].y=0;
        rsp.loaded_vertices[1].x=1; rsp.loaded_vertices[1].y=0;
        rsp.loaded_vertices[2].x=0; rsp.loaded_vertices[2].y=1;
        rsp.loaded_vertices[3].x=1; rsp.loaded_vertices[3].y=1;
        for (unsigned i=0; i<8; ++i) {
            rdp.texture_tile[i].tmem_index=i%2;
            rdp.texture_tile[i].lrs=31; rdp.texture_tile[i].lrt=31;
        }
    }
    void seed() {
        activeCounts=&counts;
        DeriveTriState(); CaptureTriStateKey(&mTriState.key); mTriState.valid=true;
        counts={}; backend.calls.clear();
    }
    std::vector<uint64_t> state() {
        std::vector<uint64_t> s;
        TriStateKey current; CaptureTriStateKey(&current);
        keyWords(s,current); keyWords(s,mTriState.key);
        const auto& t=mTriState;
        s.insert(s.end(), {t.valid,t.tm,t.numInputs,t.usedTextures[0],t.usedTextures[1],
            t.use_alpha,t.use_fog,t.use_blend_color,t.use_grayscale,t.linear_filter,
            uint64_t(uintptr_t(t.prg)),mBufVboLen,mBufVboNumTris,importPending,
            mutateAtFlush,mutateToNan,flushMutationCount,rdp.viewport_or_scissor_changed,
            mClipParameters.invertY,mClipParameters.z_is_from_0_to_1});
        for (unsigned i=0; i<2; ++i) s.insert(s.end(), {t.effective_tile[i],bits(t.uMul[i]),
            bits(t.vMul[i]),bits(t.uAdd[i]),bits(t.vAdd[i]),bits(t.halfU[i]),bits(t.halfV[i]),
            bits(t.clampS[i]),bits(t.clampT[i])});
        for (const XYWidthHeight* v : {&rdp.viewport,&rdp.scissor,&mRenderingState.viewport,&mRenderingState.scissor})
            s.insert(s.end(),{uint64_t(v->x),uint64_t(v->y),v->width,v->height});
        for (unsigned i=0; i<mBufVboLen; ++i) s.push_back(bits(mBufVbo[i]));
        for (const auto& v : rsp.loaded_vertices) s.insert(s.end(), {bits(v.x),bits(v.y),bits(v.z),
            bits(v.w),bits(v.u),bits(v.v),v.color.r,v.color.g,v.color.b,v.color.a,v.clip_rej});
        return s;
    }
};
static Interpreter* sInterpreterRaw;
static constexpr float N64_PRIM_DEPTH_MAX = 32767.0f;
__attribute__((noinline)) void Backend::DrawTriangles(float* data, unsigned len, unsigned tris) {
#ifndef BENCHMARK
    calls.insert(calls.end(), {2,len,tris});
    for (unsigned i=0; i<len; ++i) calls.push_back(bits(data[i]));
#else
    // Consume the actual buffer. No profiler counters or trace recording here.
    asm volatile("" : : "r"(data), "r"(len), "r"(tris) : "memory");
    checksum += bits(data[0]) + bits(data[len-1]);
#endif
    if (owner->mutateAtFlush) {
        ++owner->flushMutationCount;
        owner->rdp.loaded_texture[0].raw_tex_metadata.h_byte_scale = owner->mutateToNan
            ? std::numeric_limits<float>::quiet_NaN() : 3.0f;
        owner->rdp.textures_changed[0]=true;
        owner->importPending=true;
        owner->mutateAtFlush=false;
    }
}
'''


def implementation(text, namespace, candidate):
    parts = [function(text, 'void Interpreter::GfxSpTri1(')]
    if candidate:
        parts += [function(text, 'bool Interpreter::GfxSpTri1Impl(')]
    for signature in ['void Interpreter::CaptureTriStateKey(', 'void Interpreter::EmitTriangle(',
                      'void Interpreter::Flush(', 'bool gfx_tri2_handler_f3dex(',
                      'bool gfx_quad_handler_f3dex2(', 'bool gfx_quad_handler_f3dex(']:
        parts.append(function(text, signature))
    return '\nnamespace ' + namespace + ' {\n' + CLASS + '\n'.join(parts) + '\n}\n'


TEST = r'''
static F3DGfx command(unsigned encoding, unsigned a=0,unsigned b=1,unsigned c=2,unsigned d=3) {
    if (encoding==2) return {{0, (d*2<<24)|(a*2<<16)|(b*2<<8)|(c*2)}};
    return {{(a<<17)|(b<<9)|(c<<1), (b<<17)|(d<<9)|(c<<1)}};
}
template<class I> void run(I& i,unsigned encoding,F3DGfx cmd) {
    activeCounts=&i.counts; F3DGfx* p=&cmd;
    if constexpr (std::is_same_v<I, Original::Interpreter>) {
        Original::sInterpreterRaw=&i;
        if (encoding==0) Original::gfx_tri2_handler_f3dex(&p);
        if (encoding==1) Original::gfx_quad_handler_f3dex2(&p);
        if (encoding==2) Original::gfx_quad_handler_f3dex(&p);
    } else {
        Candidate::sInterpreterRaw=&i;
        if (encoding==0) Candidate::gfx_tri2_handler_f3dex(&p);
        if (encoding==1) Candidate::gfx_quad_handler_f3dex2(&p);
        if (encoding==2) Candidate::gfx_quad_handler_f3dex(&p);
    }
}
static void equal(Original::Interpreter& a,Candidate::Interpreter& b) {
    assert(a.backend.calls==b.backend.calls); // Includes exact flushed vertex bits.
    assert(a.state()==b.state());             // Includes buffered vertex bits and cached/live key bits.
    assert(a.counts.triangle==b.counts.triangle);
    assert(a.counts.emit==b.counts.emit);
    assert(a.counts.derive==b.counts.derive);
}
int main() {
    unsigned cases=0;
    for (unsigned encoding=0;encoding<3;++encoding) {
        for (unsigned scenario=0;scenario<18;++scenario) {
            Original::Interpreter a; Candidate::Interpreter b;
            auto setup=[&](auto& i) {
                i.seed();
                if (scenario==1) for(unsigned n:{0u,1u,2u}) i.rsp.loaded_vertices[n].clip_rej=1;
                if (scenario==2) i.mTriState.valid=false;
                if (scenario==3) { i.importPending=true; i.rdp.textures_changed[0]=true; }
                if (scenario==4 || scenario==5) {
                    i.mBufVboNumTris=MAX_TRI_BUFFER-1; i.mBufVboLen=12;
                    for(unsigned n=0;n<12;++n) i.mBufVbo[n]=n*0.1f;
                    i.mutateAtFlush=true; i.mutateToNan=scenario==5;
                }
                if (scenario==6) i.rdp.texture_tile[0].uls=std::numeric_limits<float>::quiet_NaN();
                if (scenario==7) i.mTriState.key.loaded[0].h_byte_scale=std::numeric_limits<float>::quiet_NaN();
                if (scenario==8) { i.rdp.texture_tile[0].uls=-0.0f; i.mTriState.key.tile[0].uls=0.0f; }
                if (scenario==9) {
                    i.rdp.viewport_or_scissor_changed=true; i.rdp.viewport.x=7; i.rdp.scissor.y=9;
                    i.mBufVboLen=12; i.mBufVboNumTris=1; i.mutateAtFlush=true;
                }
                if (scenario==10) { i.rsp.geometry_mode=CULL_BOTH; }
                if (scenario==11) { i.rsp.geometry_mode=CULL_FRONT; i.rsp.extra_geometry_mode=G_EX_INVERT_CULLING; }
                if (scenario==12) { i.rsp.geometry_mode=CULL_BACK; i.rsp.loaded_vertices[0].w=-1; }
                if (scenario==13) { i.rdp.first_tile_index=3; i.rdp.grayscale=true; i.rdp.other_mode_h=3; }
                if (scenario==14) { i.mClipParameters={true,true}; i.rdp.other_mode_h=1; }
                if (scenario==15) { i.mBufVboNumTris=MAX_TRI_BUFFER-2; i.mBufVboLen=12; i.mutateAtFlush=true; }
                if (scenario==16) { i.rdp.loaded_texture[0].raw_tex_metadata.h_byte_scale=std::numeric_limits<float>::quiet_NaN(); i.seed(); }
                if (scenario==17) { for(unsigned n:{encoding==2?0u:1u,2u,3u}) i.rsp.loaded_vertices[n].clip_rej=1; }
            };
            setup(a); setup(b);
            run(a,encoding,command(encoding)); run(b,encoding,command(encoding)); equal(a,b);
            if (scenario==0 || scenario==8) {
                assert(a.counts.key==2);
                assert(b.counts.key==(SOH3DS_TRIANGLE_PAIR_REUSE ? 1u : 2u));
                assert(b.counts.derive==0);
            }
            if (scenario==1 || scenario==17) assert(a.counts.emit==1 && b.counts.key==1);
            if (scenario==3) { assert(a.counts.derive==1); assert(b.counts.key==3); }
            if (scenario==4 || scenario==5) assert(a.counts.derive>=1 && b.counts.key==a.counts.key);
            if (scenario==6 || scenario==16) assert(a.counts.derive==2 && b.counts.key==4);
            // A following command must consult state after a direct public mutation.
            a.rdp.combine_mode^=0x123; b.rdp.combine_mode^=0x123;
            a.mShaderStack.push(42); b.mShaderStack.push(42);
            const unsigned previousDerives=a.counts.derive;
            run(a,encoding,command(encoding,4,5,6,7)); run(b,encoding,command(encoding,4,5,6,7)); equal(a,b);
            if (scenario==0 || scenario==8) assert(a.counts.derive==previousDerives+1);
            ++cases;
        }
    }
    // Independent randomized differential sequence with public state writes
    // between commands. The random source is identical for both interpreters.
    Original::Interpreter a; Candidate::Interpreter b; a.seed(); b.seed();
    uint32_t rng=0x7a3612u;
    for (unsigned n=0;n<3000;++n) {
        rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
        auto change=[&](auto& i) {
            if ((rng&7)==0) i.rdp.combine_mode=rng;
            if ((rng&31)==1) { i.rdp.first_tile_index=(rng>>8)%6; i.rdp.textures_changed[0]=true; i.importPending=true; }
            if ((rng&31)==2) i.rdp.texture_tile[i.rdp.first_tile_index].ult=float(int(rng%31)-15);
            if ((rng&31)==3) { i.rdp.viewport_or_scissor_changed=true; i.rdp.viewport.x=rng%100; }
            i.rsp.geometry_mode=(rng>>10)&3; i.rsp.extra_geometry_mode=(rng>>12)&1;
            i.mClipParameters={bool(rng&0x4000),bool(rng&0x8000)};
        };
        change(a);change(b);
        unsigned encoding=rng%3;
        auto cmd=command(encoding,(rng>>4)%20,(rng>>9)%20,(rng>>14)%20,(rng>>19)%20);
        run(a,encoding,cmd);run(b,encoding,cmd);equal(a,b);
    }
    std::printf("PASS: switch=%d, %u edge cases plus 3000 command differentials; exact vertex bits/backend calls/final state\n",
                SOH3DS_TRIANGLE_PAIR_REUSE,cases);
}
'''

BENCH = r'''
int main(int argc, char** argv) {
    Run::Interpreter i; i.seed(); Run::sInterpreterRaw=&i;
    const bool minimal=argc>1 && std::strcmp(argv[1],"minimal")==0;
    const bool single=argc>1 && std::strcmp(argv[1],"single")==0;
    const bool reject=argc>1 && std::strcmp(argv[1],"reject")==0;
    if (minimal) { i.mTriState.usedTextures[0]=i.mTriState.usedTextures[1]=false; i.mTriState.numInputs=0; }
    if (reject) for(unsigned n:{0u,1u,2u}) i.rsp.loaded_vertices[n].clip_rej=1;
    F3DGfx cmd{{(0<<17)|(1<<9)|(2<<1),(1<<17)|(3<<9)|(2<<1)}};
    F3DGfx* p=&cmd;
    for(unsigned n=0;n<10000;++n) {
        if(single) { i.GfxSpTri1(0,1,2,false); i.GfxSpTri1(1,3,2,false); }
        else Run::gfx_tri2_handler_f3dex(&p);
    }
    const auto start=std::chrono::steady_clock::now();
    for(unsigned n=0;n<1000000;++n) {
        if(single) { i.GfxSpTri1(0,1,2,false); i.GfxSpTri1(1,3,2,false); }
        else Run::gfx_tri2_handler_f3dex(&p);
    }
    const auto end=std::chrono::steady_clock::now();
    i.Flush();
    std::printf("%.6f %llu\n",std::chrono::duration<double,std::nano>(end-start).count()/1000000,
                (unsigned long long)i.backend.checksum);
}
'''

with tempfile.TemporaryDirectory(prefix='soh-triangle-pair-') as directory:
    p = Path(directory)
    flags = ['-std=c++20', '-O2', '-g', '-fno-fast-math']
    if os.getenv('SANITIZE'):
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
    compiler = os.getenv('CXX', 'g++')
    benchmark = bool(os.getenv('BENCHMARK'))
    if benchmark:
        assert not os.getenv('SANITIZE'), 'Sanitizers distort timings'
        cpu = int(os.getenv('BENCH_CPU', str(max(os.sched_getaffinity(0)))))
        os.sched_setaffinity(0, {cpu})
        print(f'host={os.uname().machine}, cpu={cpu}, workload={os.getenv("HOTPATH", "textured")}, '
              f'compiler={subprocess.check_output([compiler, "--version"], text=True).splitlines()[0]}, flags={flags}', flush=True)
    executables = []
    for enabled in ((-1, 0, 1) if benchmark else (0, 1)):
        code = PREAMBLE.replace('TRI_CAPACITY', re.search(r'constexpr size_t MAX_TRI_BUFFER = (\d+);', source).group(1))
        if benchmark:
            code += implementation(old if enabled == -1 else source, 'Run', enabled != -1) + BENCH
        else:
            code += implementation(old, 'Original', False) + implementation(source, 'Candidate', True) + TEST
        cpp = p / f'test-{enabled}.cpp'
        cpp.write_text(code)
        exe = p / f'test-{enabled}'
        subprocess.run([compiler, *flags, f'-DSOH3DS_TRIANGLE_PAIR_REUSE={enabled}',
                        *(['-DBENCHMARK'] if benchmark else []), str(cpp), '-o', str(exe)], check=True)
        executables.append(exe)
        if not benchmark:
            subprocess.run([str(exe)], check=True)
    if benchmark:
        results = [[], [], []]
        checksums = set()
        for trial in range(9):
            order = [(trial + offset) % 3 for offset in range(3)]
            if trial % 2:
                order.reverse()
            for index in order:
                output = subprocess.check_output([str(executables[index]), os.getenv('HOTPATH', 'textured')], text=True).split()
                results[index].append(float(output[0])); checksums.add(output[1])
        assert len(checksums) == 1, checksums
        for label, values in zip(('original', 'switch=0', 'switch=1'), results):
            print(f'{label} ns/pair: {values}; median={statistics.median(values):.3f}')
        print(f'host median reduction vs original: {(1-statistics.median(results[2])/statistics.median(results[0]))*100:.2f}%')
        print(f'host median reduction vs switch=0: {(1-statistics.median(results[2])/statistics.median(results[1]))*100:.2f}%')
