#!/usr/bin/env python3
"""Exercise production audio init/exit with both Old 3DS scheduling policies."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'third_party/shipwright/soh/soh/OTRGlobals.cpp').read_text()


def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


code = r'''
#include "soh/OTRAudio.h"
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cassert>
#include <vector>
#define __3DS__
OTRAudioSync audio;
using Soh3dsThreadHandle=void*;
static Soh3dsThreadHandle sSoh3dsAudioThread=nullptr;
static int sSoh3dsAudioCore=-1;
static uint32_t sSoh3dsAudioCpuLimit;
static int32_t sSoh3dsAudioCpuLimitResult;
static int mode, joins, frees;
static std::vector<int> limits, cores;
// Modes: 0 all creation fails; 1 Old; 2 New core2; 3 New core1 fallback;
// 4 Old core0 rejected; 5 model-query failure (must remain safe on Old);
// 6 Old with a package allowing an 80-percent core1 request.
void ResourceMgr_LoadDirectory(const char*){}
void OTRAudio_Thread(){}
void Soh3dsAudioThreadEntry(void*){}
int APT_CheckNew3DS(bool* b){if(mode==5)return -1;*b=mode==2||mode==3;return 0;}
int APT_SetAppCpuTimeLimit(uint32_t p){limits.push_back(p);return mode==6||p<=50?0:-1;}
int APT_GetAppCpuTimeLimit(uint32_t* p){*p=mode==6?80:30;return 0;}
Soh3dsThreadHandle threadCreate(void(*entry)(void*),void* arg,size_t stack,int prio,int core,bool detached){
 assert(entry==Soh3dsAudioThreadEntry&&arg==nullptr&&stack==128*1024&&prio==0x18&&!detached);
 assert(audio.running);
 cores.push_back(core);
 if(mode==0)return nullptr;
 if((core==0&&mode!=4)||(mode==2&&core==2)||
    (core==1&&!limits.empty()&&(limits.back()==30||(mode==6&&limits.back()==80))))
  return (void*)1;
 return nullptr;
}
int threadJoin(Soh3dsThreadHandle t,uint64_t timeout){
 assert(t==(void*)1&&timeout==UINT64_MAX&&!audio.running);++joins;return 0;
}
void threadFree(Soh3dsThreadHandle t){assert(t==(void*)1&&joins==1);++frees;}
''' + function('void OTRAudio_Init()') + '\n' + function('extern "C" void OTRAudio_Exit()') + r'''
int main(){
 for(mode=0;mode<7;mode++){
  limits.clear();cores.clear();joins=frees=0;
  OTRAudio_Init();
  const bool core0Attempt =
#ifdef SOH3DS_OLD_AUDIO_CORE0
    mode!=2&&mode!=3;
#else
    false;
#endif
  if(mode==0){assert(sSoh3dsAudioCore==0&&audio.thread.joinable());}
  else if(mode==2){assert(sSoh3dsAudioCore==2&&limits.empty());assert((cores==std::vector<int>{2}));}
  else if(core0Attempt&&mode!=4){
   assert(sSoh3dsAudioCore==0&&sSoh3dsAudioThread!=nullptr&&limits.empty());
   assert((cores==std::vector<int>{0}));
  }else if(mode==6){
   assert(sSoh3dsAudioCore==1&&sSoh3dsAudioCpuLimit==80&&sSoh3dsAudioCpuLimitResult==0);
   assert((limits==std::vector<int>{80})&&(cores==std::vector<int>{1}));
  }else{
   assert(sSoh3dsAudioCore==1&&sSoh3dsAudioCpuLimit==30&&sSoh3dsAudioCpuLimitResult==0);
   assert((limits==std::vector<int>{80,70,50,30}));
  }
  if(core0Attempt)assert(cores.front()==0);
  else for(int core:cores)assert(core!=0);
  auto attempts=cores.size();OTRAudio_Init();assert(cores.size()==attempts);
  OTRAudio_Exit();
  assert(!audio.running&&!audio.thread.joinable()&&sSoh3dsAudioThread==nullptr);
  assert(joins==(mode!=0)&&frees==joins);
 }
}
'''

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    for label, flags in [('default', []), ('core0', ['-DSOH3DS_OLD_AUDIO_CORE0'])]:
        subprocess.run(['g++', '-std=c++20', '-pthread',
                        '-I' + str(ROOT / 'third_party/shipwright/soh'), *flags,
                        str(path / 'test.cpp'), '-o', str(path / label)], check=True)
        subprocess.run([str(path / label)], check=True, timeout=5)
        print(f'PASS {label}: Old/New selection, allocation failures, model-query failure, restart and shutdown')
