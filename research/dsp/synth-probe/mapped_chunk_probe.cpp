#include <array>
#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
extern "C" void adpcmReferenceAt(int16_t*,unsigned,unsigned,uint16_t,uint16_t,int16_t*,int16_t*,const int16_t*);
extern "C" void resampleReference(int16_t*,uint16_t,uint16_t,uint16_t,uint8_t,uint16_t,int16_t*);
extern "C" const int16_t* productionTable();
extern "C" void mappedEnvelopeReference(int16_t*,unsigned,unsigned,const uint16_t*,const uint16_t*,const uint16_t*);
extern "C" void mappedInterleaveReference(int16_t*,unsigned,unsigned,unsigned,unsigned);
struct Step {ResidentDsp::Command command;ResidentDsp::MappedParameters parameters{};};
int main(){
    using namespace ResidentDsp;
    auto program=makeTypedFirmware(true,true,true,true);Teakra::Teakra dsp({});
    for(unsigned i=0;i<program.words.size();++i)dsp.ProgramWrite(i,program.words[i]);
    for(unsigned i=0;i<0x7100;++i)dsp.DataWrite(i,0);dsp.Run(700);
    if(dsp.DataRead(0x10)!=0x4458 || !dsp.RecvDataIsReady(2))return 1;dsp.RecvData(2);
    uint32_t rng=0x894da5;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    std::vector<int16_t> memory(0x7100);for(auto& x:memory)x=int16_t(next());
    for(unsigned i=0;i<256;++i)memory[0x4000+i]=productionTable()[i];
    for(unsigned i=0;i<16;++i)memory[0x2800+i]=uint16_t(1u<<i);
    for(unsigned i=0;i<16*128;++i)memory[AdpcmBookWordBase+i]=int(next()%8192)-4096;
    for(unsigned i=0x400;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
    unsigned sequence=0,totalVoices=0,totalBatches=0;
    for(unsigned chunk=0;chunk<6;++chunk){
        unsigned voices=chunk%2?24:28,count=160+(chunk%3)*16,used=0;
        std::vector<Step> steps;
        for(unsigned base:{576u,768u,960u,1152u})steps.push_back({{8,uint16_t(count*2),0,uint16_t(base*2)}, {}});
        for(unsigned voice=0;voice<voices;++voice){
            bool twoBit=voice&1;unsigned flags=chunk==0?1:(chunk+voice)%7==0?2:0,decoded=416;
            unsigned stride=twoBit?5:9,packed=decoded/16*stride;
            if(used+packed>PayloadBytes)return 2;
            auto* input=reinterpret_cast<uint8_t*>(memory.data()+PayloadWordBase)+used;
            for(unsigned i=0;i<packed;++i)input[i]=uint8_t(next());
            for(unsigned block=0;block<decoded/16;++block)input[block*stride]=uint8_t(((voice+block+chunk)%16)*16+(voice+block)%8);
            Step decode,resample,envelope;
            std::array<uint16_t,5> addresses={0x3c0+384*2,0x3c0+576*2,0x3c0+768*2,0x3c0+960*2,0x3c0+1152*2};
            std::array<uint16_t,3> volumes={next(),next(),next()},rates={next(),next(),next()};
            const uint16_t pitches[]={0x4000,0x8000,0xffff};
            if(!lowerStagedAdpcm(0,twoBit,decoded*2,used,0x3c0,flags,voice,(voice+1)%64,voice%16,decode.command,decode.parameters) ||
               !lowerMappedResample(0,count*2,0x3c0+16*2,addresses[0],pitches[(chunk+voice)%3],flags,(voice*3)%64,resample.command,resample.parameters) ||
               !lowerMappedEnvelope(0,count,addresses,(chunk+voice)%32,volumes,rates,envelope.command,envelope.parameters))return 3;
            steps.push_back(decode);steps.push_back(resample);steps.push_back(envelope);used+=packed+1;
        }
        Step interleave;
        if(!lowerMappedInterleave(0,count*2,0x3c0+576*2,0x3c0+768*2,0x3c0,interleave.command,interleave.parameters))return 4;
        steps.push_back(interleave);
        // All compressed samples are staged once, before any voice runs.
        for(unsigned i=0;i<PayloadBytes/2;++i)dsp.DataWrite(PayloadWordBase+i,uint16_t(memory[PayloadWordBase+i]));
        for(unsigned start=0;start<steps.size();start+=32){
            unsigned length=unsigned(steps.size()-start);if(length>32)length=32;
            unsigned parameters=0;
            for(unsigned i=0;i<length;++i){
                auto c=steps[start+i].command;const auto& r=steps[start+i].parameters;
                if(c.type>=9){
                    c.argument=parameters++;
                    for(unsigned n=0;n<16;++n){memory[ParameterWordBase+c.argument*16+n]=r[n];dsp.DataWrite(ParameterWordBase+c.argument*16+n,r[n]);}
                }
                dsp.DataWrite(0x100+i*4,c.type);dsp.DataWrite(0x101+i*4,c.count);dsp.DataWrite(0x102+i*4,c.argument);dsp.DataWrite(0x103+i*4,c.flags);
                switch(c.type){
                    case 8:std::memset(reinterpret_cast<uint8_t*>(memory.data()+AudioDmemWordBase)+c.flags,0,c.count);break;
                    case 13:case 14:adpcmReferenceAt(memory.data(),c.count*2,r[5]|(c.type==14?4:0),PayloadWordBase*2+r[0],(AudioDmemWordBase+r[1])*2,memory.data()+AdpcmStateWordBase+r[2]*16,memory.data()+AdpcmLoopWordBase+r[3]*16,memory.data()+AdpcmBookWordBase+r[4]*128);break;
                    case 12:resampleReference(memory.data(),(AudioDmemWordBase+r[0])*2,(AudioDmemWordBase+r[1])*2,c.count*2,r[4],r[3],memory.data()+ResampleStateWordBase+r[2]*16);break;
                    case 10:{uint16_t bases[5];for(unsigned n=0;n<5;++n)bases[n]=AudioDmemWordBase+r[n];mappedEnvelopeReference(memory.data(),c.count,r[5],r.data()+6,r.data()+9,bases);break;}
                    case 11:mappedInterleaveReference(memory.data(),c.count*2,(AudioDmemWordBase+r[0])*2,(AudioDmemWordBase+r[1])*2,(AudioDmemWordBase+r[2])*2);break;
                    default:return 5;
                }
            }
            dsp.DataWrite(7,length);dsp.DataWrite(3,++sequence);dsp.DataWrite(0,1);dsp.Run(800000);
            if(dsp.DataRead(0) || dsp.DataRead(0x12) || dsp.DataRead(0x11)!=sequence || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=sequence){printf("FAIL chunk%u batch%u status%u\n",chunk,start/32,dsp.DataRead(0x12));return 6;}
            for(unsigned i=0x400;i<memory.size();++i){
                if((i>=0x1000 && i<0x1008) || (i>=0x2500 && i<0x2510) || (i>=0x3b00 && i<0x3b1a) || (i>=0x3d00 && i<0x3d08))continue;
                if(dsp.DataRead(i)!=uint16_t(memory[i])){printf("FAIL chunk%u batch%u addr%x got%d expected%d\n",chunk,start/32,i,int16_t(dsp.DataRead(i)),memory[i]);return 7;}
            }
            ++totalBatches;
        }
        totalVoices+=voices;
    }
    dsp.SendData(2,0x8000);dsp.Run(700);if(!dsp.RecvDataIsReady(2) || dsp.RecvData(2))return 8;
    printf("PASS: 6 complete 24/28-voice chunks, %u voices in%u bounded batches,160/176/192 stereo samples, one payload staging per chunk, persistent state and full PCM/memory guards; %zu firmware words\n",totalVoices,totalBatches,program.words.size());
}
