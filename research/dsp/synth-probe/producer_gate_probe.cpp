#include "producer_gate.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){
    std::atomic<bool> entered{false},paused{false};
    require(SohDspProducerTryBegin(),"initial batch admitted");
    std::thread control([&](){entered=true;SohDspProducerPause(SOH_DSP_PAUSE_LIFECYCLE);paused=true;});
    while(!entered)std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(!paused,"pause waits for entire current batch");
    SohDspProducerEnd();control.join();require(paused,"pause acknowledged after submission");
    require(!SohDspProducerTryBegin(),"sleep prevents a new batch without blocking");
    SohDspProducerPause(SOH_DSP_PAUSE_CONTROL);
    SohDspProducerResume(SOH_DSP_PAUSE_CONTROL);
    require(!SohDspProducerTryBegin(),"control completion cannot reopen sleeping producer");
    SohDspProducerResume(SOH_DSP_PAUSE_LIFECYCLE);
    {
        SohDspProducerScope producer;require(bool(producer),"wake admits next batch");
        std::atomic<bool> second{true};
        std::thread other([&](){second=SohDspProducerTryBegin();if(second)SohDspProducerEnd();});other.join();
        require(!second,"busy gate try returns immediately without stealing ownership");
    }
    require(SohDspProducerTryBegin(),"RAII releases at loop break/return");SohDspProducerEnd();
    SohDspProducerPause(SOH_DSP_PAUSE_SHUTDOWN|SOH_DSP_PAUSE_LIFECYCLE);
    SohDspProducerResume(~0u);
    require(!SohDspProducerTryBegin(),"late wake cannot reopen terminal shutdown");
    std::puts("PASS: full-batch pause exclusion, nonblocking producer, independent pause reasons, RAII release and terminal shutdown");
}
