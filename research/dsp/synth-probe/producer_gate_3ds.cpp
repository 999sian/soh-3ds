#include <3ds.h>
#include "producer_gate.h"
namespace {
struct ProducerGate {
    LightLock lock;
    unsigned reasons=0;
    ProducerGate(){LightLock_Init(&lock);}
};
ProducerGate gate;
}
extern "C" bool SohDspProducerTryBegin(){
    if(LightLock_TryLock(&gate.lock)!=0)return false;
    if(gate.reasons){LightLock_Unlock(&gate.lock);return false;}
    return true;
}
extern "C" void SohDspProducerEnd(){LightLock_Unlock(&gate.lock);}
extern "C" void SohDspProducerPause(unsigned reasons){
    LightLock_Lock(&gate.lock);gate.reasons|=reasons;LightLock_Unlock(&gate.lock);
}
extern "C" void SohDspProducerResume(unsigned reasons){
    LightLock_Lock(&gate.lock);
    // Terminal shutdown cannot accidentally be undone by a late wake callback.
    gate.reasons&=~(reasons & ~unsigned(SOH_DSP_PAUSE_SHUTDOWN));
    LightLock_Unlock(&gate.lock);
}
