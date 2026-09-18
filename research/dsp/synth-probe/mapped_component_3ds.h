#pragma once
#include "resident_transport_3ds.h"
#include "component_stop_guard.h"

namespace ResidentDsp {
// Serialized native component lifetime. The caller owns a dspInit reference,
// has stopped NDSP, and excludes APT's automatic DSP unload/reload. Firmware
// storage must remain alive until confirmed stop. No destructor releases an
// uncertain component, mapped state, or completion event.
class MappedComponent3ds {
public:
    MappedComponent3ds(CtrTransport& io,Session<CtrTransport>& session):io_(io),session_(session){}
    MappedComponent3ds(const MappedComponent3ds&)=delete;
    MappedComponent3ds& operator=(const MappedComponent3ds&)=delete;
    template<class Cancelled>
    bool start(const void* firmware,unsigned bytes,const int16_t* table,Cancelled cancelled){
        if(loaded_ || uncertain_ || stopGuard_.uncertain() || io_.completionEvent() ||
           session_.state()!=State::Unbound || !firmware || !bytes || !table)return false;
        // SDK LoadComponent otherwise returns success without loading our image.
        stage_="existing-component";
        if(dspIsComponentLoaded()){uncertain_=true;return false;}
        bool accepted=false;stage_="load";
        result_=DSP_LoadComponent(firmware,bytes,0xff,0xff,&accepted);
        loaded_=accepted || dspIsComponentLoaded();
        if(R_FAILED(result_)){
            // An IPC failure with no SDK loaded flag cannot prove the service
            // did not start the component. SDK unload would merely return0.
            if(!loaded_)uncertain_=true;
            return false;
        }
        if(!loaded_)return false; // successful service response explicitly refused
        u32 base=0,last=0;stage_="mapping";
        result_=DSP_ConvertProcessAddressFromDspDram(0,&base);
        if(R_FAILED(result_))return false;
        result_=DSP_ConvertProcessAddressFromDspDram(MappedSharedWords-1,&last);
        if(R_FAILED(result_) || !validMappedRegion(base,last))return false;
        auto* memory=reinterpret_cast<volatile uint16_t*>(base);
        uint64_t began=svcGetSystemTick();bool ready=false;stage_="signature";
        // The service consumes the DR2 loader handshake; do not receive it again.
        while(!ready && uint64_t(svcGetSystemTick())-began<SYSCLOCK_ARM11/2 && !cancelled()){
            result_=svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE,base,64);
            if(R_FAILED(result_))return false;
            ready=memory[0x10]==0x4458 && memory[0]==0;
            if(!ready)svcSleepThread(1000000);
        }
        if(!ready || cancelled())return false;
        stage_="attach";
        if(!io_.open(memory,MappedSharedWords) || !session_.attach(0x4458)){result_=io_.lastResult();return false;}
        stage_="tables";
        if(!initializeMappedTables(io_,session_,table)){result_=io_.lastResult();return false;}
        stage_="ready";return true;
    }
    bool stop(){
        if(uncertain_ || stopGuard_.uncertain())return false;
        if(loaded_){
            // APT or a different owner must not have silently cleared the SDK
            // flag: an SDK no-op cannot confirm this ownership lifetime ended.
            if(!dspIsComponentLoaded()){uncertain_=true;return false;}
            stage_="unload";
            if(!stopGuard_.stop([&](){result_=DSP_UnloadComponent();return result_==0;}))return false;
            loaded_=false;
        }
        stage_="close";
        if(!io_.closeAfterStop()){result_=io_.lastResult();uncertain_=true;return false;}
        session_.resetAfterStop();stage_="stopped";return true;
    }
    bool loaded()const{return loaded_;}
    bool uncertain()const{return uncertain_ || stopGuard_.uncertain();}
    Result lastResult()const{return result_;}
    const char* lastStage()const{return stage_;}
private:
    CtrTransport& io_;Session<CtrTransport>& session_;ComponentStopGuard stopGuard_;
    bool loaded_=false,uncertain_=false;Result result_=0;const char* stage_="idle";
};
}
