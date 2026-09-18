#!/usr/bin/env python3
"""Check deferred tracker logic initialization across Old/New and save types."""
from pathlib import Path
import subprocess, tempfile, os
root=Path(__file__).resolve().parents[1]
s=(root/'third_party/shipwright/soh/soh/Enhancements/randomizer/randomizer_check_tracker.cpp').read_text()
body=s[s.index('static bool trackerLogicPending'):s.index('void CheckTrackerLoadGame')]
code=r'''
#include <cassert>
static bool oldModel, rando;
#define IS_RANDO rando
bool Soh3dsUsesOldProfile(){return oldModel;}
static int builds,overrides;
void RegionTable_Init(){++builds;}
namespace Rando { struct Context {
 static Context* GetInstance(){static Context c;return &c;}
 Context* GetEntranceShuffler(){return this;}
 void ApplyEntranceOverrides(){++overrides;}
}; }
''' + body + r'''
int main(){
 oldModel=true;rando=false;PrepareTrackerLogic();
#ifdef __3DS__
 assert(builds==0 && trackerLogicPending);
#else
 assert(builds==1 && !trackerLogicPending);
#endif
 EnsureTrackerLogic();assert(builds==1 && overrides==1 && !trackerLogicPending);
 EnsureTrackerLogic();assert(builds==1);
 oldModel=false;PrepareTrackerLogic();assert(builds==2 && overrides==2);
 oldModel=true;rando=true;PrepareTrackerLogic();assert(builds==3 && overrides==3);
}
'''
with tempfile.TemporaryDirectory(prefix='soh-tracker-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 for flags in ([],['-D__3DS__']):
  subprocess.run([os.environ.get('CXX','g++'),'-std=c++20',*flags,str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
