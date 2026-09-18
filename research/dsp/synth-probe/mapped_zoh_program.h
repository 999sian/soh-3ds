#pragma once
#include "mapped_arithmetic_program.h"
// Count: positive multiple4 output samples. Record: input/output sample offsets,
// unsigned pitch, initial fractional position. No persistent state or scratch.
inline Program makeMappedZoh(){
    Program p;std::map<std::string,unsigned> labels;
    std::vector<std::pair<unsigned,std::string>> jumps;
    auto label=[&](std::string n){labels[n]=p.words.size();};
    auto jump=[&](std::string n,const char* c="always"){p.Emit(std::string("br 0x00000000 ")+c);jumps.emplace_back(p.words.size()-1,n);};
    p.Emit("load 0x0000u8 page");
    p.Emit("mov [page:0x0006u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("invalid","neq");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0002u8] a0l");p.Emit("sub 0x0000 a0",63);jump("invalid","gt");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0001u8] a0l");p.Emit("sub 0x0000 a0",1536);jump("invalid","gt");
    p.Emit("mov [page:0x0001u8] a0l");p.Emit("and 0x0000 a0",3);jump("invalid","neq");
    p.Emit("mov [page:0x0001u8] a0l");p.Emit("cmpv 0x0000 a0l",0);jump("invalid","eq");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0002u8] a0l");p.Emit("shfi a0 a0 +0x0004");
    p.Emit("add 0x0000 a0",ResidentDsp::ParameterWordBase);p.Emit("mov a0l r5");
    // Both offsets first, with the complete output extent. No writes before
    // checking the furthest sampled input address at phase+(count-1)*pitch*4.
    for(unsigned i=0;i<2;++i){
        p.Emit("clr a0 always");p.Emit("mov [r5++] a0l");p.Emit("sub 0x0000 a0",1536);jump("invalid","gt");
        if(i){p.Emit("clr a1 always");p.Emit("mov [page:0x0001u8] a1l");p.Emit("add a1 a0");jump("invalid","gt");p.Emit("sub a1 a0");}
        p.Emit("add 0x0000 a0",1536+ResidentDsp::AudioDmemWordBase);
        p.Emit(i?"mov a0l r1":"mov a0l r0");
    }
    p.Emit("clr a0 always");p.Emit("mov [page:0x0001u8] a0l");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");p.Emit("mov a0l y0");
    p.Emit("mpysu y0 [r5] a0");p.Emit("mov p* a0");p.Emit("shfi a0 a0 +0x0002");
    p.Emit("clr b1 always");p.Emit("mov [r5++] b1l");p.Emit("shfi b1 b1 +0x0002");
    p.Emit("clr b0 always");p.Emit("mov [r5] b0l");p.Emit("mov b0 a1");p.Emit("add a1 a0");p.Emit("shfi a0 a0 -0x0011");
    p.Emit("clr a1 always");p.Emit("mov r0 a1l");p.Emit("add a1 a0");
    p.Emit("sub 0x0000 a0",ResidentDsp::AudioDmemWordBase+1535);jump("invalid","gt");
    // Position never wraps uint32 for validated counts/pitch. Preserve forward
    // read/store ordering even when outputs overlap future sampled inputs.
    p.Emit("bkrep r6 0x00000000");unsigned repeat=p.words.size()-1;
    p.Emit("mov b0 a0");p.Emit("shfi a0 a0 -0x0011");
    p.Emit("clr a1 always");p.Emit("mov r0 a1l");p.Emit("add a1 a0");p.Emit("mov a0l r2");
    p.Emit("mov [r2] a0l");p.Emit("mov a0l [r1++]");
    p.Emit("mov b1 a1");p.Emit("add a1 b0");
    p.words[repeat]=p.words.size()-1;p.relocations.push_back(repeat);
    p.Emit("clr b0 always");jump("complete");label("invalid");p.Emit("mov 0x0000 b0l",3);
    label("complete");p.Emit("mov b0l [page:0x0061u8]");label("done");jump("done");
    for(auto& fix:jumps){p.words[fix.first]=labels.at(fix.second);p.relocations.push_back(fix.first);}return p;
}
