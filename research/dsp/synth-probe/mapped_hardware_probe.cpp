#include "mapped_native_context.h"
int main(){
    gfxInitDefault();consoleInit(GFX_TOP,nullptr);
    bool sleep=aptIsSleepAllowed(),home=aptIsHomeAllowed();aptSetSleepAllowed(false);aptSetHomeAllowed(false);
    bool newer=false;APT_CheckNew3DS(&newer);osSetSpeedupEnable(newer);
    mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/soh",0777);logFile=fopen("sdmc:/3ds/soh/dsp-mapped-probe.txt","w");
    bool ok=logFile!=nullptr;
    if(ok){
        fprintf(logFile,"probe=v9 model=%s\nSilent native correctness; timings include transfers, not a game benchmark.\n",newer?"new":"old");fflush(logFile);
        bool initialized=R_SUCCEEDED(dspInit());ok=initialized;
        if(initialized){
            dspHookCookie cookie;dspHook(&cookie,hook);
            for(unsigned i=0;i<2 && ok;++i){
                auto* x=new(std::nothrow) Context;ok=x!=nullptr;
                if(x){ok=boot(*x);
                    for(unsigned frame=0;frame<16 && ok;++frame)ok=x->compare(i,frame);
                    if(ok)ok=x->compare(i,16,true);
                    bool stopped=x->stop();ok=ok && stopped;
                    // Preserve owned storage if the service cannot confirm stop.
                    if(stopped)delete x;
                }
            }
            dspUnhook(&cookie);dspExit();
        }
        fprintf(logFile,"finished=%s\n",ok?"PASS":"FAIL");fclose(logFile);
    }
    aptSetHomeAllowed(home);aptSetSleepAllowed(sleep);
    printf("Mapped DSP v9: %s\nSTART to exit\n",ok?"PASS":"FAIL");
    while(aptMainLoop()){hidScanInput();if(hidKeysDown()&KEY_START)break;gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
    gfxExit();return ok?0:1;
}
