#pragma once
#include <3ds.h>
#include "resident_session.h"
#include "mapped_bootstrap.h"
#include "stream_io_trace.h"

// Keep the v14 probe build switch compatible with the runtime completion mode.
#if defined(SOH3DS_DSP_MAILBOX_DIAGNOSTIC) && !defined(SOH3DS_DSP_MAILBOX_COMPLETION)
#define SOH3DS_DSP_MAILBOX_COMPLETION 1
#endif

namespace ResidentDsp {
// Custom firmware transport with paired mailbox or DR0 completion. Lifecycle owner loads
// firmware before open(), stops it before closeAfterStop(), and preserves the
// shared sample/state buffers until then. This class never initializes NDSP.
class CtrTransport {
public:
    static constexpr unsigned LegacySharedWords=0x4100;
    CtrTransport()=default;
    CtrTransport(const CtrTransport&)=delete;
    CtrTransport& operator=(const CtrTransport&)=delete;
    bool open(volatile uint16_t* shared,unsigned words=LegacySharedWords){
        // Caller verifies service-converted endpoints before selecting the larger
        // map. Preserve the legacy bound for all existing native probes.
        if(event || !shared || (words!=LegacySharedWords && words!=MappedSharedWords))return false;
        result=svcCreateEvent(&event,RESET_STICKY);
        if(R_FAILED(result)){event=0;return false;}
#ifndef SOH3DS_DSP_MAILBOX_COMPLETION
        // DSP service interrupt0 is DR0, confirmed by the LLE implementation.
        // Unlike audio pipe2 this needs no Nintendo firmware pipe structures.
        result=DSP_RegisterInterruptEvents(event,0,0);
        if(R_FAILED(result)){svcCloseHandle(event);event=0;return false;}
#endif
        memory=shared;mappedWords=words;return true;
    }
    bool closeAfterStop(){
        if(!event){memory=nullptr;mappedWords=0;return true;}
#ifdef SOH3DS_DSP_MAILBOX_COMPLETION
        Result unregister=0;
#else
        Result unregister=DSP_RegisterInterruptEvents(0,0,0);
#endif
        Result closed=svcCloseHandle(event);event=0;memory=nullptr;mappedWords=0;
        result=R_FAILED(unregister)?unregister:closed;return R_SUCCEEDED(result);
    }
    uint16_t read(unsigned word)const{return memory[word];}
    void write(unsigned word,uint16_t value){memory[word]=value;}
    bool flush(unsigned word,unsigned count){
        if(!valid(word,count))return false;
        stage="flush";SOH_DSP_TRACE_IO("flush_begin",word,count,0);
        result=svcFlushProcessDataCache(CUR_PROCESS_HANDLE,(u32)(memory+word),count*2);
        SOH_DSP_TRACE_IO("flush_end",word,count,result);
        return R_SUCCEEDED(result);
    }
    bool invalidate(unsigned word,unsigned count){
        if(!valid(word,count))return false;
        stage="invalidate";SOH_DSP_TRACE_IO("invalidate_begin",word,count,0);
        result=svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE,(u32)(memory+word),count*2);
        SOH_DSP_TRACE_IO("invalidate_end",word,count,result);
        return R_SUCCEEDED(result);
    }
    Receive tryReceive(uint16_t& sequence){
#ifdef SOH3DS_DSP_MAILBOX_COMPLETION
        Receive ready=waitForEvent(0);
        if(ready==Receive::Received)sequence=read(0x11);
        return ready;
#else
        Receive ready=waitForEvent(0);
        if(ready!=Receive::Received)return ready;
        // A stale/spurious event must not enter the DSP service's potentially
        // blocking RecvData path with an empty register.
        bool dataReady=false;
        stage="ready";result=DSP_RecvDataIsReady(0,&dataReady);
        if(R_FAILED(result) || !dataReady){if(R_SUCCEEDED(result))stage="empty-register";return Receive::Error;}
        stage="receive";result=DSP_RecvData(0,&sequence);
        if(R_FAILED(result))return Receive::Error;
        // Sticky event lets the worker sleep first, then consume through poll.
        // No next request is allowed until this notification has been drained.
        stage="clear";result=svcClearEvent(event);
        return R_SUCCEEDED(result)?Receive::Received:Receive::Error;
#endif
    }
    Receive waitForEvent(s64 timeoutNs){
        if(!event)return Receive::Error;
#ifdef SOH3DS_DSP_MAILBOX_COMPLETION
        // Paired firmware omits DR0. Check completion without blocking DSP IPC.
        // Sleep between checks to yield ARM11 time; Session owns the deadline.
        if(!invalidate(0,32))return Receive::Error;
        if(read(0)==0)return Receive::Received;
        if(timeoutNs>0){
            stage="mailbox-sleep";svcSleepThread(timeoutNs<1000000?timeoutNs:1000000);
        }
        return Receive::Pending;
#else
        stage="wait";result=svcWaitSynchronization(event,timeoutNs);
        // Kernel timeout is positive (Info level); check it before success.
        if(R_DESCRIPTION(result)==RD_TIMEOUT)return Receive::Pending;
        return result==0?Receive::Received:Receive::Error;
#endif
    }
    Handle completionEvent()const{return event;}
    Result lastResult()const{return result;}
    const char* lastStage()const{return stage;}
private:
    bool valid(unsigned word,unsigned count)const{return memory && word<=mappedWords && count<=mappedWords-word;}
    volatile uint16_t* memory=nullptr;
    unsigned mappedWords=0;
    Handle event=0;
    Result result=0;
    const char* stage="none";
};
}
