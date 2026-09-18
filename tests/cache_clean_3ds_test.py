#!/usr/bin/env python3
"""Exercise the production cache-clean fallback at the kernel/service boundary."""
import os, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    (p/'3ds.h').write_text('''#pragma once
#include <cstdint>
#include <cstddef>
using u32=uint32_t; using Result=int;
constexpr unsigned CUR_PROCESS_HANDLE=7; constexpr uint64_t SYSCLOCK_ARM11=268123480;
#define R_SUCCEEDED(x) ((x)>=0)
uint64_t svcGetSystemTick();
Result svcStoreProcessDataCache(unsigned,u32,u32);
Result GSPGPU_FlushDataCache(const void*,u32);
Result svcInvalidateProcessDataCache(unsigned,u32,u32);
Result GSPGPU_InvalidateDataCache(const void*,u32);
''')
    (p/'test.cpp').write_text(r'''
#include "system_3ds.h"
#include <cassert>
#include <cstring>
#include <limits>
int directCalls=0, fallbackCalls=0, directResult=0, fallbackResult=0;
int directInvCalls=0, fallbackInvCalls=0;
uint64_t svcGetSystemTick(){return 1;}
int svcStoreProcessDataCache(unsigned process,uint32_t address,uint32_t size) {
 assert(process==7 && address==0x1000 && size==128); ++directCalls;return directResult;
}
int GSPGPU_FlushDataCache(const void* address,uint32_t size) {
 assert(address==reinterpret_cast<void*>(0x1000) && size==128);++fallbackCalls;return fallbackResult;
}
int svcInvalidateProcessDataCache(unsigned process,uint32_t address,uint32_t size) {
 assert(process==7 && address==0x1000 && size==128); ++directInvCalls;return 0;
}
int GSPGPU_InvalidateDataCache(const void* address,uint32_t size) {
 assert(address==reinterpret_cast<void*>(0x1000) && size==128);++fallbackInvCalls;return 0;
}
int main() {
 auto* address=reinterpret_cast<void*>(0x1000);
 assert(Soh3dsCleanDataCache(nullptr,128));assert(Soh3dsCleanDataCache(address,0));
 assert(directCalls==0 && fallbackCalls==0);
 assert(!Soh3dsCleanDataCache(address,std::numeric_limits<size_t>::max()));
 assert(Soh3dsCleanDataCache(address,128));assert(directCalls==1 && fallbackCalls==0);
 directResult=-1;
 assert(Soh3dsCleanDataCache(address,128));assert(directCalls==2 && fallbackCalls==1);
 assert(Soh3dsCleanDataCache(address,128));assert(directCalls==2 && fallbackCalls==2);
 fallbackResult=-1;assert(!Soh3dsCleanDataCache(address,128));
 assert(!strcmp(Soh3dsDataCacheMode(),"gsp-fallback"));
 assert(Soh3dsInvalidateDataCache(nullptr,128));assert(Soh3dsInvalidateDataCache(address,0));
 assert(!Soh3dsInvalidateDataCache(address,std::numeric_limits<size_t>::max()));
 assert(Soh3dsInvalidateDataCache(address,128));
}
''')
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++20','-I'+d,'-I'+str(ROOT/'platform/3ds/include'),str(p/'test.cpp'),str(ROOT/'platform/3ds/source/system_3ds.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
