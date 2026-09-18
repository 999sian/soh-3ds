#!/usr/bin/env python3
"""Compare compact snapshots to the immutable full-buffer implementation.

Allocation instrumentation checks the optimization's actual byte budget and
reuse/failure behaviour, without depending on private storage fields.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CPP = r'''
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include "depth_snapshot_3ds.h"
#define DepthSnapshot3DS DepthSnapshotBefore
#include "depth_snapshot_before.h"
#undef DepthSnapshot3DS

bool track = false, failAllocation = false;
size_t allocated = 0, allocations = 0, freed = 0;
void* operator new(size_t bytes) {
    if (track) { allocated += bytes; ++allocations; }
    if (failAllocation) throw std::bad_alloc();
    if (auto p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (track && p) ++freed; std::free(p); }
void operator delete(void* p, size_t) noexcept { ::operator delete(p); }
void expect(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s (allocated=%zu, allocations=%zu)\n", message, allocated, allocations); std::exit(1); }
}
void beginTrack() { allocated = allocations = freed = 0; track = true; }
struct Shape { uint32_t w, h, cw, ch; bool rotate; };
uint64_t comparisons = 0;
int main() {
    // RED on old implementation: 524288 backing bytes exceed 307200 content bytes.
    std::vector<uint32_t> source(1024 * 1024);
    uint32_t seed = 0x837153ab;
    for (auto& pixel : source) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; pixel = seed; }
    DepthSnapshot3DS compact;
    compact.requested = true;
    beginTrack();
    expect(compact.Capture(source.data(), 512, 256, 320, 240, false), "capture content");
    expect(allocated == 320 * 240 * sizeof(uint32_t) && allocations == 1, "allocate only 300 KiB content tiles");
    beginTrack();
    expect(compact.Capture(source.data(), 512, 256, 320, 240, false), "repeat capture");
    expect(allocations == 0, "reuse repeated capture allocation");
    expect(compact.Capture(source.data(), 512, 256, 160, 120, false), "shrink capture");
    expect(allocations == 0, "reuse capacity on shrink");
    track = false;
    const Shape shapes[] = {
        {512,256,320,240,false}, {256,512,400,240,true}, {240,400,400,240,true},
        {512,256,400,240,false}, {512,512,319,237,false}, {256,512,319,237,true},
        {8,8,1,1,false}, {8,8,1,1,true}, {16,24,9,17,false}, {24,16,9,17,true},
        {1024,1024,1023,1021,false}, {1024,1024,1023,1021,true},
        {1024,8,1024,8,false}, {8,1024,1024,8,true}, {24,16,24,16,false},
    };
    DepthSnapshotBefore before;
    for (auto s : shapes) {
        expect(compact.Capture(source.data(),s.w,s.h,s.cw,s.ch,s.rotate), "compact valid dimensions");
        expect(before.Capture(source.data(),s.w,s.h,s.cw,s.ch,s.rotate), "oracle valid dimensions");
        // Every content pixel can be queried, including tiles partly in padding.
        for (uint32_t py=0; py<s.ch; ++py) for(uint32_t px=0; px<s.cw; ++px) {
            float x=(px+.25f)*400.f/s.cw, y=(py+.25f)*240.f/s.ch;
            expect(compact.Sample(x,y)==before.Sample(x,y), "content pixel equals full-buffer oracle"); ++comparisons;
        }
        for (int y=0;y<240;++y) for(int x=0;x<400;++x) {
            expect(compact.Sample(x,y)==before.Sample(x,y), "screen grid equals oracle"); ++comparisons;
        }
        const float xs[] = {-0.f, 0.f, .001f, 7.999f, 200.5f, std::nextafter(400.f,0.f), 400.f, -1.f,
                            std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
        const float ys[] = {-0.f, 0.f, .001f, 7.999f, 120.5f, std::nextafter(240.f,0.f), 240.f, -1.f,
                            -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
        for(float x:xs) for(float y:ys) {
            expect(compact.Sample(x,y)==before.Sample(x,y), "fractional and invalid coordinates equal oracle"); ++comparisons;
        }
    }
    // Snapshot must own a completed copy: subsequent source writes do not change it.
    const auto saved = compact.Sample(200,120);
    std::memset(source.data(),0,source.size()*sizeof(uint32_t));
    expect(compact.Sample(200,120)==saved, "source changes do not change captured frame");
    expect(compact.requested, "Capture preserves request");
    beginTrack(); compact.Reset();
    expect(freed==1 && allocations==0, "Reset releases retained memory"); track=false;
    expect(!compact.requested && compact.Sample(200,120)==DepthSnapshot3DS::Far, "Reset invalidates and clears request");
    const Shape invalid[]={{0,8,1,1,false},{8,0,1,1,false},{1032,8,1,1,false},{8,1032,1,1,true},
        {9,8,1,1,false},{8,9,1,1,true},{8,8,0,1,false},{8,8,1,0,true},
        {16,8,17,8,false},{16,8,16,9,false},{16,8,9,16,true},{16,8,8,17,true}};
    for(auto s:invalid) {
        expect(compact.Capture(source.data(),8,8,8,8,false), "recapture before invalid input");
        expect(!compact.Capture(source.data(),s.w,s.h,s.cw,s.ch,s.rotate), "reject invalid shape");
        expect(compact.Sample(1,1)==DepthSnapshot3DS::Far, "invalid capture clears validity");
    }
    expect(!compact.Capture(nullptr,8,8,8,8,false), "reject null source");
    compact.Reset();
    expect(compact.Capture(source.data(),8,8,8,8,false), "small valid capture before failure");
    failAllocation=true;
    expect(!compact.Capture(source.data(),512,256,320,240,false), "growth allocation failure reported");
    failAllocation=false;
    expect(compact.Sample(200,120)==DepthSnapshot3DS::Far, "failed allocation invalidates old snapshot");
    beginTrack();
    expect(compact.Capture(source.data(),512,256,320,240,false), "recover after allocation failure");
    expect(allocated==307200 && allocations==1, "growth allocates exact tile byte count");
    track=false;
    // Increasing content should not trigger vector geometric over-allocation.
    beginTrack();
    expect(compact.Capture(source.data(),512,256,400,240,false), "grow to widescreen content");
    expect(allocated==384000 && allocations==1, "growth has no excess capacity"); track=false;
    expect(compact.Sample(0,0)==DepthSnapshot3DS::Far, "zero D24 is far");
    std::fill(source.begin(),source.end(),0xA5FFFFFFU);
    expect(compact.Capture(source.data(),512,256,320,240,false), "stencil fixture capture");
    expect(compact.Sample(399,239)==0, "D24 inversion ignores stencil byte");
    std::printf("depth snapshot: %llu oracle comparisons; exact tile allocations, reuse, growth, failure, reset pass\n",
                static_cast<unsigned long long>(comparisons));
}
'''

with tempfile.TemporaryDirectory(prefix='soh-depth-snapshot-') as temporary:
    temporary = Path(temporary)
    (temporary / 'test.cpp').write_text(CPP)
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if os.getenv('DEPTH_SANITIZE') else []
    subprocess.run([os.getenv('CXX', 'g++'), '-std=c++17', '-O1', *flags,
                    '-I'+str(ROOT / 'platform/3ds/include'), '-I'+str(ROOT / 'tests/fixtures'),
                    str(temporary / 'test.cpp'), '-o', str(temporary / 'test')], check=True)
    subprocess.run([str(temporary / 'test')], check=True)
