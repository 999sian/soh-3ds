#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "typed_firmware.h"
extern "C" void adpcmReference(int16_t*,unsigned,unsigned,int16_t*,int16_t*,const int16_t*);
extern "C" void resampleReference(int16_t*,uint16_t,uint16_t,uint16_t,uint8_t,uint16_t,int16_t*);
extern "C" const int16_t* productionTable();
int main() {
    auto p=makeTypedFirmware();Teakra::Teakra dsp({});
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    for(unsigned i=0;i<0x4100;++i)dsp.DataWrite(i,0);
    dsp.Run(500);if(dsp.DataRead(0x10)!=0x4454 || !dsp.RecvDataIsReady(2))return 1;dsp.RecvData(2);
    uint32_t rng=0x152637;unsigned seq=0,jobs=0;auto next=[&](){rng=rng*1664525u+1013904223u;return int16_t(rng>>16);};
    std::vector<int16_t> memory(0x4000);
    for(unsigned type:{1u,2u,3u})for(unsigned flags:{0u,1u,2u})for(unsigned trial=0;trial<16;++trial) {
        for(unsigned i=0;i<memory.size();++i){memory[i]=next();if(i>=0x40)dsp.DataWrite(i,uint16_t(memory[i]));}
        int16_t state[16],loop[16],book[128];for(auto& x:state)x=next();for(auto& x:loop)x=next();for(auto& x:book)x=next();
        state[5]=trial&1?0:-9-int(trial%7);
        unsigned count=type==1?((trial%4==0)?0:(trial%4==1)?8:(trial%4==2)?128:512):16;
        unsigned pitch=uint16_t(next());
        for(unsigned i=0;i<256;++i)dsp.DataWrite(0x4000+i,uint16_t(productionTable()[i]));
        for(unsigned i=0;i<16;++i){dsp.DataWrite((type==1?0x3a00:0x3800)+i,uint16_t(state[i]));dsp.DataWrite(0x3900+i,uint16_t(loop[i]));dsp.DataWrite(0x2800+i,uint16_t(1u<<i));}
        for(unsigned i=0;i<128;++i)dsp.DataWrite(0x2000+i,uint16_t(book[i]));
        reinterpret_cast<uint8_t*>(memory.data())[0x1000]=(trial<<4)|(trial%8);
        dsp.DataWrite(0x800,uint16_t(memory[0x800]));
        if(type==1)resampleReference(memory.data(),0x800,0x2000,count*2,flags,pitch,state);
        else adpcmReference(memory.data(),32,flags|(type==3?4:0),state,loop,book);
        dsp.DataWrite(7,0);dsp.DataWrite(1,count);dsp.DataWrite(2,pitch);dsp.DataWrite(3,++seq);dsp.DataWrite(5,type);dsp.DataWrite(6,flags);dsp.DataWrite(0,1);
        dsp.Run(50000);
        if(dsp.DataRead(0)!=0 || dsp.DataRead(0x11)!=seq || dsp.DataRead(0x12)!=0){printf("FAIL dispatch type%u flags%u trial%u status%u\n",type,flags,trial,dsp.DataRead(0x12));return 2;}
        unsigned out=type==1?0x1000:0x3000,n=type==1?(count?count:8):32;
        for(unsigned i=0;i<n;++i)if(int16_t(dsp.DataRead(out+i))!=memory[out+i]){printf("FAIL PCM type%u flags%u trial%u sample%u\n",type,flags,trial,i);return 3;}
        for(unsigned i=0;i<16;++i)if(int16_t(dsp.DataRead((type==1?0x3a00:0x3800)+i))!=state[i]){printf("FAIL state type%u flags%u trial%u index%u\n",type,flags,trial,i);return 4;}
        ++jobs;
    }
    for(unsigned type:{5u,17u,65535u}){dsp.DataWrite(7,0);dsp.DataWrite(5,type);dsp.DataWrite(6,0);dsp.DataWrite(3,++seq);dsp.DataWrite(0,1);dsp.Run(1000);if(dsp.DataRead(0x12)!=3 || dsp.DataRead(0) || dsp.DataRead(0x11)!=seq)return 5;}
    dsp.SendData(2,0x8000);dsp.Run(500);if(!dsp.RecvDataIsReady(2) || dsp.RecvData(2)!=0)return 6;
    printf("PASS: %u typed DSP resample/ADPCM jobs with production PCM/state, invalid types and shutdown; %zu program words\n",jobs,p.words.size());
}
