#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "resample_program.h"
extern "C" void resampleReference(int16_t*,uint16_t,uint16_t,uint16_t,uint8_t,uint16_t,int16_t*);
extern "C" const int16_t* productionTable();
// Host-side state adapter for validating kernel integration. State setup/teardown
// will subsequently move to DSP firmware; only sample processing is DSP here.
static void candidate(Teakra::Teakra& dsp,const Program& p,unsigned bytes,unsigned flags,unsigned pitch,int16_t* state) {
    int16_t tmp[16]={};if(!(flags&1))memcpy(tmp,state,sizeof(tmp));
    unsigned initial=0x400,pos=initial;
    if(flags&2){for(unsigned i=0;i<8;++i)dsp.DataWrite(pos-8+i,uint16_t(tmp[8+i]));pos-=int(tmp[5])>>1;}
    pos-=4;for(unsigned i=0;i<4;++i)dsp.DataWrite(pos+i,uint16_t(tmp[i]));
    unsigned n=bytes?((bytes+15)&~15)/2:8;
    auto& r=dsp.GetRegisterState();r.pc=0;r.r[0]=pos;r.r[2]=0x1000;r.r[3]=0x4000;r.r[4]=uint16_t(tmp[4]);r.r[5]=pitch<<1;r.r[7]=pitch>>15;r.r[6]=n-1;
    dsp.Run(1+(p.instructions-1)*n);
    pos=r.r[0];state[4]=r.r[4];
    for(unsigned i=0;i<4;++i)state[i]=int16_t(dsp.DataRead(pos+i));
    int alignment=(int(pos)-int(initial)+4)&7;pos-=alignment;
    state[5]=alignment?-8-alignment:0;
    for(unsigned i=0;i<8;++i)state[8+i]=int16_t(dsp.DataRead(pos+i));
}
int main() {
    auto p=makeResampleBuffer();Teakra::Teakra dsp({});
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
            candidate(dsp,p,bytes,flags,pitch,state);
            for(unsigned i=0;i<ref.size();++i)if(int16_t(dsp.DataRead(i))!=ref[i]){printf("FAIL memory bytes%u pitch%u frame%u word%u\n",bytes,pitch,frame,i);return 1;}
            if(memcmp(state,expectedState,sizeof(state))){printf("FAIL state bytes%u pitch%u frame%u\n",bytes,pitch,frame);return 2;}
            ++jobs;
        }
    }
    printf("PASS: %u stateful DSP resample jobs match extracted production PCM, state and memory guards\n",jobs);
}
