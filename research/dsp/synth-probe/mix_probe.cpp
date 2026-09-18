// Experimental DSP instruction probe. Not linked into the game.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "teakra/disassembler.h"
#include "parser.h"

#include "program.h"
int16_t Reference(int16_t input, int16_t output, int16_t gain) {
    int64_t value;
    if (gain == INT16_MIN) value = int32_t(output) - input;
    else {
        value = int64_t(output) * 32767 + int64_t(input) * gain + 16384;
        // Define negative rounding without relying on host signed shifts.
        value = value >= 0 ? value / 32768 : -((-value + 32767) / 32768);
    }
    return int16_t(std::clamp<int64_t>(value, INT16_MIN, INT16_MAX));
}
#include "production_mix_reference.h"

int main(int argc, char** argv) {
    auto normal = MakeProgram(false), subtract = MakeProgram(true);
    Teakra::Teakra dsp({});
    uint32_t rng=0x12345678; uint64_t checked=0;
    const int16_t edges[]={INT16_MIN,-32767,-16384,-1,0,1,16384,32766,INT16_MAX};
    auto check = [&](int16_t in, int16_t out, int16_t gain) {
        const Program& p = gain == INT16_MIN ? subtract : normal;
        dsp.Reset();
        for (unsigned i=0; i<p.words.size(); ++i) dsp.ProgramWrite(i,p.words[i]);
        auto& regs=dsp.GetRegisterState();
        regs.r[0]=0x1000;regs.r[1]=0x2000;regs.r[2]=uint16_t(gain);
        dsp.DataWrite(0x1000,uint16_t(in));dsp.DataWrite(0x2000,uint16_t(out));
        dsp.Run(p.instructions);
        int16_t actual=int16_t(dsp.DataRead(0x2000));
        int16_t expected=Reference(in,out,gain);
        if (actual!=expected) {
            std::fprintf(stderr,"Mismatch in=%d out=%d gain=%d expected=%d actual=%d pc=%u a0=%lld\n",in,out,gain,expected,actual,regs.pc,(long long)regs.a[0]);
            throw std::runtime_error("DSP mismatch");
        }
        assert(regs.r[0]==0x1001 && regs.r[1]==0x2001);
        ++checked;
    };
    for (auto gain:edges) for(auto in:edges) for(auto out:edges) check(in,out,gain);
    for (int i=0;i<20000;++i) {
        auto next=[&](){rng=rng*1664525u+1013904223u;return int16_t(rng>>16);};
        auto in=next(),out=next(),gain=next();check(in,out,gain);
    }
    std::printf("PASS: %llu DSP gain-mix samples match exact PCM reference\n",(unsigned long long)checked);
    auto blockNormal = MakeProgram(false, true), blockSubtract = MakeProgram(true, true);
    uint64_t buffers = 0, bufferSamples = 0;
    // Reset once: consecutive jobs must not depend on clean accumulator/product state.
    dsp.Reset();
    for (unsigned n : {16u,32u,64u,128u,192u,256u,512u}) {
        for (int offset : {-17,-1,0,1,17,1024}) for (int trial=0;trial<36;++trial) {
            int16_t gain = trial<9 ? edges[trial] : int16_t(rng>>16);
            const auto& p = gain==INT16_MIN ? blockSubtract : blockNormal;
            for (unsigned i=0;i<p.words.size();++i) dsp.ProgramWrite(i,p.words[i]);
            for (unsigned i=0;i<4096;++i) {
                rng=rng*1664525u+1013904223u;
                oracleMemory[i]=int16_t(rng>>16);
                dsp.DataWrite(i,uint16_t(oracleMemory[i]));
            }
            unsigned source=1024,dest=unsigned(int(source)+offset);
            aMixImplRef(n/8,gain,source*2,dest*2);
            auto& regs=dsp.GetRegisterState();
            regs.pc=0;regs.r[0]=source;regs.r[1]=dest;regs.r[2]=uint16_t(gain);regs.r[6]=n-1;
            dsp.Run(1+(p.instructions-1)*n);
            for (unsigned i=0;i<4096;++i) if (int16_t(dsp.DataRead(i))!=oracleMemory[i]) {
                std::fprintf(stderr,"Buffer mismatch n=%u offset=%d trial=%d index=%u\n",n,offset,trial,i);
                throw std::runtime_error("DSP buffer mismatch");
            }
            if (regs.r[0]!=source+n || regs.r[1]!=dest+n || regs.pc!=p.words.size())
                throw std::runtime_error("DSP loop/pointer mismatch");
            ++buffers;bufferSamples+=n;
        }
    }
    std::printf("PASS: %llu buffers / %llu samples match production aMixImplRef; overlaps, canaries and consecutive jobs checked\n",
        (unsigned long long)buffers,(unsigned long long)bufferSamples);
    if(argc>1) {
        for(auto item: {std::pair{"gain",&normal},std::pair{"subtract",&subtract},std::pair{"gain-block",&blockNormal},std::pair{"subtract-block",&blockSubtract}}) {
            std::ofstream binary(std::string(argv[1])+"/"+item.first+".bin",std::ios::binary);
            std::ofstream listing(std::string(argv[1])+"/"+item.first+".txt");
            for(auto word:item.second->words){binary.put(char(word&255));binary.put(char(word>>8));}
            for(size_t i=0;i<item.second->words.size();++i) {
                auto op=item.second->words[i];bool exp=Teakra::Disassembler::NeedExpansion(op);
                listing<<Teakra::Disassembler::Do(op,exp?item.second->words[i+1]:0)<<'\n';if(exp)++i;
            }
        }
    }
}
