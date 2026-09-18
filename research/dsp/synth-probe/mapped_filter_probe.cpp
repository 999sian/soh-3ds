#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
extern "C" void mappedFilterSetupReference(int16_t*,unsigned,const int16_t*);
extern "C" void mappedFilterProcessReference(int16_t*,unsigned,int16_t*,unsigned);
int main(){
    using namespace ResidentDsp;auto p=makeTypedFirmware(true,true,true,true);
    uint32_t rng=0x253415;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    for(unsigned trial=0;trial<96;++trial){
        const uint16_t counts[]={0,1,15,16,17,320,352,384,1024,3072,65521,65535};unsigned bytes=counts[trial%12];
        Teakra::Teakra dsp({});for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
        std::vector<int16_t> mem(0x7500);for(auto& v:mem)v=int16_t(next());
        std::array<int16_t,8> coeff;for(auto& v:coeff)v=int16_t(next());
        std::array<Command,3> commands;std::array<MappedParameters,3> records;
        unsigned offset=bytes==3072?0:trial%4;
        if(!lowerMappedFilterSetup(0,bytes,coeff,commands[0],records[0]) ||
           !lowerMappedFilter(1,0x3c0+offset*2+(trial&1),trial%64,trial&1,commands[1],records[1]) ||
           !lowerMappedFilter(2,0x3c0,(trial+1)%64,!(trial&1),commands[2],records[2]))return 1;
        for(unsigned slot=0;slot<3;++slot)for(unsigned i=0;i<16;++i)mem[ParameterWordBase+slot*16+i]=records[slot][i];
        for(unsigned i=0;i<mem.size();++i)dsp.DataWrite(i,uint16_t(mem[i]));
        dsp.Run(700);if(!dsp.RecvDataIsReady(2))return 2;dsp.RecvData(2);
        mappedFilterSetupReference(mem.data(),bytes,coeff.data());
        for(unsigned i=1;i<3;++i)mappedFilterProcessReference(mem.data(),records[i][2],mem.data()+FilterStateWordBase+records[i][1]*16,(AudioDmemWordBase+records[i][0])*2);
        for(unsigned i=0;i<3;++i){auto c=commands[i];dsp.DataWrite(0x100+i*4,c.type);dsp.DataWrite(0x101+i*4,c.count);dsp.DataWrite(0x102+i*4,c.argument);dsp.DataWrite(0x103+i*4,c.flags);}
        dsp.DataWrite(7,3);dsp.DataWrite(3,trial+1);dsp.DataWrite(0,1);dsp.Run(1400000);
        if(dsp.DataRead(0) || dsp.DataRead(0x12) || dsp.DataRead(0x11)!=trial+1 || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=trial+1){printf("FAIL mapped filter trial%u status%u\n",trial,dsp.DataRead(0x12));return 3;}
        for(unsigned i=0x400;i<mem.size();++i){
            if(i>=0x3c20 && i<0x3c30)continue;
            if(dsp.DataRead(i)!=uint16_t(mem[i])){printf("FAIL mapped filter trial%u addr%x got%x expected%x\n",trial,i,dsp.DataRead(i),uint16_t(mem[i]));return 4;}
        }
    }
    unsigned boundaries=0;
    for(unsigned bad=0;bad<19;++bad){
        Teakra::Teakra dsp({});for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
        std::vector<int16_t> mem(0x7500);for(auto& v:mem)v=int16_t(next());
        Command c={21,0,63,0};MappedParameters r{};mem[FilterCountWord]=8;
        bool valid=bad>=17;
        if(bad==0)c.count=1;else if(bad==1)c.argument=64;else if(bad==2)c.flags=1;
        else if(bad==3)r[0]=65535;else if(bad==4)r[0]=1529;
        else if(bad==5)r[1]=64;else if(bad==6)r[2]=2;
        else if(bad==7)mem[FilterCountWord]=7;else if(bad==8)mem[FilterCountWord]=1544;
        else if(bad==9)mem[FilterCountWord]=65535;else if(bad==10){mem[FilterCountWord]=0;r[0]=1536;}
        else if(bad<17){c.type=20;c.count=8;if(bad==11)c.count=7;else if(bad==12)c.count=1544;else if(bad==13)c.count=65535;else if(bad==14)c.argument=64;else if(bad==15)c.flags=1;else c.count=1;}
        else {r[0]=1528;mem[FilterCountWord]=bad==17?8:0;}
        for(unsigned i=0;i<16;++i)mem[ParameterWordBase+63*16+i]=r[i];
        for(unsigned i=0;i<mem.size();++i)dsp.DataWrite(i,uint16_t(mem[i]));
        dsp.Run(700);if(!dsp.RecvDataIsReady(2))return 5;dsp.RecvData(2);
        if(valid)mappedFilterProcessReference(mem.data(),0,mem.data()+FilterStateWordBase,(AudioDmemWordBase+r[0])*2);
        dsp.DataWrite(0x100,c.type);dsp.DataWrite(0x101,c.count);dsp.DataWrite(0x102,c.argument);dsp.DataWrite(0x103,c.flags);
        dsp.DataWrite(7,1);dsp.DataWrite(3,bad+1);dsp.DataWrite(0,1);dsp.Run(400000);
        if(dsp.DataRead(0) || dsp.DataRead(0x12)!=(valid?0:3) || dsp.DataRead(0x11)!=bad+1 || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=bad+1)return 6;
        for(unsigned i=0x400;i<mem.size();++i){if(valid && i>=0x3c20 && i<0x3c30)continue;if(dsp.DataRead(i)!=uint16_t(mem[i])){printf("FAIL filter boundary%u addr%x\n",bad,i);return 7;}}
        ++boundaries;
    }
    Command c={1,2,3,4},saved=c;MappedParameters r;r.fill(0x1234);auto old=r;std::array<int16_t,8> coeff{};
    for(unsigned bad=0;bad<6;++bad){
        bool ok=bad<2?lowerMappedFilterSetup(bad==0?64:0,bad==1?3073:0,coeff,c,r):
            lowerMappedFilter(bad==2?64:0,bad==3?0x3bf:0x3c0,bad==4?64:0,bad==5?2:0,c,r);
        if(ok || std::memcmp(&c,&saved,sizeof(c)) || r!=old)return 8;
    }
    printf("PASS: 96 resident filter setup/two-state chains, global coefficient mutation, init/history, zero/rounded/wrapped/max counts, all persistent slots, PCM/state/coefficients and memory guards, plus%u boundary/rejection checks; %zu firmware words\n",boundaries,p.words.size());
}
