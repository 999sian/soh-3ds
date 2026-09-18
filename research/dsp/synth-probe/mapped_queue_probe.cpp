#include <cstdio>
#include <cstring>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
int main(){
    auto p=makeTypedFirmware(true,true,true,true);Teakra::Teakra dsp({});
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    for(unsigned i=0;i<0x5800;++i)dsp.DataWrite(i,0);
    dsp.Run(700);
    if(dsp.DataRead(0x10)!=0x4458 || !dsp.RecvDataIsReady(2) || dsp.RecvData(2))return 1;
    std::vector<uint16_t> memory(0x1000);uint32_t rng=0x513fed;
    for(auto& x:memory){rng=rng*1664525u+1013904223u;x=rng>>16;}
    // Includes untouched scratch/gap and guards around the resident DMEM.
    for(unsigned i=0;i<memory.size();++i)dsp.DataWrite(0x4800+i,memory[i]);
    auto* data=reinterpret_cast<uint8_t*>(memory.data()+0x800);
    unsigned jobs=0;
    for(unsigned trial=0;trial<100;++trial){
        unsigned commands=trial%2?32:1,status=0;
        for(unsigned i=0;i<commands;++i){
            unsigned type=i%3==0?8:7,bytes=trial%3==0?512:trial%3==1?17:0;
            unsigned source=(i*31)%1024,dest=(i*17+trial)%1024;
            if(trial%5==0 && i==commands/2){bytes=3073;}
            dsp.DataWrite(0x100+4*i,type);dsp.DataWrite(0x101+4*i,bytes);
            dsp.DataWrite(0x102+4*i,source);dsp.DataWrite(0x103+4*i,dest);
            if(!status){
                if(bytes>3072)status=3;
                else if(type==8)std::memset(data+dest,0,bytes);
                else std::memmove(data+dest,data+source,bytes);
            }
        }
        dsp.DataWrite(7,commands);dsp.DataWrite(3,trial+1);dsp.DataWrite(0,1);dsp.Run(1200000);
        if(dsp.DataRead(0) || dsp.DataRead(0x11)!=trial+1 || dsp.DataRead(0x12)!=status || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=trial+1){std::printf("FAIL mapped dispatch%u\n",trial);return 2;}
        for(unsigned i=0;i<memory.size();++i)if(dsp.DataRead(0x4800+i)!=memory[i]){std::printf("FAIL mapped queue%u address%x\n",trial,0x4800+i);return 3;}
        ++jobs;
    }
    // A real synthesis kernel between transfers catches mode/register handoff.
    const uint16_t mixed[]={7,18,0,2, 0,16,0x8000,0, 8,2,0,3};
    for(unsigned i=0;i<12;++i)dsp.DataWrite(0x100+i,mixed[i]);
    int16_t expected[16];
    for(unsigned i=0;i<16;++i){
        int input=-16000+int(i)*123,output=5000-int(i)*450;
        dsp.DataWrite(0x1000+i,uint16_t(input));dsp.DataWrite(0x1400+i,uint16_t(output));
        int difference=output-input;expected[i]=difference>32767?32767:difference<-32768?-32768:difference;
    }
    std::memmove(data+2,data,18);std::memset(data+3,0,2);
    dsp.DataWrite(7,3);dsp.DataWrite(3,101);dsp.DataWrite(0,1);dsp.Run(100000);
    if(dsp.DataRead(0) || dsp.DataRead(0x11)!=101 || dsp.DataRead(0x12) || !dsp.RecvDataIsReady(0) || dsp.RecvData(0)!=101)return 5;
    for(unsigned i=0;i<16;++i)if(int16_t(dsp.DataRead(0x1400+i))!=expected[i])return 6;
    for(unsigned i=0;i<memory.size();++i)if(dsp.DataRead(0x4800+i)!=memory[i])return 7;
    ++jobs;
    dsp.SendData(2,0x8000);dsp.Run(700);
    if(!dsp.RecvDataIsReady(2) || dsp.RecvData(2))return 4;
    std::printf("PASS: %u mapped resident queues, 1/32 descriptors, byte extents, partial abort/recovery, mixed synthesis handoff, DR0 completion and shutdown; %zu firmware words\n",jobs,p.words.size());
}
