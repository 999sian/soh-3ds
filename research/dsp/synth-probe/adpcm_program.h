#pragma once
#include <cstdio>
#include "program.h"
inline Program makeAdpcmHalf(unsigned output=0x3000,unsigned book=0x2000) {
    Program p;
    for(unsigned j=0;j<8;++j) {
        p.Emit("mov 0x0000 r0",output-2);p.Emit("mov [r0] y0");
        p.Emit("mov 0x0000 r1",book+j);p.Emit("mpy y0 [r1] a0");p.Emit("mov p* a0");
        p.Emit("mov 0x0000 r0",output-1);p.Emit("mov [r0] y0");
        p.Emit("mov 0x0000 r1",book+8+j);p.Emit("mpy y0 [r1] a1");p.Emit("add p* a0");
        p.Emit("mov 0x0000 r0",0x1000+j);p.Emit("mov [r0] a1");p.Emit("shfi a1 a1 +0x000b");p.Emit("add a1 a0");
        for(unsigned k=0;k<j;++k) {
            p.Emit("mov 0x0000 r0",0x1000+k);p.Emit("mov [r0] y0");
            p.Emit("mov 0x0000 r1",book+8+j-k-1);p.Emit("mpy y0 [r1] a1");p.Emit("add p* a0");
        }
        // Match ARM11's modulo32 accumulator before signed Q11 shift/clipping.
        p.Emit("shfi a0 a0 +0x0008");p.Emit("shfi a0 a0 -0x0008");
        p.Emit("shfi a0 a0 +0x0005");p.Emit("mov 0x0000 r2",output+j);p.Emit("mov a0h [r2]");
    }
    return p;
}
// Packed little-endian compressed bytes at800, 16 predictor books at2000,
// selected book scratch2500, factors2800[16]. Output3000[16], prev2ffe/2fff.
inline Program makeAdpcmBlock(bool twoBit,unsigned output=0x3000,unsigned offset=0) {
    Program p;
    p.Emit("mov 0x0000 r0",0x800+offset/2);p.Emit("mov [r0] a0");
    if(offset&1)p.Emit("shfi a0 a0 -0x0008");
    p.Emit("and 0x0000 a0",15);p.Emit("shfi a0 a0 +0x0004");p.Emit("add 0x0000 a0",0x2000);p.Emit("mov a0l r1");
    p.Emit("mov 0x0000 r0",0x2500);
    for(unsigned i=0;i<16;++i){p.Emit("mov [r1++] a0l");p.Emit("mov a0l [r0++]");}
    p.Emit("mov 0x0000 r0",0x800+offset/2);p.Emit("mov [r0] a0");
    if(offset&1)p.Emit("shfi a0 a0 -0x0008");
    p.Emit("and 0x0000 a0",0xf0);p.Emit("shfi a0 a0 -0x0004");p.Emit("add 0x0000 a0",0x2800);p.Emit("mov a0l r4");
    for(unsigned half=0;half<2;++half) {
        for(unsigned i=0;i<8;++i) {
            unsigned j=half*8+i,perByte=twoBit?4:2,bits=twoBit?2:4;
            unsigned byte=offset+1+j/perByte,shift=(byte%2)*8+8-bits-bits*(j%perByte);
            p.Emit("mov 0x0000 r0",0x800+byte/2);p.Emit("mov [r0] a0");
            if(shift){char op[64];std::snprintf(op,sizeof(op),"shfi a0 a0 -0x%04x",shift);p.Emit(op);}
            p.Emit("and 0x0000 a0",(1<<bits)-1);p.Emit("xor 0x0000 a0",1<<(bits-1));p.Emit("sub 0x0000 a0",1<<(bits-1));
            p.Emit("mov a0l y0");p.Emit("mpy y0 [r4] a0");p.Emit("mov p* a0");
            p.Emit("mov 0x0000 r0",0x1000+i);p.Emit("mov a0l [r0]");
        }
        auto h=makeAdpcmHalf(output+half*8,0x2500);p.words.insert(p.words.end(),h.words.begin(),h.words.end());p.instructions+=h.instructions;
    }
    return p;
}
// Full history lifecycle for a fixed validated block count; flags0/1/2 select
// continue/init/loop. State3800, loop3900, output3000 (16 history samples first).
inline Program makeAdpcmState(bool twoBit,unsigned blocks,unsigned flags) {
    Program p;p.Emit("mov 0x0000 r0",0x3000);
    if(flags==1) {
        p.Emit("clr a0 always");for(unsigned i=0;i<16;++i)p.Emit("mov a0l [r0++]");
    }else{
        p.Emit("mov 0x0000 r1",flags==2?0x3900:0x3800);
        for(unsigned i=0;i<16;++i){p.Emit("mov [r1++] a0l");p.Emit("mov a0l [r0++]");}
    }
    for(unsigned block=0;block<blocks;++block) {
        auto k=makeAdpcmBlock(twoBit,0x3010+block*16,block*(twoBit?5:9));
        p.words.insert(p.words.end(),k.words.begin(),k.words.end());p.instructions+=k.instructions;
    }
    p.Emit("mov 0x0000 r0",0x3000+blocks*16);p.Emit("mov 0x0000 r1",0x3800);
    for(unsigned i=0;i<16;++i){p.Emit("mov [r0++] a0l");p.Emit("mov a0l [r1++]");}
    return p;
}
