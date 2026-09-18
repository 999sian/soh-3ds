#pragma once
// Feasibility probe: CSND plays linear PCM while our custom DSP firmware runs.
// This is not a streaming audio backend. csndPlaySound/UpdateInfo(true) are
// deliberately avoided because libctru's command-completion wait is unbounded.
static volatile u8* csndMarker() {
    // Add an info command first. libctru returns params at header + 8; its
    // completion flag is header + 4 (same flag used by csndExecCmds).
    return reinterpret_cast<volatile u8*>(csndAddCmd(0x300))-4;
}
static bool csndExecuteBounded(volatile u8* marker) {
    if(!check(csndExecCmds(false),"CSND execute"))return false;
    u64 start=svcGetSystemTick();
    while(!*marker) {
        __dmb();
        if(cancelled || svcGetSystemTick()-start>SYSCLOCK_ARM11/2) {
            ++failures;printf("CSND command timeout\n");return false;
        }
        svcSleepThread(100000);
    }
    __dmb();return true;
}
static bool csndOutputProof(unsigned boot) {
    FILE* f=fopen("sdmc:/3ds/soh/dsp-output-probe.txt",boot?"a":"w");
    if(!f){++failures;return false;}
    fprintf(f,"boot=%u output=CSND with custom DSP\n",boot);fflush(f);
    int16_t* pcm=nullptr;
    bool initialized=false,ok=false;
    unsigned channel=0,overlappedJobs=0,confirmedActiveJobs=0;
    do {
        Result rc=csndInit();fprintf(f,"csndInit=%08lx\n",(unsigned long)rc);fflush(f);
        if(!check(rc,"csndInit"))break;
        initialized=true;
        fprintf(f,"channel_mask=%08lx\n",(unsigned long)csndChannels);fflush(f);
        if(!csndChannels){++failures;break;}
        channel=__builtin_ctz(csndChannels);
        // 500 Hz triangle at 32 kHz, low amplitude; DSP applies the gain.
        for(unsigned i=0;i<512;++i){int t=i%64;input[i]=(t<32?t:64-t)*256-4096;initial[i]=0;}
        if(!upload(512) || !job(512,16384) || !download(512))break;
        for(unsigned i=0;i<512;++i)if(actual[i]!=reference(input[i],0,16384)){++failures;break;}
        if(failures)break;
        pcm=static_cast<int16_t*>(linearAlloc(32768));
        if(!pcm){++failures;break;}
        for(unsigned block=0;block<32;++block)memcpy(pcm+block*512,actual,1024);
        if(!check(svcFlushProcessDataCache(CUR_PROCESS_HANDLE,(u32)pcm,32768),"PCM flush"))break;
        auto marker=csndMarker();
        u32 flags=SOUND_CHANNEL(channel)|SOUND_FORMAT_16BIT|SOUND_ONE_SHOT|SOUND_ENABLE|(CSND_TIMER(32000)<<16);
        u32 volume=CSND_VOL(0.5f,0.0f);
        CSND_SetChnRegs(flags,osConvertVirtToPhys(pcm),0,32768,volume,volume);
        if(!csndExecuteBounded(marker))break;
        marker=csndMarker();if(!csndExecuteBounded(marker))break;
        bool wasActive=csndGetChnInfo(channel)->active!=0;
        fprintf(f,"channel=%u initially_active=%d\n",channel,wasActive);fflush(f);
        if(!wasActive){++failures;break;}
        printf("Playing DSP-generated test tone...\n");
        u64 began=svcGetSystemTick();
        bool jobsOk=true;
        // Continue verified DSP jobs during CSND playback; no NDSP firmware.
        while(svcGetSystemTick()-began<SYSCLOCK_ARM11*3/4) {
            if(!aptMainLoop() || cancelled){jobsOk=false;break;}
            memcpy(expected,actual,sizeof(actual));
            Soh3dsMixArm11(input,expected,0,512);
            if(!job(512,0) || !download(512) || memcmp(expected,actual,sizeof(actual))){jobsOk=false;break;}
            ++overlappedJobs;
            if(!confirmedActiveJobs) {
                marker=csndMarker();
                if(!csndExecuteBounded(marker)){jobsOk=false;break;}
                if(csndGetChnInfo(channel)->active)++confirmedActiveJobs;
            }
            svcSleepThread(1000000);
        }
        if(!jobsOk){++failures;break;}
        marker=csndMarker();if(!csndExecuteBounded(marker))break;
        bool ended=csndGetChnInfo(channel)->active==0;
        fprintf(f,"one_shot_ended=%d verified_dsp_jobs=%u confirmed_active_jobs=%u\n",ended,overlappedJobs,confirmedActiveJobs);fflush(f);
        if(!ended || !confirmedActiveJobs){++failures;break;}
        marker=csndMarker();CSND_SetPlayStateR(channel,0);
        if(!csndExecuteBounded(marker))break;
        ok=true;
    }while(false);
    // Release playback channels before freeing memory, including timeout paths.
    if(initialized)csndExit();
    // On a failed stop/command, do not recycle potentially DMA-visible storage.
    // This isolated test retains at most32KiB until process exit.
    if(pcm && ok)linearFree(pcm);
    fprintf(f,"result=%s (audibility not machine-verified)\n",ok?"PASS":"FAIL");fclose(f);
    return ok;
}
