#include <algorithm>
#include <cstdio>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "program.h"
// Inner ADPCM reconstruction, fixed probe layout: deltas1000[8], book2000[16],
// preceding output samples2ffe/2fff, output3000[8]. Bit unpacking is not here yet.
#include "adpcm_program.h"
int main() {
    auto p=makeAdpcmHalf();Teakra::Teakra dsp({});
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    uint32_t rng=0x23789a;auto next=[&](){rng=rng*1664525u+1013904223u;return int16_t(rng>>16);};
    for(unsigned trial=0;trial<10000;++trial) {
        int16_t in[8],book[16],prev2=next(),prev1=next();
        dsp.DataWrite(0x2ffe,uint16_t(prev2));dsp.DataWrite(0x2fff,uint16_t(prev1));
        for(unsigned i=0;i<8;++i){in[i]=next();dsp.DataWrite(0x1000+i,uint16_t(in[i]));}
        for(unsigned i=0;i<16;++i){book[i]=next();dsp.DataWrite(0x2000+i,uint16_t(book[i]));}
        dsp.GetRegisterState().pc=0;dsp.Run(p.instructions);
        for(unsigned j=0;j<8;++j) {
            uint32_t acc=uint32_t(int32_t(book[j])*prev2)+uint32_t(int32_t(book[8+j])*prev1)+(uint32_t(int32_t(in[j]))<<11);
            for(unsigned k=0;k<j;++k)acc+=uint32_t(int32_t(book[8+j-k-1])*in[k]);
            int16_t expected=std::clamp(int32_t(acc)>>11,-32768,32767),actual=int16_t(dsp.DataRead(0x3000+j));
            if(actual!=expected){printf("FAIL trial%u sample%u actual%d expected%d\n",trial,j,actual,expected);return 1;}
        }
    }
    puts("PASS: 80000 DSP ADPCM reconstructed samples including modulo32 accumulator wrap");
}
