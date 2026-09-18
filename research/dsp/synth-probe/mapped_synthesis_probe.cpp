#include <array>
#include <cstdio>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
extern "C" void adpcmReferenceAt(int16_t*,unsigned,unsigned,uint16_t,uint16_t,int16_t*,int16_t*,const int16_t*);
extern "C" void resampleReference(int16_t*,uint16_t,uint16_t,uint16_t,uint8_t,uint16_t,int16_t*);
extern "C" const int16_t* productionTable();
extern "C" void mappedEnvelopeReference(int16_t*,unsigned,unsigned,const uint16_t*,const uint16_t*,const uint16_t*);
extern "C" void mappedInterleaveReference(int16_t*,unsigned,unsigned,unsigned,unsigned);
int main(){
    using namespace ResidentDsp;
    auto p=makeTypedFirmware(true,true,true,true);Teakra::Teakra dsp({});
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    for(unsigned i=0;i<0x7100;++i)dsp.DataWrite(i,0);dsp.Run(700);if(dsp.DataRead(0x10)!=0x4458 || !dsp.RecvDataIsReady(2))return 1;dsp.RecvData(2);
    uint32_t rng=0x3817ae;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    std::vector<int16_t> memory(0x7100);for(auto& x:memory)x=int16_t(next());
    for(unsigned i=0;i<256;++i)memory[0x4000+i]=productionTable()[i];
    for(unsigned i=0;i<16;++i)memory[0x2800+i]=uint16_t(1u<<i);
    for(unsigned i=0;i<16*128;++i)memory[AdpcmBookWordBase+i]=int(next()%8192)-4096;
    for(unsigned i=0x400;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
    for(unsigned trial=0;trial<64;++trial){
        bool twoBit=trial&1;unsigned flags=trial<8?1:trial%11==0?2:0;
        unsigned stateSlot=trial%8,resampleSlot=(trial*3)%8,bookSlot=(trial/4)%16,loopSlot=(stateSlot+1)%64;
        const unsigned decoded=352,count=160,source=2752;
        std::array<Command,4> commands;std::array<MappedParameters,4> records;
        std::array<uint16_t,5> addresses={0x3c0+384*2,0x3c0+576*2,0x3c0+768*2,0x3c0+960*2,0x3c0+1152*2};
        std::array<uint16_t,3> volumes={next(),next(),next()},rates={next(),next(),next()};
        const uint16_t pitches[]={0x3fff,0x8000,0xffff};unsigned pitch=pitches[(trial/8)%3];
        if(!lowerMappedAdpcm(0,twoBit,decoded*2,0x3c0+source,0x3c0,flags,stateSlot,loopSlot,bookSlot,commands[0],records[0]) ||
           !lowerMappedResample(1,count*2,0x3c0+16*2,addresses[0],pitch,flags,resampleSlot,commands[1],records[1]) ||
           !lowerMappedEnvelope(2,count,addresses,trial%32,volumes,rates,commands[2],records[2]) ||
           !lowerMappedInterleave(3,count*2,addresses[1],addresses[2],0x3c0,commands[3],records[3]))return 2;
        auto* input=reinterpret_cast<uint8_t*>(memory.data()+AudioDmemWordBase)+source;
        unsigned stride=twoBit?5:9,packed=decoded/16*stride;
        for(unsigned i=0;i<packed;++i)input[i]=uint8_t(next());
        for(unsigned block=0;block<decoded/16;++block)input[block*stride]=uint8_t(((trial+block)%16)*16+(trial+block)%8);
        for(unsigned i=0;i<(packed+1)/2;++i)dsp.DataWrite(AudioDmemWordBase+source/2+i,uint16_t(memory[AudioDmemWordBase+source/2+i]));
        for(unsigned slot=0;slot<4;++slot){
            for(unsigned i=0;i<16;++i){memory[ParameterWordBase+slot*16+i]=records[slot][i];dsp.DataWrite(ParameterWordBase+slot*16+i,records[slot][i]);}
            auto c=commands[slot];dsp.DataWrite(0x100+slot*4,c.type);dsp.DataWrite(0x101+slot*4,c.count);dsp.DataWrite(0x102+slot*4,c.argument);dsp.DataWrite(0x103+slot*4,c.flags);
        }
        adpcmReferenceAt(memory.data(),decoded*2,flags|(twoBit?4:0),AudioDmemWordBase*2+source,AudioDmemWordBase*2,memory.data()+AdpcmStateWordBase+stateSlot*16,memory.data()+AdpcmLoopWordBase+loopSlot*16,memory.data()+AdpcmBookWordBase+bookSlot*128);
        resampleReference(memory.data(),(AudioDmemWordBase+16)*2,(AudioDmemWordBase+384)*2,count*2,flags,pitch,memory.data()+ResampleStateWordBase+resampleSlot*16);
        uint16_t bases[5];for(unsigned i=0;i<5;++i)bases[i]=AudioDmemWordBase+records[2][i];
        mappedEnvelopeReference(memory.data(),count,trial%32,volumes.data(),rates.data(),bases);
        mappedInterleaveReference(memory.data(),count*2,(AudioDmemWordBase+576)*2,(AudioDmemWordBase+768)*2,AudioDmemWordBase*2);
        dsp.DataWrite(7,4);dsp.DataWrite(3,trial+1);dsp.DataWrite(0,1);dsp.Run(500000);
        if(dsp.DataRead(0) || dsp.DataRead(0x12) || dsp.DataRead(0x11)!=trial+1 || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=trial+1){printf("FAIL mapped synthesis frame%u status%u\n",trial,dsp.DataRead(0x12));return 3;}
        for(unsigned i=0x400;i<memory.size();++i){
            if((i>=0x1000 && i<0x1008) || (i>=0x2500 && i<0x2510) || (i>=0x3b00 && i<0x3b1a) || (i>=0x3d00 && i<0x3d08))continue;
            if(dsp.DataRead(i)!=uint16_t(memory[i])){printf("FAIL mapped synthesis frame%u address%x got%d expected%d\n",trial,i,int16_t(dsp.DataRead(i)),memory[i]);return 4;}
        }
    }
    dsp.SendData(2,0x8000);dsp.Run(700);if(!dsp.RecvDataIsReady(2) || dsp.RecvData(2))return 5;
    printf("PASS: 64 resident decode-resample-envelope-interleave chains, both ADPCM modes,8 persistent voices, per-command records, all memory/state/PCM guards; %zu firmware words\n",p.words.size());
}
