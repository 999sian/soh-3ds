#include "mapped_probe_cpu.h"
#include "cpu_replay_dispatch.h"
void mappedProbeExecute(const ResidentDsp::CpuCall& c,const void* p){ResidentDsp::executeCpuCall(c,p);}
void MappedProbeFixture::sequence(){
        aLoadADPCMImpl(256,book.data());aLoadBufferImpl(compressed.data(),0xdc0,18);
        aSetBufferImpl(0,0xdc0,0x3c0,64);aADPCMdecImpl(continueHistory?0:2,decoder.data());
        aSetBufferImpl(0,0x3e0,0x600,32);aResampleImpl(continueHistory?0:1,0x4000,resampler.data());
        aSetBufferImpl(0,0xdc0,0x700,32);aS8DecImpl(continueHistory?0:2,s8.data());
        aFilterImpl(2,32,coeff.data());aFilterImpl(continueHistory?0:1,0x600,filter.data());
        aHiLoGainImpl(8,0,0x600);aSaveBufferImpl(0x600,output.data(),32);
    }

void StreamProbeFixture::sequence(){
    aLoadBufferImpl(input.data(),0x3e0,1024);
    aSetBufferImpl(0,0x3e0,0x800,1024);
    aResampleImpl(continuation?0:1,0x4000,resampler.data());
    aFilterImpl(2,1024,coefficients.data());
    aFilterImpl(continuation?0:1,0x800,filter.data());
    aSaveBufferImpl(0x800,output.data(),1024);
}
