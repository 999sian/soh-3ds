#include <cstdio>
#include <stdexcept>
#include "teakra/teakra.h"
#include "typed_firmware.h"

int main() {
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    auto p=makeTypedFirmware();Teakra::Teakra dsp({});
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    for(unsigned i=0;i<0x4100;++i)dsp.DataWrite(i,0);
    dsp.Run(500);require(dsp.RecvDataIsReady(2),"boot reply");dsp.RecvData(2);
    unsigned seq=0,jobs=0;
    auto descriptor=[&](unsigned slot,unsigned type,unsigned count,unsigned arg=0,unsigned flags=0){
        unsigned addr=0x100+slot*4;
        dsp.DataWrite(addr,type);dsp.DataWrite(addr+1,count);dsp.DataWrite(addr+2,arg);dsp.DataWrite(addr+3,flags);
    };
    auto submit=[&](unsigned count,unsigned status){
        dsp.DataWrite(7,count);dsp.DataWrite(3,++seq);dsp.DataWrite(0,1);dsp.Run(100000);
        require(!dsp.DataRead(0) && dsp.DataRead(0x11)==seq,"completion/sequence");
        require(dsp.DataRead(0x12)==status,"status");++jobs;
    };
    auto seed=[&](){for(unsigned i=0;i<16;++i){dsp.DataWrite(0x1000+i,1);dsp.DataWrite(0x1400+i,100);}};
    auto output=[&](unsigned want){for(unsigned i=0;i<16;++i)require(dsp.DataRead(0x1400+i)==want,"gain count/partial progress");};
    // Every subtract command changes output by exactly one. Executing beyond
    // the last descriptor is observable even if descriptor memory is read-only.
    for(unsigned i=0;i<33;++i)descriptor(i,0,16,0x8000);
    for(unsigned n:{1u,32u}){seed();submit(n,0);output(100-n);}
    require(dsp.DataRead(0x180)==0 && dsp.DataRead(0x181)==16 && dsp.DataRead(0x182)==0x8000 && !dsp.DataRead(0x183),"descriptor sentinel");
    for(unsigned n:{33u,65535u}){seed();submit(n,3);output(100);}
    // The valid first command runs, the invalid middle command aborts, and
    // the third command must not run. Retrying needs fresh caller state.
    descriptor(1,65535,16);seed();submit(3,3);output(99);
    descriptor(1,0,16,0x8000);seed();submit(1,0);output(99);
    // Abort again, then switch to single-command mode: stale queue remaining
    // must not execute the valid trailing descriptor.
    descriptor(1,65535,16);seed();submit(3,3);output(99);
    seed();dsp.DataWrite(5,0);dsp.DataWrite(1,16);dsp.DataWrite(2,0x8000);dsp.DataWrite(6,0);submit(0,0);output(99);
    // Independent copy test verifies the full fixed-size copy and both guards.
    for(unsigned i=0;i<18;++i)dsp.DataWrite(0x3ff+i,0x5a3c);
    for(unsigned i=0;i<16;++i)dsp.DataWrite(0x3010+i,0x8910+i);
    descriptor(0,4,16);submit(1,0);
    for(unsigned i=0;i<16;++i)require(dsp.DataRead(0x400+i)==0x8910+i,"copy data");
    require(dsp.DataRead(0x3ff)==0x5a3c && dsp.DataRead(0x410)==0x5a3c,"copy guards");
    descriptor(0,4,32);submit(1,3);
    dsp.SendData(2,0x8000);dsp.Run(500);require(dsp.RecvDataIsReady(2) && !dsp.RecvData(2),"shutdown");
    printf("PASS: %u queue boundary/abort/recovery/copy jobs, counts 1/32/33/65535 and descriptor sentinel\n",jobs);
}
