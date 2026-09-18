#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
extern "C" void adpcmReference(int16_t*,unsigned,unsigned,int16_t*,int16_t*,const int16_t*);
extern "C" void resampleReference(int16_t*,uint16_t,uint16_t,uint16_t,uint8_t,uint16_t,int16_t*);
extern "C" const int16_t* productionTable();
extern "C" void envelopeReference(int16_t*,unsigned,unsigned,const uint16_t*,const uint16_t*,const uint16_t*);
extern "C" void filterReferenceAt(int16_t*,unsigned,unsigned,int16_t*,int16_t*,uint16_t);
int main(int argc,char** argv){
    std::ofstream fixture;
    if(argc>1){fixture.exceptions(std::ios::failbit|std::ios::badbit);fixture.open(std::string(argv[1])+"/chain_fixture.h");}
#ifdef SOH_DSP_MULTIBLOCK_TEST
    constexpr bool multiBlock=true;
#else
    constexpr bool multiBlock=false;
#endif
    Teakra::Teakra dsp({});auto p=makeTypedFirmware(true,multiBlock);
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    for(unsigned i=0;i<0x4100;++i)dsp.DataWrite(i,0);dsp.Run(500);
    if(dsp.DataRead(0x10)!=(multiBlock?0x4456:0x4455) || !dsp.RecvDataIsReady(2))return 1;dsp.RecvData(2);
    std::vector<int16_t> memory(0x4000);int16_t adpcm[16]={},resample[16]={},loop[16]={},book[128];
    uint32_t rng=0x515abc;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    for(unsigned i=0;i<128;++i){book[i]=int16_t(next()%8192)-4096;dsp.DataWrite(0x2000+i,uint16_t(book[i]));}
    for(unsigned i=0;i<256;++i)dsp.DataWrite(0x4000+i,uint16_t(productionTable()[i]));
    for(unsigned i=0;i<16;++i)dsp.DataWrite(0x2800+i,uint16_t(1u<<i));
    if(fixture.is_open()){
        fixture<<"#pragma once\n#include <cstdint>\nstatic const uint16_t pipelineBook[128]={";
        for(auto x:book)fixture<<uint16_t(x)<<',';
        fixture<<"};\nstatic const uint16_t pipelineTable[256]={";
        for(unsigned i=0;i<256;++i)fixture<<uint16_t(productionTable()[i])<<',';
        fixture<<"};\nstruct ChainFrame {uint16_t packed[5],vols[3],rates[3],coefficients[8],pcm[4][16],adpcm[16],resample[16],filter[16],filteredCoefficients[8];};\nstatic const ChainFrame pipelineFrames[256]={\n";
    }
    for(unsigned frame=0;frame<256;++frame){
        for(unsigned i=0;i<5;++i){memory[0x800+i]=next();dsp.DataWrite(0x800+i,uint16_t(memory[0x800+i]));}
        auto bytes=reinterpret_cast<uint8_t*>(memory.data())+0x1000;bytes[0]=((frame%12)<<4)|(frame%8);dsp.DataWrite(0x800,uint16_t(memory[0x800]));
        unsigned flags=frame==0?1:0;
        adpcmReference(memory.data(),32,flags,adpcm,loop,book);
        memcpy(memory.data()+0x400,memory.data()+0x3010,32);
        resampleReference(memory.data(),0x800,0x2000,32,flags,0x8000,resample);
        uint16_t vols[3],rates[3],bases[]={0x1000,0x1400,0x1800,0x1c00,0x2c00};
        for(unsigned i=0;i<3;++i){vols[i]=next();rates[i]=next();dsp.DataWrite(0x3b00+i,vols[i]);dsp.DataWrite(0x3b03+i,rates[i]);}
        envelopeReference(memory.data(),16,frame%32,vols,rates,bases);
        uint16_t coefficients[8];
        for(unsigned i=0;i<8;++i){coefficients[i]=next();memory[0x3c10+i]=int16_t(coefficients[i]);dsp.DataWrite(0x3c10+i,coefficients[i]);}
        filterReferenceAt(memory.data(),32,flags,memory.data()+0x3c00,memory.data()+0x3c10,0x2800);
        const uint16_t queue[]={2,16,0,uint16_t(flags),4,16,0,0,1,16,0x8000,uint16_t(flags),5,16,uint16_t(frame%32),0,6,16,0,uint16_t(flags)};
        for(unsigned i=0;i<20;++i)dsp.DataWrite(0x100+i,queue[i]);
        dsp.DataWrite(3,frame+1);dsp.DataWrite(7,5);dsp.DataWrite(0,1);dsp.Run(70000);
        if(dsp.DataRead(0) || dsp.DataRead(0x11)!=frame+1 || dsp.DataRead(0x12)){printf("FAIL chain dispatch frame%u status%u\n",frame,dsp.DataRead(0x12));return 2;}
        for(unsigned i=0;i<16;++i){
            for(unsigned base:{0x1400,0x1800,0x1c00,0x2c00,0x3c00})if(int16_t(dsp.DataRead(base+i))!=memory[base+i]){printf("FAIL chain frame%u addr%x\n",frame,base+i);return 3;}
            if(int16_t(dsp.DataRead(0x3800+i))!=adpcm[i] || int16_t(dsp.DataRead(0x3a00+i))!=resample[i])return 4;
        }
        for(unsigned i=0;i<8;++i)if(int16_t(dsp.DataRead(0x3c10+i))!=memory[0x3c10+i])return 5;
        for(unsigned i=0;i<3;++i)if(dsp.DataRead(0x3b00+i)!=vols[i] || dsp.DataRead(0x3b03+i)!=rates[i])return 6;
        if(fixture.is_open()){
            fixture<<"{{";for(unsigned i=0;i<5;++i)fixture<<uint16_t(memory[0x800+i])<<',';
            fixture<<"},{";for(auto x:vols)fixture<<x<<',';
            fixture<<"},{";for(auto x:rates)fixture<<x<<',';
            fixture<<"},{";for(auto x:coefficients)fixture<<x<<',';
            fixture<<"},{";for(unsigned base:{0x1400,0x1800,0x1c00,0x2c00}){fixture<<'{';for(unsigned i=0;i<16;++i)fixture<<uint16_t(memory[base+i])<<',';fixture<<"},";}
            fixture<<"},{";for(auto x:adpcm)fixture<<uint16_t(x)<<',';
            fixture<<"},{";for(auto x:resample)fixture<<uint16_t(x)<<',';
            fixture<<"},{";for(unsigned i=0;i<16;++i)fixture<<uint16_t(memory[0x3c00+i])<<',';
            fixture<<"},{";for(unsigned i=0;i<8;++i)fixture<<uint16_t(memory[0x3c10+i])<<',';
            fixture<<"}},\n";
        }
    }
    unsigned seq=256,commandJobs=0;
    std::vector<unsigned> counts={0,8,16,32,64,128,192,256,512};
    if(multiBlock)for(unsigned n:{24,40,152,160,168,176,184,504})counts.push_back(n);
    for(unsigned type:{5u,6u})for(unsigned count:counts)for(unsigned variation=0;variation<3;++variation){
        for(auto& x:memory)x=int16_t(next());
        for(unsigned i=0x100;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
        unsigned flags=type==6?variation%2:0,arg=variation==0?0:variation==1?17:31;
        if(type==5){
            uint16_t vols[3],rates[3],bases[]={0x1000,0x1400,0x1800,0x1c00,0x2c00};
            for(unsigned i=0;i<3;++i){vols[i]=uint16_t(memory[0x3b00+i]);rates[i]=uint16_t(memory[0x3b03+i]);}
            memory[0x3b06]=arg;envelopeReference(memory.data(),count,arg,vols,rates,bases);
        }else filterReferenceAt(memory.data(),count*2,flags,memory.data()+0x3c00,memory.data()+0x3c10,0x2800);
        dsp.DataWrite(5,type);dsp.DataWrite(1,count);dsp.DataWrite(2,arg);dsp.DataWrite(6,flags);dsp.DataWrite(7,0);dsp.DataWrite(3,++seq);dsp.DataWrite(0,1);dsp.Run(200000);
        if(dsp.DataRead(0) || dsp.DataRead(0x11)!=seq || dsp.DataRead(0x12))return 9;
        for(unsigned i=0x100;i<memory.size();++i){
            if(type==5 && i>=0x3b10 && i<0x3b1a)continue;
            if(type==6 && i>=0x3c20 && i<0x3c30)continue;
            if(int16_t(dsp.DataRead(i))!=memory[i]){printf("FAIL extended type%u count%u variant%u addr%x\n",type,count,variation,i);return 10;}
        }
        ++commandJobs;
    }
    for(unsigned type:{5u,6u})for(unsigned bad:{32u,65535u}){
        dsp.DataWrite(5,type);dsp.DataWrite(1,type==5?16:bad);dsp.DataWrite(2,bad);dsp.DataWrite(6,0);dsp.DataWrite(7,0);dsp.DataWrite(3,++seq);
        // 32 is a valid filter count: use an invalid flag for that case.
        if(type==6 && bad==32)dsp.DataWrite(6,2);
        dsp.DataWrite(0,1);
        dsp.Run(1000);if(dsp.DataRead(0) || dsp.DataRead(0x11)!=seq || dsp.DataRead(0x12)!=3)return 7;
    }
    dsp.SendData(2,0x8000);dsp.Run(500);if(!dsp.RecvDataIsReady(2) || dsp.RecvData(2))return 8;
    if(fixture.is_open()){
        fixture<<"};\n";fixture.close();
        std::ofstream firmware;firmware.exceptions(std::ios::failbit|std::ios::badbit);
        firmware.open(std::string(argv[1])+"/extended-firmware-words.bin",std::ios::binary);
        for(auto w:p.words){firmware.put(char(w));firmware.put(char(w>>8));}
    }
    printf("PASS: 256 resident decode-copy-resample-envelope-filter chains and %u standalone command cases, production outputs/state, count semantics, guards and invalid arguments; %zu DSP words\n",commandJobs,p.words.size());
}
