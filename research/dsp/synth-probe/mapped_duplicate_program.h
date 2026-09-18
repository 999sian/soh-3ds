#pragma once
#include "mapped_arithmetic_program.h"
// Count is copies (1..24), record0/1 exact byte offsets. Snapshot all128 input
// bytes before any output, matching production even across repeated overlaps.
// Scratch3e00..3e7f stores one byte per word; result61. All bounds precede writes.
inline Program makeMappedDuplicate(){
    Program p;std::map<std::string,unsigned> labels;
    std::vector<std::pair<unsigned,std::string>> jumps;
    auto label=[&](std::string n){labels[n]=p.words.size();};
    auto jump=[&](std::string n,const char* c="always"){p.Emit(std::string("br 0x00000000 ")+c);jumps.emplace_back(p.words.size()-1,n);};
    auto increment=[&](const char* r){p.Emit(std::string("mov ")+r+" a0");p.Emit("add 0x0000 a0",1);p.Emit(std::string("mov a0l ")+r);};
    auto decrement=[&](const char* r){p.Emit(std::string("mov ")+r+" a0");p.Emit("sub 0x0000 a0",1);p.Emit(std::string("mov a0l ")+r);};
    p.Emit("load 0x0000u8 page");
    p.Emit("mov [page:0x0006u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("invalid","neq");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0002u8] a0l");p.Emit("sub 0x0000 a0",63);jump("invalid","gt");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0001u8] a0l");p.Emit("sub 0x0000 a0",24);jump("invalid","gt");
    p.Emit("mov [page:0x0001u8] a0l");p.Emit("cmpv 0x0000 a0l",0);jump("invalid","eq");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0002u8] a0l");p.Emit("shfi a0 a0 +0x0004");
    p.Emit("add 0x0000 a0",ResidentDsp::ParameterWordBase);p.Emit("mov a0l r5");
    for(unsigned i=0;i<2;++i){
        p.Emit("clr a0 always");p.Emit("mov [r5++] a0l");p.Emit("sub 0x0000 a0",ResidentDsp::AudioDmemBytes);jump("invalid","gt");
        p.Emit("clr a1 always");
        if(i){p.Emit("mov [page:0x0001u8] a1l");p.Emit("shfi a1 a1 +0x0007");}
        else p.Emit("mov 0x0000 a1l",128);
        p.Emit("add a1 a0");jump("invalid","gt");p.Emit("sub a1 a0");p.Emit("add 0x0000 a0",ResidentDsp::AudioDmemBytes);
        p.Emit(i?"mov a0l r1":"mov a0l r0");
    }
    p.Emit("mov [page:0x0001u8] r5");p.Emit("mov 0x0000 r3",0x3e00);p.Emit("mov 0x0000 r6",128);
    label("snapshot");
    p.Emit("mov r0 a0");p.Emit("mov a0 a1");p.Emit("and 0x0000 a1",1);p.Emit("mov a1l r7");
    p.Emit("shfi a0 a0 -0x0001");p.Emit("add 0x0000 a0",ResidentDsp::AudioDmemWordBase);p.Emit("mov a0l r2");p.Emit("mov [r2] a0");
    p.Emit("mov r7 a1");p.Emit("cmpv 0x0000 a1l",0);jump("snapshot_low","eq");p.Emit("shfi a0 a0 -0x0008");
    label("snapshot_low");p.Emit("and 0x0000 a0",255);p.Emit("mov a0l [r3++]");
    increment("r0");decrement("r6");jump("snapshot","neq");
    label("copy");p.Emit("mov 0x0000 r3",0x3e00);p.Emit("mov 0x0000 r6",128);
    label("byte");p.Emit("mov [r3++] r4");
    p.Emit("mov r1 a0");p.Emit("mov a0 a1");p.Emit("and 0x0000 a1",1);p.Emit("mov a1l r7");
    p.Emit("shfi a0 a0 -0x0001");p.Emit("add 0x0000 a0",ResidentDsp::AudioDmemWordBase);p.Emit("mov a0l r2");p.Emit("mov [r2] a0");
    p.Emit("mov r7 a1");p.Emit("cmpv 0x0000 a1l",0);jump("high","neq");
    p.Emit("and 0x0000 a0",0xff00);p.Emit("mov r4 a1");p.Emit("or a1 a0");jump("store");
    label("high");p.Emit("and 0x0000 a0",255);p.Emit("mov r4 a1");p.Emit("shfi a1 a1 +0x0008");p.Emit("or a1 a0");
    label("store");p.Emit("mov a0l [r2]");increment("r1");decrement("r6");jump("byte","neq");
    decrement("r5");jump("copy","neq");
    p.Emit("clr b0 always");jump("complete");label("invalid");p.Emit("mov 0x0000 b0l",3);
    label("complete");p.Emit("mov b0l [page:0x0061u8]");label("done");jump("done");
    for(auto& fix:jumps){p.words[fix.first]=labels.at(fix.second);p.relocations.push_back(fix.first);}return p;
}
