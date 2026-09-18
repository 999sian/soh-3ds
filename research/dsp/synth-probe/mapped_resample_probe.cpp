#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
extern "C" void resampleReference(int16_t*,uint16_t,uint16_t,uint16_t,uint8_t,uint16_t,int16_t*);
extern "C" const int16_t* productionTable();
int main(){
    using namespace ResidentDsp;
    auto p=makeTypedFirmware(true,true,true,true);Teakra::Teakra dsp({});
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    for(unsigned i=0;i<0x6800;++i)dsp.DataWrite(i,0);dsp.Run(700);if(dsp.DataRead(0x10)!=0x4458 || !dsp.RecvDataIsReady(2))return 1;dsp.RecvData(2);
    std::vector<int16_t> memory(0x6800);std::array<std::array<int16_t,16>,64> states{};
    uint32_t rng=0x67159a;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    unsigned sequence=0,jobs=0;
    auto run=[&](Command command,MappedParameters params,unsigned originalBytes,bool valid){
        for(auto& x:memory)x=int16_t(next());
        for(unsigned i=0;i<256;++i)memory[0x4000+i]=productionTable()[i];
        for(unsigned slot=0;slot<64;++slot)for(unsigned i=0;i<16;++i)memory[ResampleStateWordBase+slot*16+i]=states[slot][i];
        unsigned slot=command.argument<64?command.argument:0;
        for(unsigned i=0;i<16;++i)memory[ParameterWordBase+slot*16+i]=params[i];
        for(unsigned i=0x400;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
        if(valid)resampleReference(memory.data(),(AudioDmemWordBase+params[0])*2,(AudioDmemWordBase+params[1])*2,originalBytes,params[4],params[3],memory.data()+ResampleStateWordBase+params[2]*16);
        dsp.DataWrite(0x100,command.type);dsp.DataWrite(0x101,command.count);dsp.DataWrite(0x102,command.argument);dsp.DataWrite(0x103,command.flags);
        dsp.DataWrite(7,1);dsp.DataWrite(3,++sequence);dsp.DataWrite(0,1);dsp.Run(200000);
        if(dsp.DataRead(0) || dsp.DataRead(0x12)!=(valid?0:3) || dsp.DataRead(0x11)!=sequence || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=sequence){printf("FAIL mapped resample job%u dispatch status%u wanted%u\n",jobs,dsp.DataRead(0x12),valid?0:3);return false;}
        for(unsigned i=0x400;i<memory.size();++i)if(dsp.DataRead(i)!=uint16_t(memory[i])){printf("FAIL mapped resample job%u in%u out%u pitch%u flags%u addr%x got%d expected%d\n",jobs,params[0],params[1],params[3],params[4],i,int16_t(dsp.DataRead(i)),memory[i]);return false;}
        if(valid)for(unsigned i=0;i<16;++i)states[params[2]][i]=memory[ResampleStateWordBase+params[2]*16+i];
        ++jobs;return true;
    };
    for(unsigned trial=0;trial<512;++trial){
        unsigned stateSlot=trial%64,flags=trial<64?1:trial%3==0?2:0;
        const uint16_t counts[]={0,1,16,32,160,320,352,384};unsigned bytes=counts[(trial/8)%8];
        const uint16_t pitches[]={0,1,0x3fff,0x8000,0xffff};unsigned pitch=pitches[(trial/64)%5];
        unsigned input=16+(trial%8),output=trial%4==0?input:trial%4==1?input+2:trial%4==2?input-2:700+trial%8;
        Command command;MappedParameters params;
        if(!lowerMappedResample(trial%64,bytes,0x3c0+input*2+(trial&1),0x3c0+output*2,pitch,flags,stateSlot,command,params))return 2;
        if(!run(command,params,bytes,true))return 3;
    }
    // Saved adjustment affects pointer movement only under flag2.
    for(int adjustment:{0,-9,-10,-11,-12,-13,-14,-15})for(unsigned phase:{0u,65535u}){
        states[63][5]=adjustment;states[63][4]=int16_t(phase);
        Command command;MappedParameters params;
        if(!lowerMappedResample(63,32,0x3c0+19*2,0x3c0+600*2,0xffff,2,63,command,params) || !run(command,params,32,true))return 4;
    }
    for(unsigned input:{4u,5u,8u,1532u,1533u})for(unsigned pitch:{0u,1u}){
        states[63][4]=int16_t(65535);states[63][5]=0;
        Command command;MappedParameters params;
        if(!lowerMappedResample(63,16,0x3c0+input*2,0x3c0+600*2,pitch,0,63,command,params))return 7;
        if(!run(command,params,16,input<=1532))return 8;
    }
    for(auto pair:std::array<std::array<unsigned,2>,2>{{{0,63},{63,0}}}){
        Command command;MappedParameters params;
        if(!lowerMappedResample(pair[0],3072,0x3c0+16*2,0x3c0,0,1,pair[1],command,params) || !run(command,params,3072,true))return 9;
    }
    for(unsigned bad=0;bad<13;++bad){
        Command command={12,8,63,0};MappedParameters params{};params[0]=16;params[1]=700;params[2]=63;params[3]=0x8000;params[4]=1;
        switch(bad){case 0:params[0]=3;break;case 1:params[0]=7;params[4]=2;break;case 2:params[0]=1536;break;case 3:params[1]=1535;break;case 4:params[2]=64;break;case 5:params[4]=3;break;case 6:command.argument=64;break;case 7:command.count=0;break;case 8:command.count=9;break;case 9:command.count=65535;break;case 10:command.flags=1;break;case 11:params[0]=1533;params[3]=0;break;case 12:params[4]=2;states[63][5]=-8;break;}
        if(!run(command,params,0,false))return 5;
    }
    for(auto& x:memory)x=int16_t(next());
    for(unsigned i=0;i<256;++i)memory[0x4000+i]=productionTable()[i];
    for(unsigned i=0x400;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
    for(unsigned batch=0;batch<32;++batch){
        std::array<Command,2> commands;std::array<MappedParameters,2> records;
        for(unsigned i=0;i<2;++i){
            unsigned stateSlot=batch%2?7:9+i,slot=i?63:0,flags=batch<2?1:0;
            if(!lowerMappedResample(slot,320,0x3c0+(16+i*2)*2,0x3c0+(400+i*2)*2,i?0x4321:0x9876,flags,stateSlot,commands[i],records[i]))return 10;
            for(unsigned n=0;n<16;++n){memory[ParameterWordBase+slot*16+n]=records[i][n];dsp.DataWrite(ParameterWordBase+slot*16+n,records[i][n]);}
            auto c=commands[i];dsp.DataWrite(0x100+i*4,c.type);dsp.DataWrite(0x101+i*4,c.count);dsp.DataWrite(0x102+i*4,c.argument);dsp.DataWrite(0x103+i*4,c.flags);
            resampleReference(memory.data(),(AudioDmemWordBase+records[i][0])*2,(AudioDmemWordBase+records[i][1])*2,320,flags,records[i][3],memory.data()+ResampleStateWordBase+stateSlot*16);
        }
        dsp.DataWrite(7,2);dsp.DataWrite(3,++sequence);dsp.DataWrite(0,1);dsp.Run(200000);
        if(dsp.DataRead(0) || dsp.DataRead(0x12) || dsp.DataRead(0x11)!=sequence || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=sequence)return 11;
        for(unsigned i=0x400;i<memory.size();++i)if(dsp.DataRead(i)!=uint16_t(memory[i])){printf("FAIL mapped resample batch%u addr%x\n",batch,i);return 12;}
    }
    dsp.SendData(2,0x8000);dsp.Run(700);if(!dsp.RecvDataIsReady(2) || dsp.RecvData(2))return 6;
    printf("PASS: %u mapped DSP resample jobs,64 persistent state slots, unaligned input-relative history, aliases, flags/pitch/phase, adjustment and preflight guards, plus32 multi-resample batches; %zu firmware words\n",jobs,p.words.size());
}
