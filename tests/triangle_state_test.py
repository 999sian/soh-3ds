#!/usr/bin/env python3
"""Compare the production live-state check with CaptureTriStateKey/operator==.

No handwritten key oracle: structs, capture, and candidate bodies come from the
interpreter. --candidate supports a proposed helper before production integration.
--benchmark records paired host timings; --arm emits an ARM fixture object.
Neither is a hardware frame-rate measurement. SANITIZE=1 enables ASan/UBSan.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LUS = ROOT / 'third_party/libultraship'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--candidate', type=Path)
parser.add_argument('--benchmark', action='store_true')
parser.add_argument('--arm', action='store_true')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
source = (LUS / 'src/fast/interpreter.cpp').read_text()
header = (LUS / 'include/fast/interpreter.h').read_text()


def function(text, signature):
    start = text.index(signature)
    return text[start:text.index('\n}', start) + 2]


def structure(name):
    start = header.index('struct ' + name + ' {')
    return header[start:header.index('\n};', start) + 3]


capture = function(source, 'void Interpreter::CaptureTriStateKey(')
candidate = function(args.candidate.read_text() if args.candidate else source,
                     'bool Interpreter::TriStateMatches(')
mutation = os.getenv('MUTATE')
if mutation:
    replacements = {
        'texture_flag': ('key.textures_changed[i] != mRdp->textures_changed[i]', 'false'),
        'texture_pointer': ('key.textures[i] != mRenderingState.mTextures[i]', 'false'),
        'float': ('cached.uls != src.uls', 'false'),
    }
    before, after = replacements[mutation]
    assert before in candidate, 'mutation no longer matches candidate'
    candidate = candidate.replace(before, after)

PREAMBLE = r'''
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stack>
#include <string>
#include <type_traits>
#include "fast/lus_gbi.h"
using namespace Fast;
namespace Fast { class Texture; enum class TextureType; }
struct ShaderProgram { unsigned id; };
struct TextureCacheNode { unsigned id; };
static ShaderProgram programs[2]{};
static TextureCacheNode textures[2]{};
#define MAX_LIGHTS 32
#define MAX_VERTICES 64
#define SHADER_MAX_TEXTURES 6
''' + '\n'.join(structure(name) for name in [
    'XYWidthHeight', 'RGBA', 'LoadedVertex', 'RawTexMetadata', 'RSP', 'RDP',
    'RenderingState', 'TriStateKey']) + r'''
struct Interpreter {
    RSP rsp{}; RDP rdp{};
    RSP* mRsp = &rsp; RDP* mRdp = &rdp;
    RenderingState mRenderingState{};
    struct { TriStateKey key{}; bool valid = true; } mTriState;
    std::stack<uint32_t> mShaderStack;
    void CaptureTriStateKey(TriStateKey*);
    bool TriStateMatches() const;
    Interpreter() {
        mRenderingState.mShaderProgram = &programs[0];
        mRenderingState.mTextures[0] = &textures[0];
        mRenderingState.mTextures[1] = &textures[1];
        for (unsigned i = 0; i < 8; ++i) {
            rdp.texture_tile[i].tmem_index = i & 1;
            rdp.texture_tile[i].lrs = 31.0f;
            rdp.texture_tile[i].lrt = 15.0f;
        }
        Seed();
    }
    void Seed() { CaptureTriStateKey(&mTriState.key); }
};
''' + capture + '\n' + candidate + r'''
extern "C" __attribute__((noinline)) bool BaselineMatches(Interpreter* interpreter) {
    TriStateKey key;
    interpreter->CaptureTriStateKey(&key);
    return interpreter->mTriState.valid && interpreter->mTriState.key == key;
}
extern "C" __attribute__((noinline)) bool FusedMatches(Interpreter* interpreter) {
    return interpreter->mTriState.valid && interpreter->TriStateMatches();
}
static unsigned checks = 0;
static void Check(Interpreter& state, bool expected, const char* label) {
    const bool baseline = BaselineMatches(&state);
    const bool fused = FusedMatches(&state);
    if (baseline != expected || fused != baseline) {
        std::fprintf(stderr, "%s: expected=%d baseline=%d fused=%d\n", label, expected, baseline, fused);
        std::exit(1);
    }
    ++checks;
}
template <typename T> void Change(T& value) {
    if constexpr (std::is_same_v<T, bool>) value = !value;
    else if constexpr (std::is_same_v<T, float>) value += 1.0f;
    else value ^= 1;
}
'''

# Explicit field inventory is independent of the candidate's comparisons.
fields = [(f's.{owner}.{name}', f's.mTriState.key.{key}') for owner, name, key in [
    ('rdp', 'combine_mode', 'combine_mode'), ('rdp', 'other_mode_l', 'other_mode_l'),
    ('rdp', 'other_mode_h', 'other_mode_h'), ('rsp', 'geometry_mode', 'geometry_mode'),
    ('rsp', 'extra_geometry_mode', 'extra_geometry_mode'),
    ('rdp', 'first_tile_index', 'first_tile_index'),
    ('mRenderingState', 'depth_test_and_mask', 'depth_test_and_mask'),
    ('rdp', 'grayscale', 'grayscale'), ('mRenderingState', 'decal_mode', 'decal_mode'),
    ('mRenderingState', 'alpha_blend', 'alpha_blend')]]
floats = []
for slot in range(2):
    for field in ['uls', 'ult', 'lrs', 'lrt', 'line_size_bytes', 'siz', 'cms', 'cmt',
                  'masks', 'maskt', 'shifts', 'shiftt', 'tmem_index']:
        pair = (f's.rdp.texture_tile[{slot}].{field}', f's.mTriState.key.tile[{slot}].{field}')
        fields.append(pair)
        if field in ('uls', 'ult', 'lrs', 'lrt'):
            floats.append(pair)
    for field in ['orig_size_bytes', 'size_bytes', 'full_image_line_size_bytes',
                  'line_size_bytes', 'h_byte_scale', 'v_pixel_scale']:
        live = 'raw_tex_metadata.' + field if field.endswith('scale') else field
        pair = (f's.rdp.loaded_texture[{slot}].{live}', f's.mTriState.key.loaded[{slot}].{field}')
        fields.append(pair)
        if field.endswith('scale'):
            floats.append(pair)
    for field in ['masked', 'blended']:
        fields.append((f's.rdp.loaded_texture[{slot}].{field}', f's.mTriState.key.{field}[{slot}]'))
    fields.append((f's.rdp.textures_changed[{slot}]', f's.mTriState.key.textures_changed[{slot}]'))

checks = []
for live, cached in fields:
    for changed in (live, cached):
        checks.append('{ Interpreter s; Change(' + changed + '); Check(s, false, "' + changed + '"); }')
for live, cached in floats:
    checks.append('''{ Interpreter s;
        LIVE = -0.0f; s.Seed(); LIVE = 0.0f; Check(s, true, "signed zero");
        for (uint32_t word : {0x7f800000u, 0xff800000u, 0x7fc00001u, 0x7fc12345u}) {
            LIVE = std::bit_cast<float>(word); s.Seed();
            Check(s, word == 0x7f800000u || word == 0xff800000u, "infinity/NaN self comparison");
            LIVE = 1.0f; Check(s, false, "cached infinity/NaN versus finite");
            s.Seed(); LIVE = std::bit_cast<float>(word); Check(s, false, "live infinity/NaN versus finite");
        }
    }'''.replace('LIVE', live))

TEST = r'''
static void Differential() {
    { Interpreter s; Check(s, true, "initial hit"); }
''' + '\n'.join(checks) + r'''
    { Interpreter s; s.mShaderStack.push(2); Check(s, false, "shader stack change");
      s.Seed(); Check(s, true, "captured shader stack");
      s.mShaderStack.pop(); Check(s, false, "empty shader stack"); }
    { Interpreter s; ++s.mTriState.key.shader_id; Check(s, false, "cached shader id"); }
    for (unsigned i = 0; i < 2; ++i) {
        Interpreter s;
        s.mRenderingState.mTextures[i] = nullptr; Check(s, false, "texture unbind");
        s.Seed(); Check(s, true, "captured null texture");
        s.mRenderingState.mTextures[i] = &textures[1-i]; Check(s, false, "texture rebind");
        s.Seed(); s.mTriState.key.textures[i] = &textures[i]; Check(s, false, "cached texture pointer");
    }
    { Interpreter s; s.mRenderingState.mShaderProgram = nullptr; Check(s, false, "shader clear");
      s.Seed(); Check(s, true, "captured null shader");
      s.mRenderingState.mShaderProgram = &programs[1]; Check(s, false, "shader rebind");
      s.Seed(); s.mTriState.key.shaderProgram = &programs[0]; Check(s, false, "cached shader pointer"); }
    { Interpreter s; s.mTriState.valid = false; Check(s, false, "external invalidation");
      s.mTriState.valid = true; Check(s, true, "valid cache"); }
    for (unsigned first = 0; first < 8; ++first) {
        for (unsigned mapping = 0; mapping < 4; ++mapping) {
            Interpreter s;
            s.rdp.first_tile_index = first;
            const unsigned second = first >= 2 ? first : first + 1;
            s.rdp.texture_tile[first].tmem_index = mapping & 1;
            s.rdp.texture_tile[second].tmem_index = (mapping >> 1) & 1;
            s.rdp.loaded_texture[0].raw_tex_metadata.h_byte_scale = 2.0f;
            s.rdp.loaded_texture[1].raw_tex_metadata.h_byte_scale = 4.0f;
            s.rdp.loaded_texture[0].masked = true;
            s.Seed(); Check(s, true, "selected tiles and TMEM mapping");
            s.rdp.loaded_texture[0].masked = false; Check(s, false, "physical masked slot");
            s.Seed(); Check(s, true, "post-import recapture");
            s.rdp.loaded_texture[s.rdp.texture_tile[first].tmem_index].size_bytes += 64;
            Check(s, false, "mapped size mutation");
            s.Seed(); Check(s, true, "post-import dimension repair");
        }
    }
    // These inputs are intentionally absent from the existing key contract.
    { Interpreter s; ++s.rdp.texture_tile[0].fmt; ++s.rdp.texture_tile[0].palette;
      ++s.rdp.texture_tile[0].tmem; ++s.rdp.loaded_texture[0].tex_flags;
      Check(s, true, "non-key fields preserve current semantics"); }
    std::printf("triangle state differential passed: %u checks, %zu-byte key\n", checks, sizeof(TriStateKey));
}
static double Time(bool fused, unsigned scenario, uint64_t* result) {
    Interpreter state;
    if (scenario == 1) ++state.rdp.combine_mode;
    if (scenario == 2) state.rdp.textures_changed[1] = true;
    if (scenario == 4) state.mTriState.valid = false;
    constexpr unsigned iterations = 1000000;
    uint64_t hits = 0;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < iterations; ++i) {
        if (scenario == 3) state.rdp.textures_changed[1] = (i & 7) == 0;
        asm volatile("" : : "g"(&state) : "memory");
        hits += fused ? FusedMatches(&state) : BaselineMatches(&state);
    }
    const auto stop = std::chrono::steady_clock::now();
    const uint64_t expected = scenario == 0 ? iterations : scenario == 3 ? iterations * 7 / 8 : 0;
    if (hits != expected) { std::fprintf(stderr, "benchmark result invalid\n"); std::exit(1); }
    *result = hits;
    return std::chrono::duration<double, std::nano>(stop - start).count() / iterations;
}
int main(int argc, char**) {
    Differential();
    if (argc > 1) {
        for (unsigned scenario = 0; scenario < 5; ++scenario) {
            for (unsigned repeat = 0; repeat < 9; ++repeat) {
                double times[2]; uint64_t result;
                for (unsigned order = 0; order < 2; ++order) {
                    const unsigned fused = order ^ (repeat & 1);
                    times[fused] = Time(fused, scenario, &result);
                }
                std::printf("bench %u %u %.6f %.6f\n", scenario, repeat, times[0], times[1]);
            }
        }
    }
}
'''

with tempfile.TemporaryDirectory(prefix='soh-triangle-state-') as temporary:
    directory = args.output or Path(temporary)
    directory.mkdir(parents=True, exist_ok=True)
    fixture = directory / 'fixture.cpp'
    fixture.write_text(PREAMBLE + TEST)
    flags = ['-std=c++20', '-O3', '-fno-fast-math', '-ffp-contract=off',
             '-ffunction-sections', '-fdata-sections', '-I', str(LUS / 'include')]
    if os.getenv('SANITIZE'):
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
    binary = directory / 'fixture'
    command = [os.environ.get('CXX', 'g++'), *flags, str(fixture), '-o', str(binary)]
    subprocess.run(command, check=True)
    output = subprocess.check_output([str(binary)] + (['benchmark'] if args.benchmark else []), text=True)
    print(output, end='')
    if args.output:
        (directory / 'host-output.txt').write_text(output)
        metadata = {'host_command': command, 'capture_sha256': hashlib.sha256(capture.encode()).hexdigest(),
                    'candidate_sha256': hashlib.sha256(candidate.encode()).hexdigest(),
                    'candidate_source': str(args.candidate) if args.candidate else 'production'}
        if args.benchmark:
            samples = {}
            for line in output.splitlines():
                if line.startswith('bench '):
                    _, scenario, repeat, baseline, fused = line.split()
                    samples.setdefault(scenario, []).append([float(baseline), float(fused)])
            metadata['host_ns_per_check'] = {scenario: {
                'baseline_median': statistics.median(v[0] for v in rows),
                'fused_median': statistics.median(v[1] for v in rows),
                'paired_ratio_median': statistics.median(v[1] / v[0] for v in rows)
            } for scenario, rows in samples.items()}
        if args.arm:
            prefix = Path(os.environ.get('DEVKITPRO', str(Path.home() / 'dkp-root/opt/devkitpro')))
            compiler = prefix / 'devkitARM/bin/arm-none-eabi-g++'
            obj = directory / 'fixture-arm.o'
            arm_command = [str(compiler), *flags, '-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard',
                           '-mtp=soft', '-mword-relocations', '-c', str(fixture), '-o', str(obj)]
            subprocess.run(arm_command, check=True)
            metadata['arm_command'] = arm_command
            for tool, extra, name in [('nm', ['-S', '--size-sort', '-C'], 'arm-symbols.txt'),
                                      ('objdump', ['-d', '-C'], 'arm-disassembly.txt')]:
                text = subprocess.check_output([str(prefix / ('devkitARM/bin/arm-none-eabi-' + tool)),
                                                *extra, str(obj)], text=True)
                (directory / name).write_text(text)
        (directory / 'results.json').write_text(json.dumps(metadata, indent=2) + '\n')
