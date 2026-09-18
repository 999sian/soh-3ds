#!/usr/bin/env python3
"""Catch swapped stage attribution, disabled I/O, and capture-window carryover."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def function(path, signature):
    source = (ROOT/path).read_text()
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

with tempfile.TemporaryDirectory() as temp:
    p = Path(temp)
    (p / '3ds.h').write_text(r'''
#include <stdint.h>
using u32=uint32_t; using s32=int32_t;
constexpr u32 CUR_THREAD_HANDLE=0xffff8000;
extern uint64_t testTick; extern unsigned clockReads;
inline uint64_t svcGetSystemTick(){++clockReads;return testTick;}
inline int svcGetProcessorID(){return 1;}
inline int svcGetThreadPriority(s32* p,u32){*p=24;return 0;}
inline int APT_GetAppCpuTimeLimit(u32* p){*p=30;return 0;}
''')
    (p / 'test.cpp').write_text(r'''
#include "ship/utils/audio_profile_3ds.h"
#include "ship/utils/logging_3ds.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
uint64_t testTick=0; unsigned clockReads=0,flags=0;
extern "C" unsigned Soh3dsLoggingFlags(){return flags;}
std::string contents(){
 FILE* f=fopen("audio-perf.log","rb");if(!f)return {};
 std::string s;char b[1024];size_t n;
 while((n=fread(b,1,sizeof(b),f)))s.append(b,n);fclose(f);return s;
}
void batch(bool enabled){
 Soh3dsAudioProfileBegin(528,enabled);
 testTick+=268112;Soh3dsAudioProfileMark(SOH3DS_AUDIO_LOADS);
 testTick+=536224;Soh3dsAudioProfileMark(SOH3DS_AUDIO_COMMANDS);
 testTick+=804336;Soh3dsAudioProfileMark(SOH3DS_AUDIO_SEQUENCE);
 Soh3dsAudioProfileCount(SOH3DS_AUDIO_ADPCM,2);
 testTick+=1072448;Soh3dsAudioProfileMark(SOH3DS_AUDIO_MIXING);
 testTick+=1340560;Soh3dsAudioProfileEnd();
}
int main(){
 for(int i=0;i<34;i++)batch(false);
 Soh3dsAudioProfileReport();assert(clockReads==0 && contents().empty());
 flags=SOH3DS_LOG_GENERAL;
 for(int i=0;i<34;i++)batch(true);
 // Two sampled batches: 1/2/3/4/5 ms in the five phases; 15ms total.
 Soh3dsAudioProfileReport();auto s=contents();
 assert(s.find("samples=2 frames=1056")!=std::string::npos);
 assert(s.find("loadsUs=2000 commandsUs=4000 sequenceUs=6000 mixingUs=8000 tailUs=10000")!=std::string::npos);
 assert(s.find("synthUs=30000 budgetUs=33000 maxUs=15000")!=std::string::npos);
 assert(s.find("adpcm=4")!=std::string::npos);
 auto reads=clockReads;assert(reads==12);
 // Switching capture off discards partial stats and prevents all timing/I/O.
 batch(true);flags=0;batch(false);Soh3dsAudioProfileReport();
 assert(contents()==s && clockReads==reads+6);
 flags=SOH3DS_LOG_GENERAL;batch(true);Soh3dsAudioProfileReport();
 auto now=contents();assert(now.find("samples=1 frames=528")!=std::string::npos);
 assert(now.find("adpcm=2")!=std::string::npos);
 puts("PASS: sampled stage attribution, tick conservation, disabled no-I/O, capture reset");
}
''')
    subprocess.run(['c++', '-std=c++17', '-DSOH3DS_AUDIO_PROFILE', '-I'+str(p),
                    '-I'+str(ROOT/'src'), '-I'+str(ROOT/'third_party/libultraship/include'),
                    str(p/'test.cpp'), str(ROOT/'src/compat3ds/audio_profile_3ds.cpp'),
                    '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], cwd=p, check=True)
    # Execute actual stage boundaries around deterministic engine-operation
    # substitutes. A missing/misplaced marker must misattribute elapsed time.
    driver = (p/'test.cpp').read_text().split('void batch(bool enabled)')[0]
    driver += r'''
using s16=int16_t;using s32=int32_t;using u32=uint32_t;using Acmd=uint64_t;
struct OSMesg {u32 data32;};
struct SynthesisReverb {int useReverb=1,framesToIgnore=0,curFrame=0;};
Acmd commands[16]{};
struct {
 unsigned totalTaskCnt=0,resetStatus=0,audioResetSpecIdToLoad=0,audioRandom=0;
 void* audioResetQueueP=nullptr;void* cmdProcQueueP=nullptr;
 bool cmdQueueFinished=true;Acmd* curAbiCmdBuf=commands;
 struct {int updatesPerFrame=2,samplesPerUpdateMax=272,samplesPerUpdateMin=256,samplesPerUpdate=264;} audioBufferParameters;
 int numSynthesisReverbs=1;SynthesisReverb synthesisReverbs[1];void* curLoadedBook=nullptr;
} gAudioContext;
constexpr int OS_MESG_NOBLOCK=0;
void AudioLoad_DecreaseSampleDmaTtls(){testTick+=268112;}
void AudioLoad_ProcessLoads(unsigned){testTick+=268112;}
void AudioLoad_ProcessScriptLoads(){testTick+=268112;}
int AudioHeap_ResetStep(){return 1;}
int osSendMesg8(void*,unsigned,int){return 0;}
int osRecvMesg(void*,OSMesg*,int){testTick+=268112;return -1;}
void Audio_ProcessCmds(u32){}
void Audio_ScheduleProcessCmds(){testTick+=536224;}
void AudioSeq_ProcessSequences(int){testTick+=268112;}
void func_800DB03C(int){testTick+=268112;}
void AudioSynth_InitNextRingBuf(int,int,int){testTick+=268112;}
Acmd* AudioSynth_DoOneAudioUpdate(s16* out,int,Acmd* cmd,int i){
 testTick+=804336;*out=42+i;return cmd+1;
}
unsigned osGetCount(){testTick+=268112;return 7;}
'''
    driver += function('third_party/shipwright/soh/src/code/audio_synthesis.c',
                       'Acmd* AudioSynth_Update(')
    driver += function('third_party/shipwright/soh/src/code/code_800E4FE0.c',
                       'void AudioMgr_CreateNextAudioBuffer(')
    driver += r'''
int main(){
 flags=SOH3DS_LOG_GENERAL;int16_t out[1056]{};
 Soh3dsAudioProfileBegin(528,true);
 AudioMgr_CreateNextAudioBuffer(out,528);
 Soh3dsAudioProfileEnd();Soh3dsAudioProfileReport();
 auto s=contents();
 assert(s.find("loadsUs=3000 commandsUs=3000 sequenceUs=4000 mixingUs=8000 tailUs=1000")!=std::string::npos);
 assert(s.find("synthUs=19000")!=std::string::npos);
 assert(out[0]==42 && out[528]==43 && gAudioContext.totalTaskCnt==1);
 puts("PASS: actual AudioMgr/AudioSynth phase boundaries and output handoff");
}
'''
    (p/'integration.cpp').write_text(driver)
    subprocess.run(['c++', '-std=c++17', '-DSOH3DS_AUDIO_PROFILE', '-I'+str(p),
                    '-I'+str(ROOT/'third_party/libultraship/include'),
                    str(p/'integration.cpp'), str(ROOT/'src/compat3ds/audio_profile_3ds.cpp'),
                    '-o', str(p/'integration')], check=True)
    (p/'audio-perf.log').unlink()
    subprocess.run([str(p/'integration')], cwd=p, check=True)
    # Prove the boundary check detects an omitted load-phase marker.
    (p/'integration.cpp').write_text(driver.replace(
        'Soh3dsAudioProfileMark(SOH3DS_AUDIO_LOADS);', '(void)0;'))
    subprocess.run(['c++', '-std=c++17', '-DSOH3DS_AUDIO_PROFILE', '-I'+str(p),
                    '-I'+str(ROOT/'third_party/libultraship/include'),
                    str(p/'integration.cpp'), str(ROOT/'src/compat3ds/audio_profile_3ds.cpp'),
                    '-o', str(p/'negative')], check=True)
    (p/'audio-perf.log').unlink()
    negative = subprocess.run([str(p/'negative')], cwd=p, capture_output=True, text=True)
    assert negative.returncode != 0 and 'loadsUs=3000' in negative.stderr
    print('PASS: missing production phase marker is detected')
