#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
int main(){
    using namespace ResidentDsp;auto p=makeTypedFirmware(true,true,true,true);unsigned jobs=0;
    uint32_t rng=0x361281;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    auto run=[&](unsigned bytes,unsigned source,unsigned destination,bool valid){
        Teakra::Teakra dsp({});for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
        std::vector<int16_t> mem(0x7500);for(auto& v:mem)v=int16_t(next());
        for(unsigned i=0;i<mem.size();++i)dsp.DataWrite(i,uint16_t(mem[i]));
        dsp.Run(700);if(!dsp.RecvDataIsReady(2))return false;dsp.RecvData(2);
        const Command commands[]={{24,uint16_t(bytes),uint16_t(source),uint16_t(destination)},
                                  {8,uint16_t(bytes),0,uint16_t(destination)},
                                  {24,uint16_t(bytes),uint16_t(source),uint16_t(destination)}};
        if(valid){
            auto* data=reinterpret_cast<unsigned char*>(mem.data());
            std::memcpy(data+AudioDmemWordBase*2+destination,data+PayloadWordBase*2+source,bytes);
            std::memset(data+AudioDmemWordBase*2+destination,0,bytes);
            std::memcpy(data+AudioDmemWordBase*2+destination,data+PayloadWordBase*2+source,bytes);
        }
        unsigned count=valid?3:1;
        for(unsigned i=0;i<count;++i){auto c=commands[i];dsp.DataWrite(0x100+i*4,c.type);dsp.DataWrite(0x101+i*4,c.count);dsp.DataWrite(0x102+i*4,c.argument);dsp.DataWrite(0x103+i*4,c.flags);}
        dsp.DataWrite(7,count);dsp.DataWrite(3,jobs+1);dsp.DataWrite(0,1);dsp.Run(1000000);
        if(dsp.DataRead(0) || dsp.DataRead(0x12)!=(valid?0:3) || dsp.DataRead(0x11)!=jobs+1 || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=jobs+1){printf("FAIL payload load%u status%u\n",jobs,dsp.DataRead(0x12));return false;}
        for(unsigned i=0x400;i<mem.size();++i)if(dsp.DataRead(i)!=uint16_t(mem[i])){printf("FAIL payload load%u addr%x\n",jobs,i);return false;}
        ++jobs;return true;
    };
    for(unsigned bytes:{0u,1u,2u,15u,16u,17u,31u,32u,160u,3071u,3072u})for(unsigned pattern=0;pattern<4;++pattern){
        unsigned source=pattern==0?0:pattern==1?1:PayloadBytes-bytes,destination=pattern==0?0:pattern==1?1:AudioDmemBytes-bytes;
        bool valid=destination+bytes<=AudioDmemBytes;
        Command c={1,2,3,4},saved=c;
        bool lowered=lowerMappedLoad(source,AudioDmemStart+destination,bytes,c);
        if(lowered!=valid || (!valid && std::memcmp(&c,&saved,sizeof(c))) || (valid && (c.type!=24 || c.count!=bytes || c.argument!=source || c.flags!=destination)))return 1;
        if(!run(bytes,source,destination,valid))return 2;
    }
    for(unsigned bad=0;bad<6;++bad){
        unsigned bytes=16,source=0,dest=0;
        if(bad==0)bytes=3073;else if(bad==1)bytes=65535;else if(bad==2)source=PayloadBytes-15;else if(bad==3)source=65535;else if(bad==4)dest=AudioDmemBytes-15;else dest=65535;
        if(!run(bytes,source,dest,false))return 3;
    }
    Command c={1,2,3,4},saved=c;
    if(lowerMappedLoad(0,0x3bf,0,c) || lowerMappedLoad(65535,0x3c0,0,c) || std::memcmp(&c,&saved,sizeof(c)))return 4;
    printf("PASS: %u resident payload load cases, exact bytes, odd endpoints, load-clear-load ordering, immutable source guards, zero/end/max extents and rejected descriptors; %zu firmware words\n",jobs,p.words.size());
}
