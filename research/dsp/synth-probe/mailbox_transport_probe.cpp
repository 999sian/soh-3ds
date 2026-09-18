#define SOH3DS_DSP_MAILBOX_DIAGNOSTIC
#include "resident_transport_3ds.h"
#include <cassert>
#include <cstdio>
static unsigned sleeps=0; static bool badCache=false;
Result svcCreateEvent(Handle* h,int){*h=1;return 0;}
Result svcCloseHandle(Handle){return 0;}
Result svcFlushProcessDataCache(Handle,u32,unsigned){return 0;}
Result svcInvalidateProcessDataCache(Handle,u32,unsigned){return badCache?-1:0;}
void svcSleepThread(s64 ns){assert(ns>0 && ns<=1000000);++sleeps;}
// No event-registration or DR0 IPC definitions: linking catches accidental use.
int main(){
 using namespace ResidentDsp;
 static uint16_t memory[MappedSharedWords]{};memory[0x10]=0x4458;
 CtrTransport io;Session<CtrTransport> session(io);
 assert(io.open(memory,MappedSharedWords) && session.attach(0x4458));
 Command command{8,16,0,0};assert(session.submit(&command,1,0,100));
 assert(session.poll(1)==State::Running && sleeps==0);
 assert(io.waitForEvent(9000000)==Receive::Pending && sleeps==1);
 memory[0x11]=session.submittedSequence();memory[0]=0;
 assert(session.poll(2)==State::Done && session.acknowledge());
 assert(session.submit(&command,1,3,100));memory[0]=0;
 assert(session.poll(4)==State::Fault && session.error()==Failure::Protocol);
 session.resetAfterStop();assert(io.closeAfterStop());
 assert(io.open(memory,MappedSharedWords) && session.attach(0x4458));
 assert(session.submit(&command,1,5,100));badCache=true;
 assert(session.poll(6)==State::Fault && session.error()==Failure::Transfer);
 badCache=false;session.resetAfterStop();assert(io.closeAfterStop());
 assert(io.open(memory,MappedSharedWords));memory[0]=0;assert(session.attach(0x4458));
 assert(session.submit(&command,1,10,2));
 assert(session.poll(12)==State::Fault && session.error()==Failure::Timeout);
 assert(io.closeAfterStop());puts("PASS: mailbox sleep bound, completion, stale sequence, cache fault, timeout, no DR0 IPC linkage");
}
