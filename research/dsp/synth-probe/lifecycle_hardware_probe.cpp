#include "mapped_native_context.h"
#include "sdk_lifecycle_bridge.h"
#include "sdk_ownership.h"

// Deliberate native lifecycle calls, not a claim of physical lid/HOME testing.
// Actual producer gating and NDSP ownership belong to the game adapter.
struct LifecycleProbe {
    Context* current=nullptr;
    bool suspended=false,ok=true;
    unsigned sleeps=0,wakes=0,cancels=0;
    static bool sleep(void* p,bool (*sdk)(void)){
        auto& x=*static_cast<LifecycleProbe*>(p);
        if(!x.current)return sdk();
        if(x.suspended)return true;
        x.ok=x.current->stop() && x.ok;
        x.suspended=x.ok;++x.sleeps;return x.suspended;
    }
    static void wake(void* p,void (*sdk)(void)){
        auto& x=*static_cast<LifecycleProbe*>(p);
        if(!x.current){sdk();return;}
        if(!x.suspended){x.ok=false;return;}
        // If stock automatic reload ran before us, boot refuses its existing
        // component. Success therefore proves entry-point routing owns restart.
        x.ok=boot(*x.current) && x.ok;x.suspended=false;++x.wakes;
    }
    static void cancel(void* p,void (*sdk)(void)){
        auto& x=*static_cast<LifecycleProbe*>(p);
        if(!x.current){sdk();return;}
        x.ok=x.suspended && !x.current->component.loaded() && x.ok;
        x.suspended=false;++x.cancels;
    }
};
static LifecycleProbe lifecycle;
static const SohDspLifecycleHooks lifecycleHooks={&lifecycle,LifecycleProbe::sleep,LifecycleProbe::wake,LifecycleProbe::cancel};

int main(){
    gfxInitDefault();consoleInit(GFX_TOP,nullptr);
    bool sleep=aptIsSleepAllowed(),home=aptIsHomeAllowed();aptSetSleepAllowed(false);aptSetHomeAllowed(false);
    bool newer=false;APT_CheckNew3DS(&newer);osSetSpeedupEnable(newer);
    mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/soh",0777);
    logFile=fopen("sdmc:/3ds/soh/dsp-lifecycle-probe.txt","w");bool ok=logFile!=nullptr;
    if(ok){
        fprintf(logFile,"probe=v11 model=%s\nNative SDK lifecycle routing and DSP correctness; not physical sleep or performance validation.\n",newer?"new":"old");fflush(logFile);
        bool initialized=R_SUCCEEDED(dspInit());ok=initialized && SohDspObservedComponentState()==SOH_DSP_COMPONENT_STOPPED;
        if(initialized){
            dspHookCookie cookie;dspHook(&cookie,hook);
            // No component: before and after installation must preserve SDK's
            // inactive sleep/wake/cancel behavior.
            ok=!aptDspSleep() && SohDspLifecycleInstall(&lifecycleHooks);
            aptDspWakeup();aptDspCancel();ok=!aptDspSleep() && ok;
            for(unsigned i=0;i<2 && ok;++i){
                auto* x=new(std::nothrow) Context;ok=x!=nullptr;
                if(x){
                    lifecycle.current=x;ok=boot(*x) && SohDspObservedComponentState()==SOH_DSP_COMPONENT_LOADED;
                    for(unsigned frame=0;frame<16 && ok;++frame){
                        if(frame==8 || frame==12){
                            ok=aptDspSleep() && !x->component.loaded() && x->session.state()==State::Unbound && !x->io.completionEvent() && SohDspObservedComponentState()==SOH_DSP_COMPONENT_STOPPED;
                            if(frame==8)aptDspWakeup();
                            else{aptDspCancel();ok=!x->component.loaded() && boot(*x) && ok;}
                            ok=lifecycle.ok && ok;
                            fprintf(logFile,"lifecycle boot=%u frame=%u result=%s\n",i,frame,ok?"PASS":"FAIL");fflush(logFile);
                        }
                        if(ok)ok=x->compare(i,frame);
                    }
                    if(ok)ok=x->compare(i,16,true);
                    bool stopped=x->stop();ok=ok && stopped;
                    if(stopped){lifecycle.current=nullptr;delete x;}
                }
            }
            // Never discard an uncertain context, including through late hooks.
            if(lifecycle.current){svcBreak(USERBREAK_PANIC);std::abort();}
            dspUnhook(&cookie);
            dspExit();
        }
        ok=ok && lifecycle.sleeps==4 && lifecycle.wakes==2 && lifecycle.cancels==2;
        ok=ok && SohDspObservedOwnershipChanges()==13 && SohDspObservedComponentState()==SOH_DSP_COMPONENT_STOPPED;
        fprintf(logFile,"ownership_changes=%lu state=%u\n",(unsigned long)SohDspObservedOwnershipChanges(),SohDspObservedComponentState());
        fprintf(logFile,"lifecycle_sleeps=%u wakes=%u cancels=%u\nfinished=%s\n",lifecycle.sleeps,lifecycle.wakes,lifecycle.cancels,ok?"PASS":"FAIL");fclose(logFile);
    }
    aptSetHomeAllowed(home);aptSetSleepAllowed(sleep);
    printf("DSP lifecycle v11: %s\nSTART to exit\n",ok?"PASS":"FAIL");
    while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_START)break;gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
    gfxExit();return ok?0:1;
}
