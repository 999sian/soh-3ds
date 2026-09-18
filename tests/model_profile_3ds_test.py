#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
code='''#include "model_profile_3ds.h"
static_assert(Soh3dsLinearHeapBytes(64ULL*1024*1024)==9*1024*1024);
static_assert(Soh3dsLinearHeapBytes(96ULL*1024*1024)==9*1024*1024);
static_assert(Soh3dsLinearHeapBytes(124ULL*1024*1024)==16*1024*1024);
static_assert(Soh3dsLinearHeapBytes(178ULL*1024*1024)==16*1024*1024);
static_assert(Soh3dsUseOldProfile(true,false));
static_assert(!Soh3dsUseOldProfile(true,true));
static_assert(Soh3dsUseOldProfile(false,true));
int main(){}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-I'+str(root/'platform/3ds/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
