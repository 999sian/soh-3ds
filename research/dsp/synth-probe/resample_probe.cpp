#include <algorithm>
#include <cstdio>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "program.h"
// N64 resampling rounds each product before summing; rounding only the dot
// product gives different PCM. Pointers r0=input4, r1=coeff4, r2=output1.
#include "resample_program.h"
static int16_t reference(const int16_t* in,const int16_t* coeff) {
    int32_t sum=0;
    for(unsigned i=0;i<4;++i)sum+=(int32_t(in[i])*coeff[i]+0x4000)>>15;
    return int16_t(std::clamp(sum,-32768,32767));
}
int main() {
    auto p=makeResampleTap();Teakra::Teakra dsp({});
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    uint32_t rng=0x925ac1;
    for(unsigned test=0;test<30000;++test) {
        int16_t input[4],coeff[4];
        for(unsigned i=0;i<4;++i) {
            rng=rng*1664525u+1013904223u;input[i]=int16_t(rng>>16);
            rng=rng*1664525u+1013904223u;coeff[i]=int16_t(rng>>16);
            if(test<8){input[i]=test&1?32767:-32768;coeff[i]=test&2?32767:-32768;if(test&4)input[i]=i&1?-1:1;}
            dsp.DataWrite(0x100+i,uint16_t(input[i]));dsp.DataWrite(0x200+i,uint16_t(coeff[i]));
        }
        auto& r=dsp.GetRegisterState();r.pc=0;r.r[0]=0x100;r.r[1]=0x200;r.r[2]=0x300;
        dsp.Run(p.instructions);
        if(int16_t(dsp.DataRead(0x300))!=reference(input,coeff)) {
            std::fprintf(stderr,"resample PCM mismatch case%u actual%d expected%d\n",test,int16_t(dsp.DataRead(0x300)),reference(input,coeff));return 1;
        }
        if(r.r[0]!=0x104 || r.r[1]!=0x204 || r.r[2]!=0x301)return 2;
    }
    puts("PASS: 30000 DSP four-tap resampling calculations, per-product rounding, saturation and consecutive jobs");
}
