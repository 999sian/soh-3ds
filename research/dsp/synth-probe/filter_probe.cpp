#include <cstdio>
#include <vector>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "filter_program.h"
extern "C" void filterReference(int16_t*,unsigned,unsigned,int16_t*,int16_t*);
int main(){
    uint32_t rng=0x319a35;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    unsigned jobs=0,samples=0;
    for(unsigned init:{0u,1u}){
        auto p=makeFilter(init);Teakra::Teakra dsp({});
        for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
        std::vector<int16_t> memory(0x4000);
        for(unsigned trial=0;trial<400;++trial){
            unsigned bytes=trial%5==0?0:trial%5==1?16:trial%5==2?32:trial%5==3?256:1024;
            unsigned count=bytes?bytes/2:8;
            for(auto& x:memory)x=int16_t(next());
            // Explicit odd negative sums distinguish truncation from arithmetic shift.
            if(trial<16)for(unsigned i=0;i<8;++i){memory[0x3c10+i]=-int(trial)-1;memory[0x3c08+i]=0;}
            if(trial>=16 && trial<24)for(unsigned i=0;i<8;++i){memory[0x3c10+i]=trial&1?-32768:32767;memory[0x3c08+i]=memory[0x3c10+i];}
            for(unsigned i=0;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
            filterReference(memory.data(),bytes,init,memory.data()+0x3c00,memory.data()+0x3c10);
            auto& regs=dsp.GetRegisterState();regs.pc=0;regs.r[0]=0x400;regs.r[6]=count/8-1;
            dsp.Run(180000);
            for(unsigned i=0;i<memory.size();++i){
                if(i>=0x3c20 && i<0x3c30)continue;
                if(int16_t(dsp.DataRead(i))!=memory[i]){printf("FAIL filter init%u trial%u addr%x got%d expected%d\n",init,trial,i,int16_t(dsp.DataRead(i)),memory[i]);return 1;}
            }
            if(regs.r[0]!=0x400+count)return 2;
            ++jobs;samples+=count;
        }
    }
    printf("PASS: %u DSP filter jobs / %u samples, init/history, signed coefficient averaging, 64-bit reference accumulation, zero count and memory guards\n",jobs,samples);
}
