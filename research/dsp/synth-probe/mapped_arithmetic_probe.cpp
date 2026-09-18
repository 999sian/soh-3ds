#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
extern "C" void mappedGainReference(int16_t*,unsigned,int16_t,unsigned,unsigned);
extern "C" void mappedInterleaveReference(int16_t*,unsigned,unsigned,unsigned,unsigned);
extern "C" void mappedEnvelopeReference(int16_t*,unsigned,unsigned,const uint16_t*,const uint16_t*,const uint16_t*);
extern "C" void mappedAddReference(int16_t*,unsigned,unsigned,unsigned);
extern "C" void mappedInterlReference(int16_t*,unsigned,unsigned,unsigned);
extern "C" void mappedZohReference(int16_t*,unsigned,unsigned,unsigned,uint16_t,uint16_t);
int main(){
    using namespace ResidentDsp;
    auto p=makeTypedFirmware(true,true,true,true);Teakra::Teakra dsp({});
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    for(unsigned i=0;i<0x6400;++i)dsp.DataWrite(i,0);dsp.Run(700);
    if(dsp.DataRead(0x10)!=0x4458 || !dsp.RecvDataIsReady(2))return 1;dsp.RecvData(2);
    std::vector<int16_t> memory(0x6400);uint32_t rng=0x28cd14;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    unsigned jobs=0,sequence=0;
    auto run=[&](Command command,const MappedParameters& params,bool expectedValid,unsigned originalCount,bool envelope){
        for(auto& x:memory)x=int16_t(next());
        unsigned slot=command.argument<ParameterSlots?command.argument:0;
        for(unsigned i=0;i<ParameterWords;++i)memory[ParameterWordBase+slot*ParameterWords+i]=params[i];
        for(unsigned i=0x400;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
        if(expectedValid){
            if(envelope){uint16_t bases[5];for(unsigned i=0;i<5;++i)bases[i]=AudioDmemWordBase+params[i];mappedEnvelopeReference(memory.data(),originalCount,params[5],params.data()+6,params.data()+9,bases);}
            else if(command.type==11)mappedInterleaveReference(memory.data(),originalCount,(AudioDmemWordBase+params[0])*2,(AudioDmemWordBase+params[1])*2,(AudioDmemWordBase+params[2])*2);
            else if(command.type==15)mappedAddReference(memory.data(),originalCount,(AudioDmemWordBase+params[0])*2,(AudioDmemWordBase+params[1])*2);
            else if(command.type==16)mappedInterlReference(memory.data(),originalCount,(AudioDmemWordBase+params[0])*2,(AudioDmemWordBase+params[1])*2);
            else if(command.type==17)mappedZohReference(memory.data(),originalCount,(AudioDmemWordBase+params[0])*2,(AudioDmemWordBase+params[1])*2,params[2],params[3]);
            else mappedGainReference(memory.data(),originalCount,int16_t(params[12]),(AudioDmemWordBase+params[0])*2,(AudioDmemWordBase+params[1])*2);
        }
        dsp.DataWrite(0x100,command.type);dsp.DataWrite(0x101,command.count);dsp.DataWrite(0x102,command.argument);dsp.DataWrite(0x103,command.flags);
        dsp.DataWrite(7,1);dsp.DataWrite(3,++sequence);dsp.DataWrite(0,1);dsp.Run(400000);
        if(dsp.DataRead(0) || dsp.DataRead(0x11)!=sequence || dsp.DataRead(0x12)!=(expectedValid?0:3) || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=sequence){std::printf("FAIL mapped arithmetic job%u type%u dispatch status%u\n",jobs,command.type,dsp.DataRead(0x12));return false;}
        for(unsigned i=0x400;i<memory.size();++i){
            if(envelope && expectedValid && i>=0x3b00 && i<0x3b1a)continue;
            if(command.type==11 && expectedValid && command.count && i>=0x3d00 && i<0x3d08)continue;
            if(dsp.DataRead(i)!=uint16_t(memory[i])){std::printf("FAIL mapped arithmetic job%u type%u count%u address%x got%d expected%d\n",jobs,command.type,command.count,i,int16_t(dsp.DataRead(i)),memory[i]);return false;}
        }
        ++jobs;return true;
    };
    for(unsigned trial=0;trial<168;++trial){
        const uint16_t bytes[]={0,1,7,8,9,320,352,384,768,1536,3072,65535};
        const uint16_t pitches[]={0,1,0x3fff,0x4000,0x8000,0xfffe,0xffff};
        unsigned original=bytes[trial%12],count=(original+7)&~7u;count=count?count/2:4;
        Command c={17,uint16_t(count),uint16_t(trial%64),0};MappedParameters r{};
        r[0]=(trial%4)*2;r[1]=(3-trial%4)*2;r[2]=pitches[(trial/12)%7];r[3]=trial&1?65535:0;
        unsigned last=(uint32_t(r[3])+uint32_t(count-1)*r[2]*4)>>17;
        bool valid=count<=1536 && r[0]+last<1536 && r[1]+count<=1536;
        Command lowered={1,2,3,4},saved=lowered;MappedParameters record;record.fill(0x1234);auto old=record;
        bool ok=lowerMappedZoh(trial%64,original,0x3c0+r[0]*2+(trial&1),0x3c0+r[1]*2+(trial&1),r[2],r[3],lowered,record);
        if(ok!=valid || (valid?(std::memcmp(&lowered,&c,sizeof(c)) || record!=r):(std::memcmp(&lowered,&saved,sizeof(saved)) || record!=old)))return 25;
        if(!run(c,r,valid,original,false))return 24;
    }
    for(unsigned bad=0;bad<7;++bad){
        Command c={17,4,63,0};MappedParameters r{};
        if(bad==0)c.count=0;else if(bad==1)c.count=3;else if(bad==2)c.count=65535;
        else if(bad==3)c.argument=64;else if(bad==4)c.flags=1;else if(bad==5)r[0]=1536;else r[1]=1533;
        if(!run(c,r,false,0,false))return 26;
    }
    for(unsigned edge=0;edge<2;++edge){
        Command c={17,4,63,0};MappedParameters r{};r[0]=1529+edge;r[1]=1532;r[2]=65535;r[3]=65535;
        if(!run(c,r,edge==0,8,false))return 27;
    }
    for(unsigned type:{15u,16u,17u}){
        Command c={uint16_t(type),uint16_t(type==16?768:1536),63,0};MappedParameters r{};
        if(type==17)r[0]=1535; // zero pitch repeatedly reads the final input sample
        if(!run(c,r,true,type==16?768:3072,false))return 28;
    }
    for(unsigned type:{15u,16u})for(unsigned trial=0;trial<96;++trial){
        const unsigned counts[]={0,1,7,8,15,16,31,32,63,64,160,192};
        unsigned original=counts[trial%12];
        unsigned samples=type==15?(((original&~15u)+63)&~63u)/2:(original+7)&~7u;
        if(!samples)samples=type==15?16:8;
        Command c={uint16_t(type),uint16_t(samples),uint16_t(trial%64),0};MappedParameters r{};
        r[0]=trial%4==0?0:trial%4==1?4:trial%4==2?32:8;
        r[1]=trial%4==0?4:trial%4==1?0:trial%4==2?32:7;
        Command lowered;MappedParameters record;
        bool ok=type==15?lowerMappedAdd(trial%64,original,0x3c0+r[0]*2+(trial&1),0x3c0+r[1]*2+(trial&1),lowered,record):
            lowerMappedInterl(trial%64,original,0x3c0+r[0]*2+(trial&1),0x3c0+r[1]*2+(trial&1),lowered,record);
        if(!ok || std::memcmp(&lowered,&c,sizeof(c)) || record!=r)return 20;
        if(!run(c,r,true,original,false))return 19;
    }
    for(unsigned type:{15u,16u}){
        for(unsigned bad=0;bad<9;++bad){
            Command c={uint16_t(type),16,63,0};MappedParameters r{};
            if(bad==0)c.count=0;else if(bad==1)c.count=7;else if(bad==2)c.count=65535;
            else if(bad==3)c.argument=64;else if(bad==4)c.flags=1;
            else if(bad==5)r[0]=1536;else if(bad==6)r[1]=1536;
            else if(bad==7)r[0]=65535;else r[1]=65535;
            if(!run(c,r,false,0,false))return 21;
        }
        for(unsigned edge=0;edge<2;++edge){
            Command c={uint16_t(type),16,63,0};MappedParameters r{};
            r[0]=1536-(type==15?16:32)+edge;r[1]=1520;
            if(!run(c,r,edge==0,type==15?0:16,false))return 22;
        }
        Command saved={1,2,3,4},c=saved;MappedParameters r;r.fill(0x1234);auto old=r;
        for(unsigned bad=0;bad<4;++bad){
            unsigned slot=bad==0?64:0,count=bad==1?65535:32;
            uint16_t input=bad==2?0x3bf:0x3c0,output=bad==3?0xfc0:0x3c0;
            bool ok=type==15?lowerMappedAdd(slot,count,input,output,c,r):lowerMappedInterl(slot,count,input,output,c,r);
            if(ok || std::memcmp(&c,&saved,sizeof(c)) || r!=old)return 23;
        }
    }
    for(unsigned trial=0;trial<192;++trial){
        const uint16_t counts[]={0,1,2,3,20,22,24,32,64,96,128,191,192};unsigned count=counts[trial%13];
        const int16_t gains[]={-32768,-32767,-1,0,1,16384,32767};int16_t gain=gains[trial%7];
        unsigned source=count>96?0:trial%4==0?0:trial%4==1?2:trial%4==2?32:64;
        unsigned dest=count>96?0:trial%4==0?2:trial%4==1?0:trial%4==2?32:62;
        Command command;MappedParameters params;
        if(!lowerMappedGain(trial%64,count,gain,0x3c0+source+(trial&1),0x3c0+dest+(trial&1),command,params))return 2;
        if(!run(command,params,true,count,false))return 3;
    }
    for(unsigned trial=0;trial<256;++trial){
        const uint16_t counts[]={0,8,16,24,160,176,192,512};unsigned count=counts[(trial/32)%8];
        std::array<uint16_t,5> addresses;
        for(unsigned i=0;i<5;++i)addresses[i]=0x3c0+((trial/32)%4==0?0:(trial/32)%4==1?i*2:(trial/32)%4==2?(i%2)*32:i*256);
        std::array<uint16_t,3> volumes={next(),next(),next()},rates={next(),next(),next()};
        Command command;MappedParameters params;
        if(!lowerMappedEnvelope(trial%64,count,addresses,trial%32,volumes,rates,command,params))return 4;
        if(!run(command,params,true,count,true))return 5;
    }
    for(unsigned trial=0;trial<128;++trial){
        const uint16_t counts[]={0,1,7,8,9,16,320,352,384,512,768,1024,1536};unsigned bytes=counts[trial%13];
        unsigned offset=bytes>512?0:trial%4*2;
        Command command;MappedParameters params;
        if(!lowerMappedInterleave(trial%64,bytes,0x3c0+offset,0x3c0+offset+2*(trial%2),0x3c0+(bytes>512?0:6-offset),command,params)){
            // Max output extent requires all source offsets to be zero too.
            if(!lowerMappedInterleave(trial%64,bytes,0x3c0,0x3c0,0x3c0,command,params))return 10;
        }
        if(!run(command,params,true,bytes,false))return 11;
    }
    for(unsigned field=0;field<3;++field){
        Command command={11,4,63,0};MappedParameters params{};params[field]=1533;
        if(field!=2)params[field]=1536;
        if(!run(command,params,false,0,false))return 12;
    }
    // Firmware independently rejects corrupted record extents and descriptors.
    for(bool envelope:{false,true})for(unsigned bad=0;bad<14;++bad){
        Command command={uint16_t(envelope?10:9),16,63,0};MappedParameters params{};params[12]=0x4000;
        if(bad<5){params[bad]=0xffff;if(!envelope && bad>=2)params[1]=0xffff;}
        else if(bad<10){params[bad-5]=1536;if(!envelope && bad>=7)params[0]=1536;}
        else if(bad==10)command.argument=64;
        else if(bad==11)command.count=65535;
        else if(bad==12)command.flags=1;
        else if(envelope)params[5]=32;else command.count=8;
        if(!run(command,params,false,0,envelope))return 6;
    }
    // Lowering failure must leave caller-owned outputs untouched.
    Command command={1,2,3,4},saved=command;MappedParameters params;params.fill(0x1234);auto old=params;
    for(auto count:{uint16_t(193),uint16_t(65535)})if(lowerMappedGain(0,count,1,0x3c0,0x3c0,command,params) || std::memcmp(&command,&saved,sizeof(command)) || params!=old)return 7;
    if(lowerMappedEnvelope(64,0,{0x3c0,0x3c0,0x3c0,0x3c0,0x3c0},0,{0,0,0},{0,0,0},command,params) || params!=old)return 8;
    for(auto destination:{uint16_t(0x3bf),uint16_t(0xfc0),uint16_t(0xffff)}){
        if(lowerMappedInterleave(0,8,0x3c0,0x3c0,destination,command,params) || std::memcmp(&command,&saved,sizeof(command)) || params!=old)return 17;
    }
    uint16_t offset=0xffff;
    if(!lowerSampleAddress(0x3c1,1,offset) || offset!=0 || !lowerSampleAddress(0x3c3,1,offset) || offset!=1 ||
       !lowerSampleAddress(0xfbf,1,offset) || offset!=1535)return 18;
    // A whole resident gain/envelope/interleave chain: no ARM-side copies
    // between stages, and the final stereo PCM remains ready for one readback.
    for(auto& x:memory)x=int16_t(next());
    for(unsigned i=0x400;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
    for(unsigned trial=0;trial<64;++trial){
        std::array<Command,6> commands;std::array<MappedParameters,6> records;
        const uint16_t count=160;
        if(!lowerMappedGain(0,20,int16_t(next()),0x3c0,0x3c0+320,commands[0],records[0]))return 13;
        std::array<uint16_t,5> addresses={0x3c0+320,0x3c0+640,0x3c0+1024,0x3c0+1408,0x3c0+1792};
        std::array<uint16_t,3> volumes={next(),next(),next()},rates={next(),next(),next()};
        if(!lowerMappedEnvelope(1,count,addresses,trial%32,volumes,rates,commands[1],records[1]) ||
           !lowerMappedInterleave(2,count*2,addresses[1],addresses[2],0x3c0+2240,commands[2],records[2]))return 14;
        if(!lowerMappedZoh(3,320,0x3c0,0x3c0,0x8000,trial*1023,commands[3],records[3]) ||
           !lowerMappedAdd(4,320,0x3c0,0x3c0+320,commands[4],records[4]) ||
           !lowerMappedInterl(5,160,0x3c0,0x3c0+640,commands[5],records[5]))return 29;
        for(unsigned slot=0;slot<6;++slot){
            for(unsigned i=0;i<16;++i){memory[ParameterWordBase+slot*16+i]=records[slot][i];dsp.DataWrite(ParameterWordBase+slot*16+i,records[slot][i]);}
            auto c=commands[slot];dsp.DataWrite(0x100+slot*4,c.type);dsp.DataWrite(0x101+slot*4,c.count);dsp.DataWrite(0x102+slot*4,c.argument);dsp.DataWrite(0x103+slot*4,c.flags);
        }
        mappedGainReference(memory.data(),20,int16_t(records[0][12]),AudioDmemWordBase*2,(AudioDmemWordBase+160)*2);
        uint16_t bases[5];for(unsigned i=0;i<5;++i)bases[i]=AudioDmemWordBase+records[1][i];
        mappedEnvelopeReference(memory.data(),count,trial%32,volumes.data(),rates.data(),bases);
        mappedInterleaveReference(memory.data(),count*2,(AudioDmemWordBase+320)*2,(AudioDmemWordBase+512)*2,(AudioDmemWordBase+1120)*2);
        mappedZohReference(memory.data(),320,AudioDmemWordBase*2,AudioDmemWordBase*2,0x8000,trial*1023);
        mappedAddReference(memory.data(),320,AudioDmemWordBase*2,(AudioDmemWordBase+160)*2);
        mappedInterlReference(memory.data(),160,AudioDmemWordBase*2,(AudioDmemWordBase+320)*2);
        dsp.DataWrite(7,6);dsp.DataWrite(3,++sequence);dsp.DataWrite(0,1);dsp.Run(250000);
        if(dsp.DataRead(0) || dsp.DataRead(0x11)!=sequence || dsp.DataRead(0x12) || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=sequence)return 15;
        for(unsigned i=0x400;i<memory.size();++i){
            if((i>=0x3b00 && i<0x3b1a) || (i>=0x3d00 && i<0x3d08))continue;
            if(dsp.DataRead(i)!=uint16_t(memory[i])){std::printf("FAIL mapped chain%u addr%x\n",trial,i);return 16;}
        }
    }
    dsp.SendData(2,0x8000);dsp.Run(700);if(!dsp.RecvDataIsReady(2) || dsp.RecvData(2))return 9;
    std::printf("PASS: %u addressable DSP gain/envelope/interleave/add/interl/ZOH jobs, production PCM, all slots/flags, aliased buffers, normalized counts and rejected extents, plus64 resident six-command mixed chains; %zu firmware words\n",jobs,p.words.size());
}
