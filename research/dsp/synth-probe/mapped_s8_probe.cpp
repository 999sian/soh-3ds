#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
extern "C" void s8Reference(int16_t*,unsigned,unsigned,unsigned,unsigned,int16_t*,int16_t*);
int main(){
    using namespace ResidentDsp;auto p=makeTypedFirmware(true,true,true,true);
    uint32_t rng=0x334815;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};unsigned jobs=0;
    auto run=[&](unsigned bytes,unsigned source,unsigned output,unsigned flags,unsigned region,bool valid,unsigned bad=0){
        Teakra::Teakra dsp({});for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
        std::vector<int16_t> mem(0x7100);for(auto& v:mem)v=int16_t(next());
        unsigned samples=((bytes+31)&~31u)/2;MappedParameters r{};
        r[0]=source;r[1]=output;r[2]=jobs%64;r[3]=(jobs+13)%64;r[5]=flags;r[6]=region;
        if(bad==1)r[2]=64;else if(bad==2)r[3]=64;else if(bad==3)r[4]=1;
        unsigned descriptorSlot=bad==4?64:63,descriptorFlags=bad==5?1:0;
        if(bad==6)samples=1;
        if(!bad && region<2){
            Command c={1,2,3,4},saved=c;MappedParameters record;record.fill(0x1234);auto old=record;
            bool lowered=lowerMappedS8(63,bytes,(region?0:AudioDmemStart)+source,AudioDmemStart+output*2,flags,r[2],r[3],region,c,record);
            if(lowered!=valid || (valid?(c.type!=19 || c.count!=samples || c.argument!=63 || c.flags || record!=r):(std::memcmp(&c,&saved,sizeof(c)) || record!=old)))return false;
        }
        for(unsigned i=0;i<16;++i)mem[ParameterWordBase+63*16+i]=r[i];
        for(unsigned i=0;i<mem.size();++i)dsp.DataWrite(i,uint16_t(mem[i]));
        dsp.Run(700);if(!dsp.RecvDataIsReady(2))return false;dsp.RecvData(2);
        unsigned n=valid?2:1;
        for(unsigned i=0;i<n;++i){
            dsp.DataWrite(0x100+i*4,19);dsp.DataWrite(0x101+i*4,samples);dsp.DataWrite(0x102+i*4,descriptorSlot);dsp.DataWrite(0x103+i*4,descriptorFlags);
            if(valid)s8Reference(mem.data(),bytes,flags,(region?PayloadWordBase:AudioDmemWordBase)*2+source,(AudioDmemWordBase+output)*2,mem.data()+AdpcmStateWordBase+r[2]*16,mem.data()+AdpcmLoopWordBase+r[3]*16);
        }
        dsp.DataWrite(7,n);dsp.DataWrite(3,jobs+1);dsp.DataWrite(0,1);dsp.Run(400000);
        if(dsp.DataRead(0) || dsp.DataRead(0x12)!=(valid?0:3) || dsp.DataRead(0x11)!=jobs+1 || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=jobs+1){printf("FAIL S8 job%u status%u\n",jobs,dsp.DataRead(0x12));return false;}
        for(unsigned i=0x400;i<mem.size();++i)if(dsp.DataRead(i)!=uint16_t(mem[i])){printf("FAIL S8 job%u addr%x got%x expected%x\n",jobs,i,dsp.DataRead(i),uint16_t(mem[i]));return false;}
        ++jobs;return true;
    };
    for(unsigned region:{0u,1u})for(unsigned flags:{0u,1u,2u})for(unsigned bytes:{0u,1u,31u,32u,33u,320u,1024u,3040u})for(unsigned parity:{0u,1u}){
        if(!run(bytes,parity,0,flags,region,true))return 1;
    }
    // Decoded PCM, rather than just initial history, overwrites future input.
    for(unsigned flags:{0u,1u,2u})for(unsigned source:{32u,33u,64u,65u,128u}){
        if(!run(320,source,0,flags,0,true) || !run(320,0,32,flags,0,true))return 7;
    }
    for(unsigned region:{0u,1u})for(unsigned edge:{0u,1u}){
        if(!run(32,(region?PayloadBytes:AudioDmemBytes)-16+edge,1504,0,region,!edge))return 2;
        if(!run(0,region?PayloadBytes:AudioDmemBytes,1520+edge,2,region,!edge))return 3;
    }
    if(!run(65535,0,0,0,0,false) || !run(32,0,0,3,0,false) || !run(32,0,0,0,2,false))return 4;
    for(unsigned bad=1;bad<=6;++bad)if(!run(32,0,0,1,0,false,bad))return 5;
    Command c={1,2,3,4},saved=c;MappedParameters r;r.fill(0x1234);auto old=r;
    for(unsigned bad=0;bad<5;++bad){
        if(lowerMappedS8(bad==0?64:0,32,bad==1?0x3bf:0x3c0,bad==2?0xfc0:0x3c0,0,bad==3?64:0,bad==4?64:0,false,c,r) || std::memcmp(&c,&saved,sizeof(c)) || r!=old)return 6;
    }
    printf("PASS: %u resident S8 cases, init/loop/continue, successive states, zero/rounded/max counts, odd bytes, DMEM aliases and payload sources, full PCM/state/memory guards; %zu firmware words\n",jobs,p.words.size());
}
