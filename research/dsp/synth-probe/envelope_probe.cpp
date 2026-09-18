#include <algorithm>
#include <cstdio>
#include <vector>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "envelope_program.h"
extern "C" void envelopeReference(int16_t*,unsigned,unsigned,const uint16_t*,const uint16_t*,const uint16_t*);
int main(){
    uint32_t rng=0x35211;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    unsigned jobs=0,samples=0;
    for(bool runtime:{false,true})for(unsigned flags=0;flags<32;++flags){
        auto p=makeEnvelope(flags,runtime);Teakra::Teakra dsp({});
        for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
        std::vector<int16_t> memory(0x4000);
        for(unsigned trial=0;trial<40;++trial){
            unsigned n=trial%5==0?0:trial%5==1?16:trial%5==2?32:trial%5==3?128:512;
            unsigned actual=n?n:8;
            uint16_t vols[3],rates[3];
            for(auto& x:memory)x=int16_t(next());
            for(unsigned i=0;i<3;++i){vols[i]=trial<8?uint16_t((trial%4)*0x5555):next();rates[i]=trial<8?uint16_t(0xffff-trial):next();memory[0x3b00+i]=int16_t(vols[i]);memory[0x3b03+i]=int16_t(rates[i]);}
            if(trial<8)for(unsigned i=0x400;i<0xe00;++i)memory[i]=trial&1?-32768:32767;
            memory[0x3b06]=flags;
            for(unsigned i=0;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
            uint16_t bases[]={0x400,0x600,0x800,0xa00,0xc00};
            switch((trial/5)%5){case 1:bases[2]=bases[3];break;case 2:bases[1]=bases[4];break;case 3:bases[2]=bases[3]=bases[4]=bases[1];break;case 4:bases[1]=bases[0];break;}
            envelopeReference(memory.data(),n,flags,vols,rates,bases);
            auto& regs=dsp.GetRegisterState();regs.pc=0;for(unsigned i=0;i<5;++i)regs.r[i]=bases[i];regs.r[6]=actual/8-1;
            dsp.Run(150000);
            for(unsigned i=0;i<memory.size();++i){
                if(i>=0x3b10 && i<(runtime?0x3b1a:0x3b15))continue;
                if(int16_t(dsp.DataRead(i))!=memory[i]){printf("FAIL envelope flags%u trial%u addr%x got%d expected%d\n",flags,trial,i,int16_t(dsp.DataRead(i)),memory[i]);return 1;}
            }
            for(unsigned i=0;i<5;++i)if(regs.r[i]!=bases[i]+actual)return 2;
            ++jobs;samples+=actual;
        }
    }
    printf("PASS: %u DSP envelope jobs / %u samples, static/runtime flags, 32 swap/negation combinations, unsigned volumes, wrapping ramps, zero count, aliased buffers and full memory guards\n",jobs,samples);
}
