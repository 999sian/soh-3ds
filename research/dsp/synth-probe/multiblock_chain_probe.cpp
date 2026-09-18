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
    Teakra::Teakra dsp({});auto p=makeTypedFirmware(true,true);
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    for(unsigned i=0;i<0x4100;++i)dsp.DataWrite(i,0);dsp.Run(700);
    if(dsp.DataRead(0x10)!=0x4456 || !dsp.RecvDataIsReady(2))return 1;dsp.RecvData(2);
    std::vector<int16_t> mem(0x4000);int16_t adpcm[16]={},resample[16]={},loop[16]={},book[128];
    uint32_t rng=0x52c321;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    for(unsigned i=0;i<128;++i){book[i]=int16_t(next()%8192)-4096;dsp.DataWrite(0x2000+i,uint16_t(book[i]));}
    for(unsigned i=0;i<256;++i)dsp.DataWrite(0x4000+i,uint16_t(productionTable()[i]));
    for(unsigned i=0;i<16;++i)dsp.DataWrite(0x2800+i,uint16_t(1u<<i));
    unsigned checkedSamples=0;
    for(unsigned frame=0;frame<192;++frame){
        const unsigned counts[]={16,160,176,192,256,512};unsigned count=counts[(frame/2)%6];bool twoBit=frame&1;
        unsigned packedBytes=count/16*(twoBit?5:9);
        for(unsigned i=0;i<(packedBytes+1)/2;++i)mem[0x800+i]=int16_t(next());
        auto bytes=reinterpret_cast<uint8_t*>(mem.data())+0x1000;
        for(unsigned block=0;block<count/16;++block)bytes[block*(twoBit?5:9)]=((frame%16)<<4)|(block%8);
        for(unsigned i=0;i<(packedBytes+1)/2;++i)dsp.DataWrite(0x800+i,uint16_t(mem[0x800+i]));
        unsigned flags=frame%17==0?1:0;
        adpcmReference(mem.data(),count*2,flags|(twoBit?4:0),adpcm,loop,book);
        memcpy(mem.data()+0x400,mem.data()+0x3010,count*2);
        resampleReference(mem.data(),0x800,0x2000,count*2,flags,0x8000,resample);
        uint16_t vols[3],rates[3],bases[]={0x1000,0x1400,0x1800,0x1c00,0x2c00};
        for(unsigned i=0;i<3;++i){vols[i]=next();rates[i]=next();dsp.DataWrite(0x3b00+i,vols[i]);dsp.DataWrite(0x3b03+i,rates[i]);}
        envelopeReference(mem.data(),count,frame%32,vols,rates,bases);
        for(unsigned i=0;i<8;++i){mem[0x3c10+i]=int16_t(next());dsp.DataWrite(0x3c10+i,uint16_t(mem[0x3c10+i]));}
        filterReferenceAt(mem.data(),count*2,flags,mem.data()+0x3c00,mem.data()+0x3c10,0x2800);
        const uint16_t queue[]={uint16_t(twoBit?3:2),uint16_t(count),0,uint16_t(flags),4,uint16_t(count),0,0,1,uint16_t(count),0x8000,uint16_t(flags),5,uint16_t(count),uint16_t(frame%32),0,6,uint16_t(count),0,uint16_t(flags)};
        for(unsigned i=0;i<20;++i)dsp.DataWrite(0x100+i,queue[i]);
        dsp.DataWrite(3,frame+1);dsp.DataWrite(7,5);dsp.DataWrite(0,1);dsp.Run(700000);
        if(dsp.DataRead(0) || dsp.DataRead(0x11)!=frame+1 || dsp.DataRead(0x12)){printf("FAIL multiblock dispatch frame%u status%u\n",frame,dsp.DataRead(0x12));return 2;}
        for(unsigned base:{0x1400,0x1800,0x1c00,0x2c00})for(unsigned i=0;i<count;++i)if(int16_t(dsp.DataRead(base+i))!=mem[base+i]){printf("FAIL multiblock frame%u count%u addr%x\n",frame,count,base+i);return 3;}
        for(unsigned i=0;i<16;++i)if(int16_t(dsp.DataRead(0x3800+i))!=adpcm[i] || int16_t(dsp.DataRead(0x3a00+i))!=resample[i] || int16_t(dsp.DataRead(0x3c00+i))!=mem[0x3c00+i])return 4;
        for(unsigned i=0;i<8;++i)if(int16_t(dsp.DataRead(0x3c10+i))!=mem[0x3c10+i])return 5;
        checkedSamples+=count;
    }
    unsigned seq=192;
    std::vector<unsigned> copies={8,513,65535};for(unsigned count=0;count<=512;count+=16)copies.push_back(count);
    for(unsigned count:copies){
        for(unsigned i=0;i<514;++i){mem[0x3ff+i]=int16_t(next());dsp.DataWrite(0x3ff+i,uint16_t(mem[0x3ff+i]));}
        for(unsigned i=0;i<512;++i){mem[0x3010+i]=int16_t(next());dsp.DataWrite(0x3010+i,uint16_t(mem[0x3010+i]));}
        bool valid=count<=512 && count%16==0;
        if(valid)memcpy(mem.data()+0x400,mem.data()+0x3010,count*2);
        dsp.DataWrite(5,4);dsp.DataWrite(1,count);dsp.DataWrite(6,0);dsp.DataWrite(7,0);dsp.DataWrite(3,++seq);dsp.DataWrite(0,1);dsp.Run(20000);
        if(dsp.DataRead(0) || dsp.DataRead(0x11)!=seq || dsp.DataRead(0x12)!=(valid?0:3))return 7;
        for(unsigned i=0;i<514;++i)if(int16_t(dsp.DataRead(0x3ff+i))!=mem[0x3ff+i])return 8;
    }
    // An invalid odd-byte second header must fail before INIT zeros history.
    dsp.DataWrite(0x800,0);dsp.DataWrite(0x804,0x0800);
    for(unsigned i=0;i<48;++i)dsp.DataWrite(0x3000+i,0x5127);
    for(unsigned i=0;i<16;++i)dsp.DataWrite(0x3800+i,0x3921);
    dsp.DataWrite(5,2);dsp.DataWrite(1,32);dsp.DataWrite(6,1);dsp.DataWrite(3,++seq);dsp.DataWrite(0,1);dsp.Run(10000);
    if(dsp.DataRead(0) || dsp.DataRead(0x11)!=seq || dsp.DataRead(0x12)!=3)return 9;
    for(unsigned i=0;i<48;++i)if(dsp.DataRead(0x3000+i)!=0x5127)return 10;
    for(unsigned i=0;i<16;++i)if(dsp.DataRead(0x3800+i)!=0x3921)return 11;
    dsp.SendData(2,0x8000);dsp.Run(700);if(!dsp.RecvDataIsReady(2) || dsp.RecvData(2))return 6;
    if(argc>1){std::ofstream f;f.exceptions(std::ios::failbit|std::ios::badbit);f.open(std::string(argv[1])+"/multiblock-firmware-words.bin",std::ios::binary);for(auto w:p.words){f.put(char(w));f.put(char(w>>8));}}
    printf("PASS: 192 multiblock resident synthesis chains / %u samples per channel, 16/160/176/192/256/512 samples, both codecs and persistent state; %zu DSP words\n",checkedSamples,p.words.size());
}
