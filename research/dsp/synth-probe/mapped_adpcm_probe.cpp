#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
extern "C" void adpcmReferenceAt(int16_t*,unsigned,unsigned,uint16_t,uint16_t,int16_t*,int16_t*,const int16_t*);
int main(){
    using namespace ResidentDsp;
    auto p=makeTypedFirmware(true,true,true,true);Teakra::Teakra dsp({});
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    for(unsigned i=0;i<0x7100;++i)dsp.DataWrite(i,0);dsp.Run(700);if(dsp.DataRead(0x10)!=0x4458 || !dsp.RecvDataIsReady(2))return 1;dsp.RecvData(2);
    std::vector<int16_t> memory(0x7100);std::array<std::array<int16_t,16>,64> states{};
    uint32_t rng=0x5973ac;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    unsigned jobs=0,sequence=0;
    auto run=[&](Command command,MappedParameters params,bool valid,bool badHeader=false){
        for(auto& x:memory)x=int16_t(next());
        for(unsigned slot=0;slot<64;++slot)for(unsigned i=0;i<16;++i)memory[AdpcmStateWordBase+slot*16+i]=states[slot][i];
        for(unsigned i=0;i<16;++i)memory[0x2800+i]=uint16_t(1u<<i);
        unsigned slot=command.argument<64?command.argument:0;
        for(unsigned i=0;i<16;++i)memory[ParameterWordBase+slot*16+i]=params[i];
        unsigned blocks=command.count/16,stride=command.type==14?5:9;
        unsigned sourceBase=params[6]==1?PayloadWordBase:AudioDmemWordBase;
        unsigned sourceBytes=params[6]==1?PayloadBytes:AudioDmemBytes;
        if(params[0]+blocks*stride<=sourceBytes){
            auto* input=reinterpret_cast<uint8_t*>(memory.data()+sourceBase)+params[0];
            for(unsigned i=0;i<blocks;++i)input[i*stride]=uint8_t(((jobs+i)%16)*16+(jobs+i)%8);
            if(badHeader && blocks)input[(blocks-1)*stride]|=8;
        }
        for(unsigned i=0x400;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
        if(valid)adpcmReferenceAt(memory.data(),command.count*2,params[5]|(command.type==14?4:0),sourceBase*2+params[0],(AudioDmemWordBase+params[1])*2,memory.data()+AdpcmStateWordBase+params[2]*16,memory.data()+AdpcmLoopWordBase+params[3]*16,memory.data()+AdpcmBookWordBase+params[4]*128);
        dsp.DataWrite(0x100,command.type);dsp.DataWrite(0x101,command.count);dsp.DataWrite(0x102,command.argument);dsp.DataWrite(0x103,command.flags);
        dsp.DataWrite(7,1);dsp.DataWrite(3,++sequence);dsp.DataWrite(0,1);dsp.Run(500000);
        if(dsp.DataRead(0) || dsp.DataRead(0x12)!=(valid?0:3) || dsp.DataRead(0x11)!=sequence || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=sequence){printf("FAIL mapped ADPCM job%u type%u count%u status%u expected%u\n",jobs,command.type,command.count,dsp.DataRead(0x12),valid?0:3);return false;}
        for(unsigned i=0x400;i<memory.size();++i){
            if(valid && blocks && ((i>=0x1000 && i<0x1008) || (i>=0x2500 && i<0x2510)))continue;
            if(dsp.DataRead(i)!=uint16_t(memory[i])){printf("FAIL mapped ADPCM job%u type%u count%u in%u out%u state%u flags%u addr%x got%d expected%d\n",jobs,command.type,command.count,params[0],params[1],params[2],params[5],i,int16_t(dsp.DataRead(i)),memory[i]);return false;}
        }
        if(valid)for(unsigned i=0;i<16;++i)states[params[2]][i]=memory[AdpcmStateWordBase+params[2]*16+i];
        ++jobs;return true;
    };
    for(bool twoBit:{false,true})for(unsigned trial=0;trial<128;++trial){
        const uint16_t counts[]={0,16,32,160,176,192,512,768,1184};unsigned count=counts[trial%9];
        if(twoBit && count==1184)count=1312;
        unsigned output=count>768?0:trial%8,source=count>768?(count+16)*2:2400+(trial&1);
        Command command;MappedParameters params;
        if(!lowerMappedAdpcm(trial%64,twoBit,count*2,0x3c0+source,0x3c0+output*2+(trial&1),trial<64?1:trial%3,(trial*7)%64,(trial*19)%64,trial%16,command,params))return 2;
        if(!run(command,params,true))return 3;
    }
    for(bool twoBit:{false,true})for(unsigned flags:{0u,1u,2u})for(unsigned samples:{0u,16u,512u}){
        Command command;MappedParameters params;
        if(!lowerMappedAdpcm(63,twoBit,samples*2,0x3c0+2401,0x3c0+2,flags,0,63,15,command,params) || !run(command,params,true))return 4;
    }
    for(bool twoBit:{false,true})for(unsigned bad=0;bad<14;++bad){
        Command command={uint16_t(twoBit?14:13),32,63,0};MappedParameters params{};params[0]=2401;params[2]=63;params[3]=0;params[4]=15;params[5]=1;
        switch(bad){case 0:params[0]=3072;break;case 1:params[1]=1520;break;case 2:params[2]=64;break;case 3:params[3]=64;break;case 4:params[4]=16;break;case 5:params[5]=3;break;case 6:command.argument=64;break;case 7:command.count=8;break;case 8:command.count=65535;break;case 9:command.flags=1;break;case 10:params[0]=0;break;case 11:params[0]=31;break;case 12:params[1]=1200;break;case 13:break;}
        if(!run(command,params,false,bad==13))return 5;
    }
    // Empty compressed extent does not alias the mandatory history write.
    for(bool twoBit:{false,true}){
        Command command;MappedParameters params;
        if(!lowerMappedAdpcm(0,twoBit,0,0x3c0,0x3c0,2,63,0,0,command,params) || !run(command,params,true))return 6;
    }
    for(bool twoBit:{false,true})for(unsigned bytes:{1u,31u,33u}){
        unsigned samples=((bytes+31)&~31u)/2,packed=samples/16*(twoBit?5:9);
        for(bool sourceFirst:{false,true}){
            // Choose an even compressed end so both regions can abut exactly.
            unsigned source=sourceFirst?(packed&1):((samples+16)*2);
            unsigned output=sourceFirst?(source+packed)/2:0;
            Command command;MappedParameters params;
            if(!lowerMappedAdpcm(0,twoBit,bytes,0x3c0+source,0x3c0+output*2,1,63,0,15,command,params) || command.count!=samples || !run(command,params,true))return 9;
        }
    }
    for(bool twoBit:{false,true})for(unsigned samples:{0u,16u,512u,1520u}){
        unsigned packed=samples/16*(twoBit?5:9);
        for(unsigned offset:{0u,1u,PayloadBytes-packed}){
            Command command;MappedParameters params;
            if(!lowerStagedAdpcm(63,twoBit,samples*2,offset,0x3c0,1,0,63,15,command,params) || !run(command,params,true))return 13;
        }
    }
    for(unsigned region:{1u,2u}){
        Command command={13,16,0,0};MappedParameters params{};params[0]=region==1?PayloadBytes:0;params[5]=1;params[6]=region;
        if(!run(command,params,false))return 14;
    }
    Command command={1,2,3,4};auto saved=command;MappedParameters params;params.fill(0x1234);auto old=params;
    if(lowerMappedAdpcm(0,false,32,0x3c0,0x3c0,1,0,0,0,command,params) || std::memcmp(&command,&saved,sizeof(saved)) || params!=old)return 7;
    for(unsigned batch=0;batch<16;++batch){
        std::array<Command,2> commands;std::array<MappedParameters,2> records;
        for(unsigned i=0;i<2;++i){
            unsigned slot=i?63:0,source=i?2501:2400,state=batch%2?0:(i?63:0),flags=batch==0?1:batch%3;
            if(!lowerMappedAdpcm(slot,i,64,0x3c0+source,0x3c0+i*128,flags,state,i?0:63,i?15:0,commands[i],records[i]))return 10;
            auto* input=reinterpret_cast<uint8_t*>(memory.data()+AudioDmemWordBase)+source;
            input[0]=0x20;input[i?5:9]=0x31;
            for(unsigned n=0;n<16;++n)memory[ParameterWordBase+slot*16+n]=records[i][n];
            auto c=commands[i];dsp.DataWrite(0x100+i*4,c.type);dsp.DataWrite(0x101+i*4,c.count);dsp.DataWrite(0x102+i*4,c.argument);dsp.DataWrite(0x103+i*4,c.flags);
        }
        for(unsigned i=0x400;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
        for(unsigned i=0;i<2;++i){auto& r=records[i];adpcmReferenceAt(memory.data(),64,r[5]|(i?4:0),AudioDmemWordBase*2+r[0],(AudioDmemWordBase+r[1])*2,memory.data()+AdpcmStateWordBase+r[2]*16,memory.data()+AdpcmLoopWordBase+r[3]*16,memory.data()+AdpcmBookWordBase+r[4]*128);}
        dsp.DataWrite(7,2);dsp.DataWrite(3,++sequence);dsp.DataWrite(0,1);dsp.Run(100000);
        if(dsp.DataRead(0) || dsp.DataRead(0x12) || dsp.DataRead(0x11)!=sequence || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=sequence)return 11;
        for(unsigned i=0x400;i<memory.size();++i){
            if((i>=0x1000 && i<0x1008) || (i>=0x2500 && i<0x2510))continue;
            if(dsp.DataRead(i)!=uint16_t(memory[i])){printf("FAIL mapped ADPCM batch%u addr%x\n",batch,i);return 12;}
        }
    }
    dsp.SendData(2,0x8000);dsp.Run(700);if(!dsp.RecvDataIsReady(2) || dsp.RecvData(2))return 8;
    printf("PASS: %u mapped DSP ADPCM jobs, both modes, persistent/loop/book slots, odd bytes, large blocks, immutable payload bank, preflight header/extent/alias rejection and memory guards, plus16 mixed-codec batches; %zu firmware words\n",jobs,p.words.size());
}
