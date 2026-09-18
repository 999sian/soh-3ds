// Included only by mixer.c, after rspa, its table and existing pointer guard.
#include <stddef.h>
#include "dsp_mixer_hooks.h"
_Static_assert(sizeof(rspa)==sizeof(DspMixerImage),"DSP mixer image size");
#define SOH_DSP_CHECK_FIELD(field) _Static_assert(offsetof(__typeof__(rspa),field)==offsetof(DspMixerImage,field),"DSP mixer image offset " #field)
SOH_DSP_CHECK_FIELD(in);SOH_DSP_CHECK_FIELD(out);SOH_DSP_CHECK_FIELD(nbytes);
SOH_DSP_CHECK_FIELD(vol);SOH_DSP_CHECK_FIELD(rate);SOH_DSP_CHECK_FIELD(vol_wet);SOH_DSP_CHECK_FIELD(rate_wet);
SOH_DSP_CHECK_FIELD(adpcm_loop_state);SOH_DSP_CHECK_FIELD(adpcm_table);SOH_DSP_CHECK_FIELD(filter_count);SOH_DSP_CHECK_FIELD(filter);SOH_DSP_CHECK_FIELD(buf);
#undef SOH_DSP_CHECK_FIELD
static SohDspMixerHooks soh_dsp_hooks;
static void* soh_dsp_context;
static int soh_dsp_chunk_active,soh_dsp_cpu_bypass;
int SohDspMixerInstallHooks(const SohDspMixerHooks* hooks,void* context){
    if(soh_dsp_chunk_active || soh_dsp_cpu_bypass)return 0;
    if(hooks && (!hooks->begin || !hooks->end || !hooks->barrier || !hooks->record))return 0;
    if(hooks)soh_dsp_hooks=*hooks;else memset(&soh_dsp_hooks,0,sizeof(soh_dsp_hooks));
    soh_dsp_context=context;return 1;
}
void SohDspMixerBeginChunk(void){
    if(!soh_dsp_hooks.record || soh_dsp_cpu_bypass || soh_dsp_chunk_active)return;
    soh_dsp_chunk_active=1;soh_dsp_hooks.begin(soh_dsp_context);
}
void SohDspMixerEndChunk(void){
    if(!soh_dsp_chunk_active || soh_dsp_cpu_bypass)return;
    soh_dsp_hooks.end(soh_dsp_context);soh_dsp_chunk_active=0;
}
int SohDspMixerSetCpuBypass(int bypass){int previous=soh_dsp_cpu_bypass;soh_dsp_cpu_bypass=!!bypass;return previous;}
int SohDspMixerReadImage(DspMixerImage* image,unsigned bytes){if(!image || bytes!=sizeof(rspa))return 0;memcpy(image,&rspa,bytes);return 1;}
int SohDspMixerWriteImage(const DspMixerImage* image,unsigned bytes){if(!image || bytes!=sizeof(rspa))return 0;memcpy(&rspa,image,bytes);return 1;}
const int16_t* SohDspMixerResampleTable(void){return &resample_table[0][0];}
static void soh_dsp_barrier(void){if(soh_dsp_chunk_active && !soh_dsp_cpu_bypass)soh_dsp_hooks.barrier(soh_dsp_context);}
static int soh_dsp_loop_ready(void){
    if(!soh_dsp_chunk_active || soh_dsp_cpu_bypass)return 1;
    if(SOH3DS_AUDIO_GUARD("dsp.loop",rspa.adpcm_loop_state,32))return 1;
    // A captured SetLoop may supersede this baseline pointer; conservative CPU
    // fallback restores that ordering before the original decoder guard runs.
    soh_dsp_barrier();return 0;
}
static int soh_dsp_record(uint8_t op,const void* pointer,uint32_t bytes,int checkPointer,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4){
    uint32_t args[5];
    if(!soh_dsp_chunk_active || soh_dsp_cpu_bypass)return 0;
    // Validate BEFORE snapshotting any host range. On failure flush the prefix,
    // then let the original guarded body perform its existing zero/skip behavior.
    if(checkPointer && !SOH3DS_AUDIO_GUARD("dsp.capture",pointer,bytes)){soh_dsp_barrier();return 0;}
    args[0]=a0;args[1]=a1;args[2]=a2;args[3]=a3;args[4]=a4;
    return soh_dsp_hooks.record(soh_dsp_context,op,args,(uintptr_t)pointer);
}
#define SOH_DSP_CAPTURE_CALL(op,pointer,bytes,check,a0,a1,a2,a3,a4) \
    do {if(soh_dsp_record(SOH_DSP_OP_##op,(pointer),(uint32_t)(bytes),(check),(uint32_t)(a0),(uint32_t)(a1),(uint32_t)(a2),(uint32_t)(a3),(uint32_t)(a4)))return;}while(0)
#define SOH_DSP_CAPTURE_BARRIER() soh_dsp_barrier()
