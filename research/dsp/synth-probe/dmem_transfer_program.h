#pragma once
#include <map>
#include <cstdio>
#include "dmem_layout.h"
#include "program.h"
// Resident audio DMEM: 0xc00 bytes at DSP word5000, matching mixer.c's
// [N64 0x3c0,0xfc0) byte buffer. Parameters50=source byte offset,
// 51=destination byte offset,52=exact bytes,54=0(move)/1(clear). Status53=0/3.
// Payload mode only permits move, sourcing the disjoint immutable bank.
// All extents are checked before writes. Host translates operation-specific
// rounding; this primitive also preserves odd-byte endpoints and memmove aliasing.
inline Program makeDmemTransfer(bool payload=false){
    Program p;std::map<std::string,unsigned> labels;
    std::vector<std::pair<unsigned,std::string>> jumps;
    auto label=[&](std::string n){labels[n]=p.words.size();};
    auto jump=[&](std::string n,const char* c="always"){
        p.Emit(std::string("br 0x00000000 ")+c);jumps.emplace_back(p.words.size()-1,n);
    };
    p.Emit("mov 0x0000 mod0",2);p.Emit("mov 0x0000 mod1",0);p.Emit("mov 0x0000 mod2",0);p.Emit("mov 0x0000 mod3",0);
    p.Emit("load 0x0000u8 page");
    p.Emit("mov [page:0x0054u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("op_ok","eq");
    if(payload)jump("invalid");else{p.Emit("cmpv 0x0000 b0l",1);jump("invalid","neq");}label("op_ok");
    // Zero-extend every public u16 before arithmetic, including 0xffff.
    for(unsigned param:{0x52u,0x51u,0x50u}){
        if(param==0x50){p.Emit("mov [page:0x0054u8] b0l");p.Emit("cmpv 0x0000 b0l",1);jump("bounds_done","eq");}
        char op[80];std::snprintf(op,sizeof(op),"mov [page:0x%04xu8] a0l",param);
        p.Emit("clr a0 always");p.Emit(op);p.Emit("sub 0x0000 a0",payload && param==0x50?ResidentDsp::PayloadBytes:ResidentDsp::AudioDmemBytes);jump("invalid","gt");
        if(param!=0x52){
            p.Emit("clr a1 always");p.Emit("mov [page:0x0052u8] a1l");p.Emit("add a1 a0");jump("invalid","gt");
        }
    }
    label("bounds_done");p.Emit("mov [page:0x0052u8] a0l");p.Emit("mov a0l r6");p.Emit("cmpv 0x0000 a0l",0);jump("success","eq");
    p.Emit("mov [page:0x0050u8] a0l");p.Emit("mov a0l r0");p.Emit("mov [page:0x0051u8] a0l");p.Emit("mov a0l r1");
    // Aligned even transfers use a word loop, keeping the common full-buffer
    // clear/move inexpensive. Odd endpoints retain the exact byte path below.
    p.Emit("mov r6 a0");p.Emit("and 0x0000 a0",1);jump("byte_path","neq");
    p.Emit("mov r1 a0");p.Emit("and 0x0000 a0",1);jump("byte_path","neq");
    p.Emit("mov [page:0x0054u8] b0l");p.Emit("cmpv 0x0000 b0l",1);jump("word_path","eq");
    p.Emit("mov r0 a0");p.Emit("and 0x0000 a0",1);jump("word_path","eq");
    label("byte_path");
    p.Emit("mov [page:0x0054u8] b0l");p.Emit("cmpv 0x0000 b0l",1);jump("clear","eq");
    p.Emit("clr a0 always");p.Emit("mov r1 a0l");p.Emit("clr a1 always");p.Emit("mov r0 a1l");p.Emit("sub a1 a0");jump("forward","le");
    for(const char* reg:{"r0","r1"}){
        p.Emit("clr a0 always");p.Emit(std::string("mov ")+reg+" a0l");p.Emit("clr a1 always");p.Emit("mov r6 a1l");
        p.Emit("add a1 a0");p.Emit("sub 0x0000 a0",1);p.Emit(std::string("mov a0l ")+reg);
    }
    jump("backward");
    for(const std::string mode:{"forward","backward","clear"}){
        label(mode);
        if(mode=="clear"){p.Emit("mov 0x0000 r4",0);}else{
            p.Emit("clr a0 always");p.Emit("mov r0 a0l");p.Emit("mov a0 a1");p.Emit("and 0x0000 a1",1);p.Emit("mov a1l r7");
            p.Emit("shfi a0 a0 -0x0001");p.Emit("add 0x0000 a0",payload?ResidentDsp::PayloadWordBase:ResidentDsp::AudioDmemWordBase);p.Emit("mov a0l r2");p.Emit("mov [r2] a0");
            p.Emit("mov r7 a1");p.Emit("cmpv 0x0000 a1l",0);jump(mode+"_read","eq");p.Emit("shfi a0 a0 -0x0008");
            label(mode+"_read");p.Emit("and 0x0000 a0",255);p.Emit("mov a0l r4");
        }
        p.Emit("clr a0 always");p.Emit("mov r1 a0l");p.Emit("mov a0 a1");p.Emit("and 0x0000 a1",1);p.Emit("mov a1l r7");
        p.Emit("shfi a0 a0 -0x0001");p.Emit("add 0x0000 a0",ResidentDsp::AudioDmemWordBase);p.Emit("mov a0l r2");p.Emit("mov [r2] a0");
        p.Emit("mov r7 a1");p.Emit("cmpv 0x0000 a1l",0);jump(mode+"_high","neq");
        p.Emit("and 0x0000 a0",0xff00);p.Emit("mov r4 a1");p.Emit("or a1 a0");jump(mode+"_store");
        label(mode+"_high");p.Emit("and 0x0000 a0",255);p.Emit("mov r4 a1");p.Emit("shfi a1 a1 +0x0008");p.Emit("or a1 a0");
        label(mode+"_store");p.Emit("mov a0l [r2]");
        for(const char* reg:{"r0","r1"}){
            p.Emit("clr a0 always");p.Emit(std::string("mov ")+reg+" a0l");p.Emit(mode=="backward"?"sub 0x0000 a0":"add 0x0000 a0",1);p.Emit(std::string("mov a0l ")+reg);
        }
        p.Emit("mov r6 a0");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");jump(mode,"neq");jump("success");
    }
    label("word_path");
    for(const char* reg:{"r0","r1"}){
        p.Emit("clr a0 always");p.Emit(std::string("mov ")+reg+" a0l");
        p.Emit("shfi a0 a0 -0x0001");p.Emit("add 0x0000 a0",payload && std::string(reg)=="r0"?ResidentDsp::PayloadWordBase:ResidentDsp::AudioDmemWordBase);p.Emit(std::string("mov a0l ")+reg);
    }
    p.Emit("mov r6 a0");p.Emit("shfi a0 a0 -0x0001");p.Emit("mov a0l r6");
    p.Emit("mov [page:0x0054u8] b0l");p.Emit("cmpv 0x0000 b0l",1);jump("word_clear","eq");
    p.Emit("mov r1 a0");p.Emit("mov r0 a1");p.Emit("sub a1 a0");jump("word_forward","le");
    for(const char* reg:{"r0","r1"}){
        p.Emit(std::string("mov ")+reg+" a0");p.Emit("mov r6 a1");p.Emit("add a1 a0");p.Emit("sub 0x0000 a0",1);p.Emit(std::string("mov a0l ")+reg);
    }
    jump("word_backward");
    for(const std::string mode:{"word_forward","word_backward","word_clear"}){
        label(mode);
        if(mode=="word_clear")p.Emit("clr a0 always");
        else p.Emit(mode=="word_backward"?"mov [r0--] a0l":"mov [r0++] a0l");
        p.Emit(mode=="word_backward"?"mov a0l [r1--]":"mov a0l [r1++]");
        p.Emit("mov r6 a0");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");jump(mode,"neq");jump("success");
    }
    label("invalid");p.Emit("mov 0x0000 b0l",3);jump("complete");label("success");p.Emit("clr b0 always");
    label("complete");p.Emit("mov b0l [page:0x0053u8]");label("done");jump("done");
    for(auto& fix:jumps){p.words[fix.first]=labels.at(fix.second);p.relocations.push_back(fix.first);}
    return p;
}
