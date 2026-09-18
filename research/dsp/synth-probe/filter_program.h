#pragma once
#include "program.h"
// In-place eight-tap filter. r0 buffer, r6 eight-sample blocks minus1.
// State3c00[16], coefficient setup3c10[8], history scratch3c20[16].
// Mapped mode: r4 state base, shared coefficients7400; scratch remains3c20.
// Coefficients and state are updated exactly like production aFilterImpl.
inline Program makeFilter(bool init,bool mapped=false){
    Program p;
    auto stateAddress=[&](unsigned offset,const char* reg){
        if(mapped){p.Emit("mov r4 a1");if(offset)p.Emit("add 0x0000 a1",offset);p.Emit(std::string("mov a1l ")+reg);}
        else p.Emit(std::string("mov 0x0000 ")+reg,0x3c00+offset);
    };
    const unsigned coefficients=mapped?0x7400:0x3c10;
    for(unsigned i=0;i<8;++i){
        if(init)p.Emit("clr a0 always");
        else {stateAddress(i,"r1");p.Emit("mov [r1] a0l");}
        p.Emit("mov 0x0000 r1",0x3c20+i);p.Emit("mov a0l [r1]");
        p.Emit("mov 0x0000 r1",coefficients+i);p.Emit("mov [r1] a0");
        if(!init){stateAddress(8+i,"r1");p.Emit("add [r1] a0");}
        // Sum is in[-65536,65534]. Add one for negative sums so the
        // arithmetic shift rounds toward zero as C signed division does.
        p.Emit("mov a0 a1");p.Emit("shfi a1 a1 -0x0010");p.Emit("and 0x0000 a1",1);
        p.Emit("add a1 a0");p.Emit("shfi a0 a0 -0x0001");
        p.Emit("mov 0x0000 r1",coefficients+i);p.Emit("mov a0l [r1]");
    }
    p.Emit("bkrep r6 0x00000000");unsigned repeat=p.words.size()-1;
    p.Emit("mov r0 r1");p.Emit("mov 0x0000 r2",0x3c28);
    for(unsigned i=0;i<8;++i){p.Emit("mov [r1++] a0l");p.Emit("mov a0l [r2++]");}
    for(unsigned i=0;i<8;++i){
        p.Emit("clr a1 always");p.Emit("add 0x0000 a1",0x4000);
        for(unsigned j=0;j<8;++j){
            p.Emit("mov 0x0000 r1",0x3c20+i+j);p.Emit("mov [r1] y0");
            p.Emit("mov 0x0000 r2",coefficients+7-j);p.Emit("mpy y0 [r2] a0");p.Emit("add p* a1");
        }
        // Eight signed16 products fit 35 bits, so the DSP40 accumulator
        // preserves the C int64 sum; high-word saturation supplies clamp16.
        p.Emit("shfi a1 a1 +0x0001");p.Emit("mov a1h [r0++]");
    }
    p.Emit("mov 0x0000 r1",0x3c28);p.Emit("mov 0x0000 r2",0x3c20);
    for(unsigned i=0;i<8;++i){p.Emit("mov [r1++] a0l");p.Emit("mov a0l [r2++]");}
    p.words[repeat]=p.words.size()-1;p.relocations.push_back(repeat);
    p.Emit("mov 0x0000 r1",0x3c20);stateAddress(0,"r2");
    for(unsigned i=0;i<8;++i){p.Emit("mov [r1++] a0l");p.Emit("mov a0l [r2++]");}
    p.Emit("mov 0x0000 r1",coefficients);
    for(unsigned i=0;i<8;++i){p.Emit("mov [r1++] a0l");p.Emit("mov a0l [r2++]");}
    unsigned done=p.words.size();p.Emit("br 0x00000000 always",done);p.relocations.push_back(p.words.size()-1);
    return p;
}
