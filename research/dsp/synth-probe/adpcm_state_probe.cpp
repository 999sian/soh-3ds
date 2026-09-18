#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "adpcm_program.h"
extern "C" void adpcmReference(int16_t*,unsigned,unsigned,int16_t*,int16_t*,const int16_t*);
int main() {
    Teakra::Teakra dsp({});uint32_t rng=0x563901;
    auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    std::vector<int16_t> mem(0x4000);unsigned checked=0;
    for(bool twoBit:{false,true})for(unsigned blocks:{0u,1u,2u,4u,16u}) {
        int16_t state[16]={},expectedState[16]={},loop[16],book[128];
        for(auto& x:loop)x=next();for(auto& x:book)x=next();
        for(unsigned frame=0;frame<16;++frame) {
            unsigned flags=frame==0?1:frame%3==0?2:0;
            auto p=makeAdpcmState(twoBit,blocks,flags);
            for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
            for(auto& x:mem)x=next();
            auto bytes=reinterpret_cast<uint8_t*>(mem.data())+0x1000;
            for(unsigned b=0;b<blocks;++b)bytes[b*(twoBit?5:9)]=((frame%16)<<4)|(b%8);
            for(unsigned i=0;i<mem.size();++i)dsp.DataWrite(i,uint16_t(mem[i]));
            for(unsigned i=0;i<16;++i){dsp.DataWrite(0x3800+i,uint16_t(state[i]));dsp.DataWrite(0x3900+i,uint16_t(loop[i]));dsp.DataWrite(0x2800+i,uint16_t(1u<<i));}
            for(unsigned i=0;i<128;++i)dsp.DataWrite(0x2000+i,uint16_t(book[i]));
            adpcmReference(mem.data(),blocks*32,flags|(twoBit?4:0),expectedState,loop,book);
            dsp.GetRegisterState().pc=0;dsp.Run(p.instructions);
            for(unsigned i=0;i<16;++i)state[i]=int16_t(dsp.DataRead(0x3800+i));
            if(memcmp(state,expectedState,sizeof(state))){printf("FAIL ADPCM state blocks%u frame%u bits%u\n",blocks,frame,twoBit?2:4);return 1;}
            for(unsigned i=0x2fff;i<=0x3010+blocks*16;++i)if(int16_t(dsp.DataRead(i))!=mem[i]){printf("FAIL ADPCM output blocks%u frame%u word%x bits%u\n",blocks,frame,i,twoBit?2:4);return 2;}
            ++checked;
        }
    }
    printf("PASS: %u complete DSP ADPCM jobs match extracted production PCM/state, init/loop/continue, packed odd-byte blocks and guards\n",checked);
}
