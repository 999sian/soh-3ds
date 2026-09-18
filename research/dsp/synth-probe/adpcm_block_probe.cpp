#include <algorithm>
#include <cstdio>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "adpcm_program.h"
int main() {
    Teakra::Teakra dsp({});uint32_t rng=0x72389a;unsigned samples=0;
    auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    for(bool twoBit:{false,true}) {
        auto p=makeAdpcmBlock(twoBit);
        for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
        for(unsigned i=0;i<16;++i)dsp.DataWrite(0x2800+i,uint16_t(1u<<i));
        for(unsigned shift=0;shift<16;++shift)for(unsigned selector=0;selector<16;++selector)for(unsigned trial=0;trial<8;++trial) {
            uint8_t bytes[10];for(auto& b:bytes)b=next();bytes[0]=(shift<<4)|selector;
            for(unsigned i=0;i<5;++i)dsp.DataWrite(0x800+i,uint16_t(bytes[i*2]|(bytes[i*2+1]<<8)));
            int16_t book[256],output[18];for(unsigned i=0;i<256;++i){book[i]=next();dsp.DataWrite(0x2000+i,uint16_t(book[i]));}
            output[0]=next();output[1]=next();dsp.DataWrite(0x2ffe,uint16_t(output[0]));dsp.DataWrite(0x2fff,uint16_t(output[1]));
            dsp.GetRegisterState().pc=0;dsp.Run(p.instructions);
            for(unsigned half=0;half<2;++half){
                int16_t delta[8];unsigned bits=twoBit?2:4,perByte=8/bits;
                for(unsigned j=0;j<8;++j){unsigned index=half*8+j,value=(bytes[1+index/perByte]>>(8-bits-bits*(index%perByte)))&((1<<bits)-1);int signedValue=int(value^(1<<(bits-1)))-(1<<(bits-1));delta[j]=int16_t(uint32_t(signedValue)<<shift);}
                for(unsigned j=0;j<8;++j){
                    const int16_t* b=book+selector*16;
                    uint32_t acc=uint32_t(int32_t(b[j])*output[half*8])+uint32_t(int32_t(b[8+j])*output[half*8+1])+(uint32_t(int32_t(delta[j]))<<11);
                    for(unsigned k=0;k<j;++k)acc+=uint32_t(int32_t(b[8+j-k-1])*delta[k]);
                    output[2+half*8+j]=std::clamp(int32_t(acc)>>11,-32768,32767);
                    if(int16_t(dsp.DataRead(0x3000+half*8+j))!=output[2+half*8+j]){printf("FAIL bits%u shift%u selector%u half%u j%u\n",bits,shift,selector,half,j);return 1;}++samples;
                }
            }
        }
    }
    printf("PASS: %u DSP ADPCM samples decoded from packed bytes, 2/4-bit modes, all shifts and books\n",samples);
}
