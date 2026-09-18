#include <algorithm>
#include <cstdio>
#include <vector>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "resample_program.h"
int main() {
    auto p=makeResampleBuffer();Teakra::Teakra dsp({});
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    uint32_t rng=0x153901;unsigned checked=0;
    auto next=[&](){rng=rng*1664525u+1013904223u;return int16_t(rng>>16);};
    std::vector<int16_t> samples(2048),table(256);
    for(unsigned n:{1u,8u,128u,512u})for(unsigned step:{0u,1u,0x7fffu,0x8000u,0xffffu,0x10000u,0x1fffeu})for(unsigned initial:{0u,1u,1023u,0x7fffu,0xffffu}) {
        for(unsigned i=0;i<samples.size();++i){samples[i]=next();dsp.DataWrite(0x1000+i,uint16_t(samples[i]));}
        for(unsigned i=0;i<table.size();++i){table[i]=next();dsp.DataWrite(0x4000+i,uint16_t(table[i]));}
        dsp.DataWrite(0x5fff,0xa56b);dsp.DataWrite(0x6000+n,0x5719);
        auto& r=dsp.GetRegisterState();r.pc=0;r.r[0]=0x1000;r.r[2]=0x6000;r.r[3]=0x4000;r.r[4]=initial;r.r[5]=step;r.r[7]=step>>16;r.r[6]=n-1;
        dsp.Run(1+(p.instructions-1)*n);
        unsigned phase=initial,pos=0;
        for(unsigned i=0;i<n;++i){
            int sum=0;unsigned t=(phase>>10)*4;
            for(unsigned k=0;k<4;++k)sum+=(int32_t(samples[pos+k])*table[t+k]+0x4000)>>15;
            int16_t want=std::clamp(sum,-32768,32767),actual=int16_t(dsp.DataRead(0x6000+i));
            if(want!=actual){printf("FAIL n%u step%u phase%u i%u actual%d want%d\n",n,step,initial,i,actual,want);return 1;}
            phase+=step;pos+=phase>>16;phase&=65535;++checked;
        }
        if(r.r[0]!=0x1000+pos || r.r[4]!=phase || r.r[2]!=0x6000+n || dsp.DataRead(0x5fff)!=0xa56b || dsp.DataRead(0x6000+n)!=0x5719)return 2;
    }
    printf("PASS: %u DSP resampled buffer samples, pitch/phase boundaries, pointers and output guards\n",checked);
}
