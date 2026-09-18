#include "sdk_lifecycle_owner.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static unsigned sleeps=0,wakes=0,cancels=0;
static bool sdkSleeping=false,allowSleep=true;
extern "C" bool SohSdkDspSleep(){++sleeps;sdkSleeping=allowSleep;return sdkSleeping;}
extern "C" void SohSdkDspWakeup(){require(sdkSleeping,"SDK wake paired");sdkSleeping=false;++wakes;}
extern "C" void SohSdkDspCancel(){require(sdkSleeping,"SDK cancel paired");sdkSleeping=false;++cancels;}
struct Owner {
    enum class Mode {Cpu,Custom,Silent,Suspended,Closed};
    Mode current=Mode::Cpu;unsigned starts=0,stops=0,resumes=0,shutdowns=0;bool resumeFail=false,restoreCustom=false;
    Mode mode(){return current;}
    void excluded(){require(!SohDspProducerTryBegin(),"producer excluded during resource transition");}
    bool enableCustom(){excluded();require(!sdkSleeping,"cannot replace sleeping SDK component");current=Mode::Custom;++starts;return true;}
    bool disableCustom(){excluded();current=Mode::Cpu;++stops;return true;}
    bool suspend(){excluded();restoreCustom=current==Mode::Custom;current=Mode::Suspended;++stops;return true;}
    bool resume(){excluded();require(current==Mode::Suspended && !sdkSleeping,"resume owns its firmware");current=resumeFail?Mode::Silent:restoreCustom?Mode::Custom:Mode::Cpu;++resumes;return !resumeFail;}
    bool shutdown(){excluded();require(!sdkSleeping,"cancel SDK before joining sleeping NDSP");current=Mode::Closed;++shutdowns;return true;}
};
static void scenario(unsigned number){
    Owner owner;std::mutex mutex;
    ResidentDsp::SdkLifecycleOwner<Owner,std::mutex> lifecycle(owner,mutex,SohSdkDspCancel);
    require(SohDspLifecycleInstall(lifecycle.hooks()),"install owner hooks");
    if(number>=4){
        require(!lifecycle.activate([](){return false;}) && owner.starts==0,"failed prepare does not enable resources");
        if(number==5)require(lifecycle.enableCustom(),"custom device reconfiguration");
        require(lifecycle.park() && !SohDspProducerTryBegin(),"device close parks producer");
        require(!aptDspSleep() && sleeps==0,"parked device has no SDK sleep lifetime");aptDspWakeup();aptDspCancel();
        bool configured=false;
        require(lifecycle.reopen([&](){owner.excluded();configured=true;}) && configured,"configure while producer remains paused");
        require(owner.current==(number==5?Owner::Mode::Custom:Owner::Mode::Cpu),"device reopen restores correct backend");
        {SohDspProducerScope producer;require(bool(producer),"device reopens producer");}
        require(lifecycle.shutdown(),"device shutdown");return;
    }
    if(number<2){
        if(number==1)require(lifecycle.enableCustom(),"enable custom before cancel");
        require(aptDspSleep(),"sleep before terminal control shutdown");
        require(lifecycle.shutdown() && owner.shutdowns==1,"terminal owner shutdown");
        require(cancels==(number==0?1u:0u),"only sleeping CPU needs SDK cancellation");
        aptDspWakeup();require(!aptDspSleep() && !lifecycle.enableCustom(),"late callbacks cannot reopen terminal lifetime");
        return;
    }
    allowSleep=false;require(!aptDspSleep(),"inactive SDK sleep returns false");
    {SohDspProducerScope producer;require(bool(producer),"inactive sleep releases gate");}
    allowSleep=true;
    require(lifecycle.enableCustom(),"custom enable");
    require(SohDspProducerTryBegin(),"producer holds whole batch");
    std::atomic<bool> entered{false},done{false};
    std::thread control([&](){entered=true;require(aptDspSleep(),"custom sleep");done=true;});
    while(!entered)std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(!done,"sleep waits for synth and submission to finish");SohDspProducerEnd();control.join();
    require(!SohDspProducerTryBegin() && !lifecycle.disableCustom(),"paused gate and mode-pair exclusion");
    require(aptDspSleep() && owner.stops==1,"duplicate sleep is idempotent");
    aptDspWakeup();require(owner.resumes==1 && sleeps==1 && wakes==0,"custom pair bypasses SDK auto reload");
    owner.resumeFail=true;require(aptDspSleep(),"sleep before failed restart");aptDspWakeup();
    require(owner.current==Owner::Mode::Silent,"failed custom resume can become silent");
    {SohDspProducerScope producer;require(bool(producer),"safe fallback resumes producer");}
    require(lifecycle.disableCustom() && aptDspSleep(),"CPU sleep through SDK");
    require(!lifecycle.enableCustom(),"no custom switch inside CPU sleep pair");
    aptDspWakeup();require(wakes==1 && owner.resumes==2,"CPU wake preserves SDK path");
    if(number==3)require(lifecycle.enableCustom(),"custom cancel case");
    require(aptDspSleep(),"sleep before SDK callback cancel");aptDspCancel();
    require(owner.current==Owner::Mode::Closed && cancels==(number==2?1u:0u),"callback cancel shuts correct backend");
    aptDspWakeup();require(!SohDspProducerTryBegin() && lifecycle.shutdown() && owner.shutdowns==1,"late wake and idempotent shutdown");
}
int main(){
    for(unsigned i=0;i<6;++i){
        pid_t pid=fork();require(pid>=0,"fork isolated terminal lifetime");
        if(!pid){scenario(i);_exit(0);}
        int status=0;require(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0,"lifecycle scenario");
    }
    std::puts("PASS: whole-batch producer exclusion, CPU/custom sleep pairing, duplicate sleep, mode-change refusal, restart fallback, SDK cancel-before-join and terminal late-wake suppression");
}
