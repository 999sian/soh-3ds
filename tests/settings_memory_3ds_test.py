#!/usr/bin/env python3
"""Grow real menu column storage under a fragmented-heap allocation ceiling."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'third_party/shipwright/soh/soh/SohGui/MenuTypes.h').read_text()
sidebar = re.search(r'struct SidebarEntry \{.*?\n\};', source, re.S).group()
code = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <new>
#include <vector>
static bool limitAllocations;
static size_t largestRequest;
void* operator new(size_t n) {
    if (limitAllocations) {
        if (n > largestRequest) largestRequest = n;
        if (n > 4096) throw std::bad_alloc();
    }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
// Representative inline widget data: exercise column ownership and indexing
// independently of UI callbacks and their separate small allocations.
struct WidgetInfo { uint32_t id; char payload[252]{}; };
''' + sidebar + r'''
int main() {
    SidebarEntry sidebar{.columnCount = 3};
    sidebar.columnWidgets.resize(3);
    for (auto& column : sidebar.columnWidgets) column.push_back({.id = 0});
    WidgetInfo* first = &sidebar.columnWidgets[0][0];
#ifdef __3DS__
    limitAllocations = true;
#endif
    // Previously, growing a column required successively larger contiguous
    // arrays while retaining the old array. Small heap holes must be enough.
    for (uint32_t i = 1; i < 256; ++i) {
        for (auto& column : sidebar.columnWidgets) column.push_back({.id = i});
    }
    limitAllocations = false;
    for (auto& column : sidebar.columnWidgets) {
        assert(column.size() == 256);
        for (uint32_t i = 0; i < 256; ++i) assert(column.at(i).id == i);
    }
#ifdef __3DS__
    assert(largestRequest <= 4096);
    assert(first == &sidebar.columnWidgets[0][0] && first->id == 0);
#endif
    puts("Menu column growth, row order and indexed access passed");
}
'''
with tempfile.TemporaryDirectory(prefix='soh-settings-memory-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    for flags in ([], ['-D__3DS__']):
        subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++20', '-O2', *flags,
                        str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
        subprocess.run([str(p / 'test')], check=True)
