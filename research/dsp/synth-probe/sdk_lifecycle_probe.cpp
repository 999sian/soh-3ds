#include "sdk_lifecycle_bridge.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>
static std::atomic<unsigned> sdkSleeps{0},sdkWakes{0},sdkCancels{0};
extern "C" bool SohSdkDspSleep(){++sdkSleeps;return true;}
extern "C" void SohSdkDspWakeup(){++sdkWakes;}
extern "C" void SohSdkDspCancel(){++sdkCancels;}
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Owner {
    std::atomic<bool> cpu{false};
    std::atomic<unsigned> sleeps{0},wakes{0},cancels{0};
    static bool sleep(void* p,bool(*sdk)()) {auto& o=*static_cast<Owner*>(p);++o.sleeps;return o.cpu?sdk():false;}
    static void wake(void* p,void(*sdk)()) {auto& o=*static_cast<Owner*>(p);++o.wakes;if(o.cpu)sdk();}
    static void cancel(void* p,void(*sdk)()) {auto& o=*static_cast<Owner*>(p);++o.cancels;if(o.cpu)sdk();}
};
static Owner owner;
static const SohDspLifecycleHooks hooks{&owner,Owner::sleep,Owner::wake,Owner::cancel};
int main(){
    require(aptDspSleep(),"SDK default return");aptDspWakeup();aptDspCancel();
    SohDspLifecycleHooks partial=hooks;partial.cancel=nullptr;
    require(!SohDspLifecycleInstall(nullptr) && !SohDspLifecycleInstall(&partial),"reject incomplete hooks");
    std::array<std::thread,16> racers;std::atomic<unsigned> winners{0};
    for(auto& t:racers)t=std::thread([&](){if(SohDspLifecycleInstall(&hooks))++winners;});
    for(auto& t:racers)t.join();
    require(winners==1,"one immutable installation");
    require(!aptDspSleep(),"custom handler controls return");aptDspWakeup();aptDspCancel();
    require(sdkSleeps==1 && sdkWakes==1 && sdkCancels==1,"custom path never automatically unloads/reloads SDK");
    owner.cpu=true;
    require(aptDspSleep(),"CPU continuation return");aptDspWakeup();aptDspCancel();
    require(sdkSleeps==2 && sdkWakes==2 && sdkCancels==2 && owner.sleeps==2 && owner.wakes==2 && owner.cancels==2,"CPU mode explicitly preserves SDK behavior");
    std::puts("PASS: SDK fallback, complete immutable hook publication, concurrent installation, custom lifecycle exclusion and CPU continuations");
}
