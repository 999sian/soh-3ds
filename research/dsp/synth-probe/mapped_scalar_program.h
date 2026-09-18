#pragma once
#include "mapped_arithmetic_program.h"
// HiLo: positive sample count multiple8, record0=in-place offset,1=gain0..255.
// Table multiply: positive sample count multiple32, record0=table,1=output.
// Table snapshots32 samples at3f00..3f1f before output; status61.
inline Program makeMappedScalar(bool table){
    Program p;std::map<std::string,unsigned> labels;std::vector<std::pair<unsigned,std::string>> jumps;
    auto label=[&](std::string n){labels[n]=p.words.size();};
    auto jump=[&](std::string n,const char* c="always"){p.Emit(std::string("br 0x00000000 ")+c);jumps.emplace_back(p.words.size()-1,n);};
    p.Emit("load 0x0000u8 page");p.Emit("mov [page:0x0006u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("invalid","neq");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0002u8] a0l");p.Emit("sub 0x0000 a0",63);jump("invalid","gt");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0001u8] a0l");p.Emit("sub 0x0000 a0",1536);jump("invalid","gt");
    p.Emit("mov [page:0x0001u8] a0l");p.Emit("and 0x0000 a0",table?31:7);jump("invalid","neq");
    p.Emit("mov [page:0x0001u8] a0l");p.Emit("cmpv 0x0000 a0l",0);jump("invalid","eq");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0002u8] a0l");p.Emit("shfi a0 a0 +0x0004");p.Emit("add 0x0000 a0",ResidentDsp::ParameterWordBase);p.Emit("mov a0l r5");
    for(unsigned i=0;i<(table?2u:1u);++i){
        p.Emit("clr a0 always");p.Emit("mov [r5++] a0l");p.Emit("sub 0x0000 a0",1536);jump("invalid","gt");
        p.Emit("clr a1 always");if(table && i==0)p.Emit("mov 0x0000 a1l",32);else p.Emit("mov [page:0x0001u8] a1l");
        p.Emit("add a1 a0");jump("invalid","gt");p.Emit("sub a1 a0");p.Emit("add 0x0000 a0",1536+ResidentDsp::AudioDmemWordBase);p.Emit(i?"mov a0l r1":"mov a0l r0");
    }
    if(table){
        p.Emit("mov 0x0000 r2",0x3f00);for(unsigned i=0;i<32;++i){p.Emit("mov [r0++] a0l");p.Emit("mov a0l [r2++]");}
        p.Emit("mov 0x0000 r0",0x3f00);
    }else{
        p.Emit("clr a0 always");p.Emit("mov [r5] a0l");p.Emit("sub 0x0000 a0",255);jump("invalid","gt");p.Emit("mov [r5] y0");
    }
    p.Emit("clr a0 always");p.Emit("mov [page:0x0001u8] a0l");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");
    p.Emit("bkrep r6 0x00000000");unsigned repeat=p.words.size()-1;
    if(table){
        p.Emit("mov [r0++] y0");p.Emit("mpy y0 [r1] a0");p.Emit("mov p* a0");p.Emit("mov a0 b0");
        // A full32-bit product cannot be shifted16 in DSP40 without overflow.
        p.Emit("sub 0x0000 a0",32767);jump("positive","gt");p.Emit("mov b0 a0");p.Emit("sub 0x0000 a0",0x8000);jump("negative","lt");
        p.Emit("mov b0 a0");jump("store");label("positive");p.Emit("mov 0x0000 a0l",32767);jump("store");label("negative");p.Emit("mov 0x0000 a0l",0x8000);
        label("store");p.Emit("mov a0l [r1++]");p.Emit("mov r0 a0");p.Emit("cmpv 0x0000 a0l",0x3f20);jump("next","neq");p.Emit("mov 0x0000 r0",0x3f00);label("next");p.Emit("nop");
    }else{
        p.Emit("mpy y0 [r0] a0");p.Emit("mov p* a0");p.Emit("shfi a0 a0 +0x000c");p.Emit("mov a0h [r0++]");
    }
    p.words[repeat]=p.words.size()-1;p.relocations.push_back(repeat);jump("success");
    label("invalid");p.Emit("mov 0x0000 b0l",3);jump("complete");label("success");p.Emit("clr b0 always");
    label("complete");p.Emit("mov b0l [page:0x0061u8]");label("done");jump("done");
    for(auto& fix:jumps){p.words[fix.first]=labels.at(fix.second);p.relocations.push_back(fix.first);}return p;
}
