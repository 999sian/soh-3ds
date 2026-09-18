#pragma once
#include "mixer_image.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SOH_DSP_MIXER_OPERATIONS(X) \
    X(Clear) X(Load) X(Save) X(Book) X(Buffer) X(Interleave) X(Move) X(Loop) X(Adpcm) X(Resample) \
    X(Env1) X(Env2) X(Envelope) X(Mix) X(S8) X(Add) X(Duplicate) X(Zoh) X(Interl) X(Filter) X(HiLo) X(Noop3) X(TableMultiply)
#define SOH_DSP_C_OP(name) SOH_DSP_OP_##name,
enum SohDspMixerOperation {SOH_DSP_MIXER_OPERATIONS(SOH_DSP_C_OP) SOH_DSP_OP_Count};
#undef SOH_DSP_C_OP
// Single audio-worker owner. record returns1 when capture or CPU replay handled
// the call,0 only after making live CPU execution safe. barrier and end must
// finish CPU fallback/output ownership before returning. No hook may throw.
typedef struct SohDspMixerHooks {
    void (*begin)(void*);
    void (*end)(void*);
    void (*barrier)(void*);
    int (*record)(void*,uint8_t,const uint32_t[5],uintptr_t);
} SohDspMixerHooks;
int SohDspMixerInstallHooks(const SohDspMixerHooks*,void* context);
void SohDspMixerBeginChunk(void);
void SohDspMixerEndChunk(void);
int SohDspMixerSetCpuBypass(int bypass);
int SohDspMixerReadImage(DspMixerImage*,unsigned bytes);
int SohDspMixerWriteImage(const DspMixerImage*,unsigned bytes);
const int16_t* SohDspMixerResampleTable(void);
#ifdef __cplusplus
}
#endif
