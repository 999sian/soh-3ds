#!/usr/bin/env python3
"""Exercise the production audio reset handoff with a full four-message queue."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
source = (root/'third_party/shipwright/soh/src/code/code_800E4FE0.c').read_text()
mesg = (root/'third_party/libultraship/src/libultraship/libultra/os_mesg.cpp').read_text()
def function(text, signature):
    start = text.index(signature + ' {')
    pos = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start:pos] + '\n'
code = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
using u8=uint8_t; using u32=uint32_t; using s32=int32_t;
union OSMesg { u8 data8; u32 data32; };
struct OSMesgQueue { int validCount, first, msgCount; OSMesg* msg; };
struct AudioCmd { u32 opArgs, data; };
struct {
 u8 cmdWrPos,cmdRdPos,cmdQueueFinished,resetStatus,audioResetSpecIdToLoad;
 AudioCmd cmdBuf[256]; OSMesgQueue* cmdProcQueueP; OSMesgQueue* audioResetQueueP;
} gAudioContext{};
constexpr int OS_MESG_NOBLOCK=0, OS_MESG_BLOCK=1;
#define MESG_GUARD() ((void)0)
static bool sAudioResetSchedulePending=false;
int scheduleAttempts=0;
s32 Audio_ScheduleProcessCmds();
'''
for sig in ['void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msgBuf, s32 count)',
            's32 osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flag)',
            's32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flag)']:
    code += function(mesg, sig)
code += r'''
s32 osSendMesg32(OSMesgQueue* mq,u32 data,s32 flag) {
 ++scheduleAttempts; OSMesg m{};m.data32=data;return osSendMesg(mq,m,flag);
}
'''
for sig in ['void Audio_QueueCmd(u32 opArgs, u32 data)',
            'void Audio_QueueCmdS32(u32 opArgs, s32 data)',
            's32 Audio_ScheduleProcessCmds(void)', 'void Audio_ResetCmdQueue(void)',
            's32 func_800E5EDC(void)', 'void func_800E5F34(void)',
            's32 func_800E5F88(s32 resetPreloadID)']:
    code += function(source, sig)
code += r'''
OSMesg commands[4]{}, acknowledgements[1]{};
OSMesgQueue cq{},aq{};
void reset() {
 gAudioContext={};sAudioResetSchedulePending=false;scheduleAttempts=0;
 osCreateMesgQueue(&cq,commands,4);osCreateMesgQueue(&aq,acknowledgements,1);
 gAudioContext.cmdProcQueueP=&cq;gAudioContext.audioResetQueueP=&aq;
}
int consume() {
 OSMesg msg;int resets=0;
 while(osRecvMesg(&cq,&msg,0)==0) {
  for(u8 i=msg.data32>>8;i!=u8(msg.data32);i++) {
   auto &cmd=gAudioContext.cmdBuf[i];
   if(cmd.opArgs==0xF9000000) {
    gAudioContext.audioResetSpecIdToLoad=cmd.data;
    OSMesg ack{};ack.data8=cmd.data;assert(osSendMesg(&aq,ack,0)==0);++resets;
   }
   cmd.opArgs=0;
  }
 }
 return resets;
}
int main() {
 reset();
 // Slow initial synthesis lets the graph thread fill the four message slots.
 for(int i=0;i<4;i++) assert(Audio_ScheduleProcessCmds()==0);
 assert(func_800E5F88(10)==-1);
 assert(gAudioContext.cmdWrPos==1 && gAudioContext.cmdRdPos==0);
 int before=scheduleAttempts;
 for(int i=0;i<3;i++) assert(func_800E5EDC()==0); // bounded retry while full
 assert(scheduleAttempts==before+3);
 assert(consume()==0); // original queued ranges contain no reset
 assert(func_800E5EDC()==0); // publishes the unsent reset, waits for real ACK
 assert(consume()==1);
 assert(func_800E5EDC()==1);
 before=scheduleAttempts;assert(func_800E5EDC()==0);assert(scheduleAttempts==before);
 assert(consume()==0); // no duplicated reset
 // Normal submission, ring wrap, and mismatched acknowledgement stay intact.
 reset();gAudioContext.cmdWrPos=gAudioContext.cmdRdPos=255;
 assert(func_800E5F88(4)==0);assert(consume()==1);assert(func_800E5EDC()==1);
 OSMesg wrong{};wrong.data8=3;assert(osSendMesg(&aq,wrong,0)==0);
 assert(func_800E5EDC()==-1);
 // In-progress reset return codes remain distinct from queue-full failure.
 gAudioContext.resetStatus=5;assert(func_800E5F88(4)==-2);
 assert(func_800E5F88(7)==-3);assert(gAudioContext.audioResetSpecIdToLoad==7);
 puts("Audio reset: full queue retries, real ACK required, no duplicate, wrap and in-progress cases pass");
}
'''
with tempfile.TemporaryDirectory() as t:
    p=Path(t);(p/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++17','-D__3DS__',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
