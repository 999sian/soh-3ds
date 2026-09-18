#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "resample_state_program.h"
extern "C" void resampleReference(int16_t*,uint16_t,uint16_t,uint16_t,uint8_t,uint16_t,int16_t*);
extern "C" const int16_t* productionTable();
int main() {
    auto p=makeResampleState();Teakra::Teakra dsp({});
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    for(unsigned i=0;i<256;++i)dsp.DataWrite(0x4000+i,uint16_t(productionTable()[i]));
    uint32_t rng=0x678945;unsigned jobs=0;
    std::vector<int16_t> ref(8192);
    for(unsigned bytes:{0u,16u,32u,128u,1024u})for(unsigned pitch:{0u,1u,0x3fffu,0x8000u,0xffffu}) {
        int16_t state[16]={},expectedState[16]={};
        for(unsigned frame=0;frame<24;++frame) {
            for(unsigned i=0;i<ref.size();++i){rng=rng*1664525u+1013904223u;ref[i]=int16_t(rng>>16);dsp.DataWrite(i,uint16_t(ref[i]));}
            unsigned flags=frame==0?1:frame%3==0?2:0;
            if(frame>=16){flags=2;state[5]=expectedState[5]=-8-int(frame-15);}
            resampleReference(ref.data(),0x800,0x2000,bytes,flags,pitch,expectedState);
            for(unsigned i=0;i<16;++i)dsp.DataWrite(0x3000+i,uint16_t(state[i]));
            dsp.DataWrite(0x20,bytes?((bytes+15)&~15)/2:8);dsp.DataWrite(0x21,flags);dsp.DataWrite(0x22,pitch);dsp.DataWrite(0x23,0);
            dsp.GetRegisterState().pc=0;dsp.Run(50000);
            if(dsp.DataRead(0x23)!=1)return 3;
            for(unsigned i=0;i<16;++i)state[i]=int16_t(dsp.DataRead(0x3000+i));
            for(unsigned i=0;i<ref.size();++i)if((i<0x20 || i>0x23) && int16_t(dsp.DataRead(i))!=ref[i]){printf("FAIL memory bytes%u pitch%u frame%u word%u\n",bytes,pitch,frame,i);return 1;}
            if(memcmp(state,expectedState,sizeof(state))){printf("FAIL state bytes%u pitch%u frame%u\n",bytes,pitch,frame);return 2;}
            ++jobs;
        }
    }
    printf("PASS: %u fully DSP-state resample jobs match extracted production PCM, state and memory guards\n",jobs);
}
