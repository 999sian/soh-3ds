#include "cpu_replay_dispatch.h"
// Keep libultra/mixer headers separate from libctru's conflicting typedefs.
void SohDspMixerExecuteCpu(const ResidentDsp::CpuCall& call,const void* source){
    ResidentDsp::executeCpuCall(call,source);
}
