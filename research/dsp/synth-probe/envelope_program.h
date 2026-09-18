#pragma once
#include "program.h"
// r0 input; r1/r2 dry L/R; r3/r4 wet L/R; r6 eight-sample blocks minus1.
// 3b00..02 initial unsigned volumes L/R/wet; 3b03..05 unsigned rates.
// Working volumes/sample pair at3b10..14. Setup values remain unchanged, as
// production aEnvMixerImpl advances local volume copies only.
// flags: bit0 swap wet channels, bit1 wet-left xor-4, bit2 wet-right xor-2,
// bit3 dry-left xor-1, bit4 dry-right xor-1. r5/r7 and accumulators clobbered.
// Runtime mode reads flags at3b06 and uses scratch3b15..19 for masks/swap.
inline Program makeEnvelope(unsigned flags,bool runtimeFlags=false){
    Program p;
    for(unsigned i=0;i<3;++i){
        p.Emit("mov 0x0000 r5",0x3b00+i);p.Emit("mov [r5] a0l");
        p.Emit("mov 0x0000 r5",0x3b10+i);p.Emit("mov a0l [r5]");
    }
    if(runtimeFlags){
        const unsigned bits[]={3,4,1,2},shifts[]={0,0,2,1};
        for(unsigned i=0;i<4;++i){
            p.Emit("mov 0x0000 r5",0x3b06);p.Emit("mov [r5] a0");
            p.Emit("shfi a0 a0 -0x000"+std::to_string(bits[i]));p.Emit("and 0x0000 a0",1);
            if(shifts[i])p.Emit("shfi a0 a0 +0x000"+std::to_string(shifts[i]));
            p.Emit("clr a1 always");p.Emit("sub a0 a1");
            p.Emit("mov 0x0000 r5",0x3b15+i);p.Emit("mov a1l [r5]");
        }
        p.Emit("mov 0x0000 r5",0x3b06);p.Emit("mov [r5] a0");p.Emit("and 0x0000 a0",1);
        p.Emit("mov 0x0000 r5",0x3b19);p.Emit("mov a0l [r5]");
    }
    p.Emit("bkrep r6 0x00000000");unsigned repeat=p.words.size()-1;
    for(unsigned sample=0;sample<8;++sample){
        p.Emit("mov [r0++] y0");
        for(unsigned channel=0;channel<2;++channel){
            p.Emit("mov 0x0000 r5",0x3b10+channel);
            p.Emit("mpysu y0 [r5] a0");p.Emit("mov p* a0");p.Emit("shfi a0 a0 -0x0010");
            if(runtimeFlags){p.Emit("mov 0x0000 r5",0x3b15+channel);p.Emit("xor [r5] a0");}
            else if(flags&(8u<<channel))p.Emit("xor 0x0000 a0",0xffff);
            p.Emit("mov 0x0000 r5",0x3b13+channel);p.Emit("mov a0l [r5]");
        }
        for(unsigned channel=0;channel<2;++channel){
            // Preserve production dryL/wetL/dryR/wetR store order. Aliased
            // output buffers make reordering saturated additions observable.
            p.Emit("mov 0x0000 r5",0x3b13+channel);
            // Reload to sign-extend the possibly XORed 16-bit sample.
            p.Emit("mov [r5] a0");p.Emit(channel?"add [r2] a0":"add [r1] a0");
            p.Emit("shfi a0 a0 +0x0010");p.Emit(channel?"mov a0h [r2++]":"mov a0h [r1++]");
            if(runtimeFlags){
                p.Emit("mov 0x0000 r5",0x3b19);p.Emit("mov [r5] a0");
                if(channel)p.Emit("xor 0x0000 a0",1);
                p.Emit("add 0x0000 a0",0x3b13);p.Emit("mov a0l r5");
            }else p.Emit("mov 0x0000 r5",0x3b13+(channel^unsigned(bool(flags&1))));
            p.Emit("mov [r5] y0");
            p.Emit("mov 0x0000 r5",0x3b12);p.Emit("mpysu y0 [r5] a0");p.Emit("mov p* a0");p.Emit("shfi a0 a0 -0x0010");
            if(runtimeFlags){p.Emit("mov 0x0000 r5",0x3b17+channel);p.Emit("xor [r5] a0");}
            else if(flags&(2u<<channel))p.Emit("xor 0x0000 a0",channel?0xfffe:0xfffc);
            // Register move to accumulator sign-extends the low word.
            p.Emit("mov a0l r7");p.Emit("mov r7 a0");p.Emit(channel?"add [r4] a0":"add [r3] a0");
            p.Emit("shfi a0 a0 +0x0010");p.Emit(channel?"mov a0h [r4++]":"mov a0h [r3++]");
        }
    }
    for(unsigned i=0;i<3;++i){
        p.Emit("mov 0x0000 r5",0x3b10+i);p.Emit("mov [r5] a0");
        p.Emit("mov 0x0000 r5",0x3b03+i);p.Emit("add [r5] a0");
        p.Emit("mov 0x0000 r5",0x3b10+i);p.Emit("mov a0l [r5]");
    }
    p.words[repeat]=p.words.size()-1;p.relocations.push_back(repeat);
    unsigned done=p.words.size();p.Emit("br 0x00000000 always",done);p.relocations.push_back(p.words.size()-1);
    return p;
}
