#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
extern "C" void adpcmReference(int16_t*,unsigned,unsigned,int16_t*,int16_t*,const int16_t*);
extern "C" void resampleReference(int16_t*,uint16_t,uint16_t,uint16_t,uint8_t,uint16_t,int16_t*);
extern "C" const int16_t* productionTable();
int main(int argc,char** argv) {
    std::ofstream fixture;
    if(argc>1){fixture.exceptions(std::ios::failbit|std::ios::badbit);fixture.open(std::string(argv[1])+"/pipeline_fixture.h");}
    Teakra::Teakra dsp({});auto p=makeTypedFirmware();for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    for(unsigned i=0;i<0x4100;++i)dsp.DataWrite(i,0);dsp.Run(500);dsp.RecvData(2);
    std::vector<int16_t> memory(0x4000);int16_t adpcm[16]={},resample[16]={},loop[16]={},book[128];
    uint32_t rng=0x372ac;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    for(unsigned i=0;i<128;++i){book[i]=int16_t(next()%8192)-4096;dsp.DataWrite(0x2000+i,uint16_t(book[i]));}
    for(unsigned i=0;i<256;++i)dsp.DataWrite(0x4000+i,uint16_t(productionTable()[i]));
    for(unsigned i=0;i<16;++i)dsp.DataWrite(0x2800+i,uint16_t(1u<<i));
    if(fixture.is_open()){
        fixture << "#pragma once\n#include <cstdint>\nstatic const uint16_t pipelineBook[128]={";
        for(auto x:book)fixture<<uint16_t(x)<<',';
        fixture << "};\nstatic const uint16_t pipelineTable[256]={";
        for(unsigned i=0;i<256;++i)fixture<<uint16_t(productionTable()[i])<<',';
        fixture << "};\nstruct PipelineFrame {uint16_t packed[5],pcm[16],adpcm[16],resample[16];};\nstatic const PipelineFrame pipelineFrames[200]={\n";
    }
    for(unsigned frame=0;frame<200;++frame) {
        for(unsigned i=0;i<5;++i){memory[0x800+i]=next();dsp.DataWrite(0x800+i,uint16_t(memory[0x800+i]));}
        auto bytes=reinterpret_cast<uint8_t*>(memory.data())+0x1000;bytes[0]=((frame%12)<<4)|(frame%8);dsp.DataWrite(0x800,uint16_t(memory[0x800]));
        unsigned flags=frame==0?1:0;
        adpcmReference(memory.data(),32,flags,adpcm,loop,book);
        memcpy(memory.data()+0x400,memory.data()+0x3010,32);
        resampleReference(memory.data(),0x800,0x2000,32,flags,0x8000,resample);
        for(unsigned i=0;i<16;++i){int v=(int32_t(memory[0x1400+i])*32767+int32_t(memory[0x1000+i])*16384+16384)>>15;memory[0x1400+i]=std::clamp(v,-32768,32767);}
        const uint16_t queue[]={2,16,0,uint16_t(flags),4,16,0,0,1,16,0x8000,uint16_t(flags),0,16,0x4000,0};
        for(unsigned i=0;i<16;++i)dsp.DataWrite(0x100+i,queue[i]);
        dsp.DataWrite(3,frame+1);dsp.DataWrite(5,65535);dsp.DataWrite(6,0);dsp.DataWrite(7,4);dsp.DataWrite(0,1);dsp.Run(50000);
        if(dsp.DataRead(0) || dsp.DataRead(0x11)!=frame+1 || dsp.DataRead(0x12)){printf("FAIL queue frame%u status%u\n",frame,dsp.DataRead(0x12));return 1;}
        for(unsigned i=0;i<16;++i)if(int16_t(dsp.DataRead(0x1400+i))!=memory[0x1400+i] || int16_t(dsp.DataRead(0x3800+i))!=adpcm[i] || int16_t(dsp.DataRead(0x3a00+i))!=resample[i]){printf("FAIL pipeline frame%u sample%u\n",frame,i);return 2;}
        if(fixture.is_open()){
            fixture<<"{{";for(unsigned i=0;i<5;++i)fixture<<uint16_t(memory[0x800+i])<<',';
            fixture<<"},{";for(unsigned i=0;i<16;++i)fixture<<uint16_t(memory[0x1400+i])<<',';
            fixture<<"},{";for(auto x:adpcm)fixture<<uint16_t(x)<<',';
            fixture<<"},{";for(auto x:resample)fixture<<uint16_t(x)<<',';
            fixture<<"}},\n";
        }
    }
    if(fixture.is_open()){
        fixture<<"};\n";fixture.close();
        std::ofstream firmware;firmware.exceptions(std::ios::failbit|std::ios::badbit);
        firmware.open(std::string(argv[1])+"/typed-firmware-words.bin",std::ios::binary);
        for(auto w:p.words){firmware.put(char(w));firmware.put(char(w>>8));}
    }
    puts("PASS: 200 resident decode-copy-resample-mix batches, one dispatch each, production PCM/state match");
}
