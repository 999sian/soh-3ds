#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "dynamic_adpcm_program.h"
extern "C" void adpcmReference(int16_t*,unsigned,unsigned,int16_t*,int16_t*,const int16_t*);
int main(){
    uint32_t rng=0x39ad2;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    unsigned jobs=0;
    for(bool twoBit:{false,true}){
        auto p=makeDynamicAdpcm(twoBit);Teakra::Teakra dsp({});
        for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
        std::vector<int16_t> mem(0x4000);int16_t state[16]={},loop[16],book[128];
        for(auto& x:loop)x=int16_t(next());for(auto& x:book)x=int16_t(next());
        for(unsigned frame=0;frame<187;++frame){
            unsigned blocks=frame%40<33?frame%40:frame%2?33:65535;
            unsigned flags=frame%4==0?1:frame%4==1?2:0;
            if(frame>=160){const unsigned edgeCounts[]={0,1,32};blocks=edgeCounts[(frame-160)/9];flags=((frame-160)/3)%3;}
            for(auto& x:mem)x=int16_t(next());
            for(unsigned i=0;i<16;++i){mem[0x3800+i]=state[i];mem[0x3900+i]=loop[i];mem[0x2800+i]=uint16_t(1u<<i);}
            for(unsigned i=0;i<128;++i)mem[0x2000+i]=book[i];
            auto bytes=reinterpret_cast<uint8_t*>(mem.data())+0x1000;
            for(unsigned b=0;b<32;++b)bytes[b*(twoBit?5:9)]=((frame%16)<<4)|(b%8);
            // Invalid later selectors must be caught before output/history mutation.
            bool invalidSelector=frame<160 && blocks>1 && blocks<=32 && frame%13==0;
            if(invalidSelector)bytes[(blocks-1)*(twoBit?5:9)]|=8;
            bool invalidFlags=frame<160 && frame%19==0; if(invalidFlags)flags=65535;
            for(unsigned i=0;i<mem.size();++i)dsp.DataWrite(i,uint16_t(mem[i]));
            bool legal=blocks<=32 && !invalidSelector && !invalidFlags;
            if(legal)adpcmReference(mem.data(),blocks*32,flags|(twoBit?4:0),state,loop,book);
            dsp.DataWrite(0x24,blocks);dsp.DataWrite(0x25,flags);dsp.DataWrite(0x27,65535);
            dsp.GetRegisterState().pc=0;dsp.Run(300000);
            if(dsp.DataRead(0x27)!=(legal?0:3)){printf("FAIL dynamic status bits%u frame%u\n",twoBit?2:4,frame);return 1;}
            for(unsigned i=0;i<16;++i)mem[0x3800+i]=state[i];
            for(unsigned i=0x100;i<mem.size();++i){
                if(legal && blocks && ((i>=0x1000 && i<0x1008)||(i>=0x2500 && i<0x2510)))continue;
                if(int16_t(dsp.DataRead(i))!=mem[i]){printf("FAIL dynamic bits%u frame%u blocks%u addr%x got%d expected%d\n",twoBit?2:4,frame,blocks,i,int16_t(dsp.DataRead(i)),mem[i]);return 2;}
            }
            ++jobs;
        }
        printf("Dynamic %ubit kernel: %zu words\n",twoBit?2:4,p.words.size());
    }
    printf("PASS: %u variable-block DSP ADPCM jobs, counts0..32, rejected33/65535, both packings, state continuity, odd-byte starts and preflight rejection guards\n",jobs);
}
