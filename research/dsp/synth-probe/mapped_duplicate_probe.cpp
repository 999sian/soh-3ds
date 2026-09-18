#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
extern "C" void duplicateReference(int16_t*,unsigned,unsigned,unsigned);
int main(){
    using namespace ResidentDsp;
    auto p=makeTypedFirmware(true,true,true,true);
    uint32_t rng=0x182336;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    unsigned jobs=0;
    auto run=[&](unsigned copies,unsigned source,unsigned dest,unsigned slot,unsigned flags,bool valid){
        Teakra::Teakra dsp({});for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
        std::vector<int16_t> memory(0x6400);for(auto& x:memory)x=int16_t(next());
        memory[1]=copies;memory[2]=slot;memory[6]=flags;
        if(slot<64){memory[ParameterWordBase+slot*16]=source;memory[ParameterWordBase+slot*16+1]=dest;}
        for(unsigned i=0;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
        dsp.Run(700);if(dsp.DataRead(0x10)!=0x4458 || !dsp.RecvDataIsReady(2))return false;dsp.RecvData(2);
        unsigned commands=valid?2:1;
        for(unsigned n=0;n<commands;++n){
            dsp.DataWrite(0x100+n*4,18);dsp.DataWrite(0x101+n*4,copies);dsp.DataWrite(0x102+n*4,slot);dsp.DataWrite(0x103+n*4,flags);
            if(valid)duplicateReference(memory.data(),copies-1,AudioDmemWordBase*2+source,AudioDmemWordBase*2+dest);
        }
        dsp.DataWrite(7,commands);dsp.DataWrite(3,jobs+1);dsp.DataWrite(0,1);dsp.Run(600000);
        if(dsp.DataRead(0) || dsp.DataRead(0x11)!=jobs+1 || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=jobs+1 || dsp.DataRead(0x12)!=(valid?0:3)){printf("FAIL duplicate job%u status%u\n",jobs,dsp.DataRead(0x61));return false;}
        for(unsigned i=0x400;i<memory.size();++i){
            if(i==0x61 || (valid && i>=0x3e00 && i<0x3e80))continue;
            if(dsp.DataRead(i)!=uint16_t(memory[i])){printf("FAIL duplicate job%u address%x got%x expected%x\n",jobs,i,dsp.DataRead(i),uint16_t(memory[i]));return false;}
        }
        ++jobs;return true;
    };
    for(unsigned copies:{1u,2u,3u,12u,23u,24u})for(unsigned pattern=0;pattern<8;++pattern){
        unsigned source=pattern<4?pattern:pattern==4?64:pattern==5?128:pattern==6?2943:2944;
        unsigned dest=pattern<4?3-pattern:pattern==4?source:pattern==5?127:3072-copies*128;
        bool valid=dest+copies*128<=3072;
        if(!run(copies,source,dest,pattern*9,0,valid))return 1;
    }
    for(unsigned bad=0;bad<9;++bad){
        unsigned copies=1,source=0,dest=0,slot=63,flags=0;
        if(bad==0)copies=0;else if(bad==1)copies=25;else if(bad==2)copies=65535;
        else if(bad==3)source=2945;else if(bad==4)source=65535;else if(bad==5)dest=2945;
        else if(bad==6)dest=65535;else if(bad==7)slot=64;else flags=1;
        if(!run(copies,source,dest,slot,flags,false))return 2;
    }
    Command c={1,2,3,4};MappedParameters r{};
    for(unsigned repeats:{0u,1u,23u}){
        if(!lowerMappedDuplicate(63,repeats,0x3c1,0x3c0,c,r) || c.type!=18 || c.count!=repeats+1 || c.argument!=63 || r[0]!=1 || r[1]!=0)return 3;
    }
    const auto saved=c;const auto old=r;
    for(unsigned repeats:{24u,65535u})if(lowerMappedDuplicate(0,repeats,0x3c0,0x3c0,c,r) || std::memcmp(&c,&saved,sizeof(c)) || r!=old)return 4;
    if(lowerMappedDuplicate(64,0,0x3c0,0x3c0,c,r) || lowerMappedDuplicate(0,0,0x3bf,0x3c0,c,r) || lowerMappedDuplicate(0,0,0xf41,0x3c0,c,r) || lowerMappedDuplicate(0,0,0x3c0,0xf41,c,r) || std::memcmp(&c,&saved,sizeof(c)) || r!=old)return 5;
    printf("PASS: %u resident DSP duplicate cases, snapshot aliases, odd bytes,1..24 copies, source/output boundaries, malformed commands and full memory guards, successive snapshots and host lowering; %zu firmware words\n",jobs,p.words.size());
}
