#include <cstdio>
#include <fstream>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "program.h"
#include "production_mix_reference.h"
#include "firmware.h"
int main(int argc, char** argv) {
    auto p = MakeFirmware();
    Teakra::Teakra dsp({});
    auto require=[](bool ok, const char* message) { if(!ok) throw std::runtime_error(message); };
    uint32_t rng=0x35a102;
    unsigned jobs=0;
    for(unsigned boot=0;boot<3;++boot) {
        dsp.Reset();
        for(unsigned i=0;i<p.words.size();++i) dsp.ProgramWrite(i,p.words[i]);
        for(unsigned i=0;i<0x1800;++i) dsp.DataWrite(i,0);
        // Firmware must initialize arithmetic and addressing, including dirty entry modes.
        auto& regs=dsp.GetRegisterState();regs.sat=1;regs.sata=0;regs.s=1;regs.ps[0]=3;
        regs.m[0]=regs.m[1]=1;regs.br[0]=regs.br[1]=1;
        dsp.Run(200);
        require(dsp.DataRead(0x10)==0x4453,"Firmware did not publish ready");
        require(dsp.RecvDataIsReady(2),"Missing loader reply");
        dsp.RecvData(2); // Loader consumes this; unload requires a fresh reply.
        for(unsigned n:{0u,1u,15u,16u,32u,64u,128u,192u,256u,512u,513u,65535u}) {
            for(int gain:{-32768,-32767,-1,0,1,16384,32767}) for(unsigned batch:{0u,1u,2u,8u,32u,33u,65535u}) {
                bool legal=n==0 || n==16 || n==32 || n==64 || n==128 || n==192 || n==256 || n==512;
                for(unsigned i=0;i<4096;++i) {
                    rng=rng*1664525u+1013904223u;
                    oracleMemory[i]=int16_t(rng>>16);
                    dsp.DataWrite(0x800+i,uint16_t(oracleMemory[i]));
                }
                bool batchLegal=batch==1 || batch==2 || batch==8 || batch==32;
                if(legal && batchLegal)for(unsigned b=0;b<batch;++b)aMixImplRef(n/8,gain,0x800*2,0xc00*2);
                uint16_t seq=uint16_t(++jobs); if(jobs==2)seq=65535;
                dsp.DataWrite(4,batch);dsp.DataWrite(1,n);dsp.DataWrite(2,uint16_t(gain));dsp.DataWrite(3,seq);dsp.DataWrite(0,1);
                dsp.Run(230000);
                require(dsp.DataRead(0)==0,"Firmware job timed out");
                require(dsp.DataRead(0x11)==seq,"Firmware sequence mismatch");
                require(dsp.DataRead(0x12)==(!batchLegal?2:legal?0:1),"Firmware validation status mismatch");
                for(unsigned i=0;i<4096;++i) require(int16_t(dsp.DataRead(0x800+i))==oracleMemory[i],"Firmware memory mismatch");
                dsp.Run(1000);
                require(dsp.DataRead(0)==0 && dsp.DataRead(0x11)==seq,"Idle changed completion");
            }
        }
        dsp.SendData(2,0x8000);dsp.Run(200);
        require(dsp.RecvDataIsReady(2),"Missing shutdown acknowledgement");
        require(dsp.RecvData(2)==0,"Bad shutdown acknowledgement");
    }
    std::printf("PASS: firmware ready, %u batched jobs, invalid batch counts and lengths, zero length, guards, idle, sequence and 3 boot/shutdown cycles\n",jobs);
    if(argc>1) {
        std::ofstream f(std::string(argv[1])+"/firmware-words.bin",std::ios::binary);
        for(auto w:p.words){f.put(char(w));f.put(char(w>>8));}
    }
}
