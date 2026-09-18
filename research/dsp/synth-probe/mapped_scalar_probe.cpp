#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
extern "C" void hiLoReference(int16_t*,unsigned,unsigned,unsigned);
extern "C" void tableReference(int16_t*,unsigned,unsigned,unsigned,unsigned);
int main(){
    using namespace ResidentDsp;auto p=makeTypedFirmware(true,true,true,true);
    uint32_t rng=0x632815;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};unsigned jobs=0;
    for(unsigned type:{22u,23u})for(unsigned trial=0;trial<(type==22?256u:96u);++trial){
        const uint16_t counts[]={0,1,31,32,33,64,320,768,1536,3072,65535};unsigned bytes=counts[trial%11];
        Command c={1,2,3,4},saved=c;MappedParameters r;r.fill(0x1234);auto old=r;
        unsigned input=type==22?0:(trial%4)*2,output=bytes>=1536?0:3-trial%4,offset=trial%2;
        bool valid=type==22?lowerMappedHiLo(trial%64,trial,bytes,0x3c0+(trial&1),c,r):lowerMappedTableMultiply(trial%64,offset,bytes,0x3c0+input,0x3c0+output,c,r);
        if(!valid){if(std::memcmp(&c,&saved,sizeof(c)) || r!=old)return 1;continue;}
        Teakra::Teakra dsp({});for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
        std::vector<int16_t> mem(0x7500);for(auto& v:mem)v=int16_t(next());
        // Include extrema and products around the saturation limits.
        const int16_t values[]={-32768,32767,-1,0,1,2,-2,16};
        for(unsigned i=0;i<64;++i)mem[AudioDmemWordBase+i]=values[(i+trial)%8];
        for(unsigned i=0;i<16;++i)mem[ParameterWordBase+c.argument*16+i]=r[i];
        for(unsigned i=0;i<mem.size();++i)dsp.DataWrite(i,uint16_t(mem[i]));
        dsp.Run(700);if(!dsp.RecvDataIsReady(2))return 2;dsp.RecvData(2);
        for(unsigned i=0;i<2;++i){
            if(type==22)hiLoReference(mem.data(),trial,bytes,AudioDmemWordBase*2+(trial&1));
            else tableReference(mem.data(),offset,bytes,AudioDmemWordBase*2+output,AudioDmemWordBase*2+input);
            dsp.DataWrite(0x100+i*4,c.type);dsp.DataWrite(0x101+i*4,c.count);dsp.DataWrite(0x102+i*4,c.argument);dsp.DataWrite(0x103+i*4,c.flags);
        }
        dsp.DataWrite(7,2);dsp.DataWrite(3,trial+1);dsp.DataWrite(0,1);dsp.Run(400000);
        if(dsp.DataRead(0) || dsp.DataRead(0x12) || dsp.DataRead(0x11)!=trial+1 || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=trial+1){printf("FAIL scalar type%u trial%u status%u\n",type,trial,dsp.DataRead(0x12));return 3;}
        for(unsigned i=0x400;i<mem.size();++i){if(type==23 && i>=0x3f00 && i<0x3f20)continue;if(dsp.DataRead(i)!=uint16_t(mem[i])){printf("FAIL scalar type%u trial%u addr%x got%x expected%x\n",type,trial,i,dsp.DataRead(i),uint16_t(mem[i]));return 4;}}
        ++jobs;
    }
    for(unsigned type:{22u,23u})for(unsigned bad=0;bad<9;++bad){
        Teakra::Teakra dsp({});for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
        std::vector<int16_t> mem(0x7500);for(auto& v:mem)v=int16_t(next());
        Command c={uint16_t(type),32,63,0};MappedParameters r{};
        if(bad==0)c.count=0;else if(bad==1)c.count=7;else if(bad==2)c.count=65535;
        else if(bad==3)c.argument=64;else if(bad==4)c.flags=1;else if(bad==5)r[0]=65535;
        else if(bad==6)r[0]=1505;else if(bad==7)r[1]=type==22?256:1536;
        else r[1]=65535;
        for(unsigned i=0;i<16;++i)mem[ParameterWordBase+63*16+i]=r[i];
        for(unsigned i=0;i<mem.size();++i)dsp.DataWrite(i,uint16_t(mem[i]));
        dsp.Run(700);if(!dsp.RecvDataIsReady(2))return 5;dsp.RecvData(2);
        dsp.DataWrite(0x100,c.type);dsp.DataWrite(0x101,c.count);dsp.DataWrite(0x102,c.argument);dsp.DataWrite(0x103,c.flags);
        dsp.DataWrite(7,1);dsp.DataWrite(3,bad+1);dsp.DataWrite(0,1);dsp.Run(400000);
        if(dsp.DataRead(0) || dsp.DataRead(0x12)!=3 || dsp.DataRead(0x11)!=bad+1 || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=bad+1)return 6;
        for(unsigned i=0x400;i<mem.size();++i)if(dsp.DataRead(i)!=uint16_t(mem[i]))return 7;
        ++jobs;
    }
    Command c={1,2,3,4},saved=c;MappedParameters r;r.fill(0x1234);auto old=r;
    for(unsigned bad=0;bad<4;++bad){
        bool ok=lowerMappedHiLo(bad==0?64:0,255,bad==1?1537:0,bad==2?0x3bf:bad==3?0xfc0:0x3c0,c,r);
        if(ok || std::memcmp(&c,&saved,sizeof(c)) || r!=old)return 8;
        ok=lowerMappedTableMultiply(bad==0?64:0,255,bad==1?65535:0,bad==2?65535:0x3c0,bad==3?0xfc0:0x3c0,c,r);
        if(ok || std::memcmp(&c,&saved,sizeof(c)) || r!=old)return 9;
    }
    printf("PASS: %u resident HiLo/table-multiply jobs, production count quirks, signed saturation, aliases and snapshot preservation, odd-byte arguments, repeated queued calls, rejected descriptors and full memory guards; %zu firmware words\n",jobs,p.words.size());
}
