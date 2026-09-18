#pragma once
#include "program.h"
inline Program makeResampleTap() {
    Program p;
    p.Emit("clr a1 always");
    for(unsigned i=0;i<4;++i) {
        p.Emit("mov [r1++] y0");p.Emit("mpy y0 [r0++] a0");
        p.Emit("mov p* a0");p.Emit("add 0x0000 a0",0x4000);
        p.Emit("shfi a0 a0 -0x000f");p.Emit("add a0 a1");
    }
    p.Emit("shfi a1 a1 +0x0010");p.Emit("mov a1h [r2++]");
    return p;
}
// Complete sample loop: r0 input, r2 output, r3 table base, r4 phase16,
// r5 step low16, r7 step high16 (0 or1), r6 positive sample count minus1.
// Returns updated r0/r2/r4. Caller handles N64 history/state memory semantics.
inline Program makeResampleBuffer() {
    Program p;
    p.Emit("bkrep r6 0x00000000");
    p.Emit("clr b0 always");p.Emit("mov r0 b0l");
    p.Emit("clr a0 always");p.Emit("mov r4 a0l");
    p.Emit("shfi a0 a0 -0x0008");p.Emit("and 0x0000 a0",0xfc);
    p.Emit("add r3 a0");p.Emit("mov a0l r1");
    auto taps=makeResampleTap();
    p.words.insert(p.words.end(),taps.words.begin(),taps.words.end());p.instructions+=taps.instructions;
    p.Emit("clr a0 always");p.Emit("mov r4 a0l");
    p.Emit("clr a1 always");p.Emit("mov r5 a1l");p.Emit("add a1 a0");
    p.Emit("clr a1 always");p.Emit("mov r7 a1l");p.Emit("shfi a1 a1 +0x0010");p.Emit("add a1 a0");
    p.Emit("mov a0l r4");p.Emit("shfi a0 a0 -0x0010");p.Emit("add b0 a0");
    p.Emit("mov a0l r0");
    p.words[1]=p.words.size()-1;
    p.relocations.push_back(1);
    return p;
}
