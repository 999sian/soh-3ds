#pragma once
#include <map>
#include <cstdio>
#include "program.h"
// Runtime block decoder. Parameters24=blocks(0..32),25=flags(0/1/2),
// completion27=0(success)/3(invalid). Data layout matches adpcm_program.h.
// Validate all headers before mutating PCM/state, including odd-byte headers.
// r3 output cursor, r5 packed BYTE cursor, r6 blocks remaining, r4 shift factor.
inline Program makeDynamicAdpcm(bool twoBit,bool mapped=false){
    Program p;std::map<std::string,unsigned> labels;
    std::vector<std::pair<unsigned,std::string>> jumps;
    auto label=[&](const std::string& name){labels[name]=p.words.size();};
    auto jump=[&](const std::string& name,const char* condition="always"){
        p.Emit(std::string("br 0x00000000 ")+condition);jumps.emplace_back(p.words.size()-1,name);
    };
    auto address=[&](unsigned field,unsigned fixed,const char* reg){
        char op[80];
        if(mapped){std::snprintf(op,sizeof(op),"mov [page:0x%04xu8] %s",field,reg);p.Emit(op);}
        else{std::snprintf(op,sizeof(op),"mov 0x0000 %s",reg);p.Emit(op,fixed);}
    };
    unsigned byteLabels=0;
    // Return one unsigned byte in a0. r5 is a byte address, so adding the
    // per-block byte offset before dividing handles odd block boundaries.
    auto readByte=[&](unsigned offset){
        p.Emit("clr a0 always");p.Emit("mov r5 a0l");
        if(offset)p.Emit("add 0x0000 a0",offset);
        p.Emit("mov a0 a1");p.Emit("and 0x0000 a1",1);p.Emit("mov a1l r7");
        p.Emit("shfi a0 a0 -0x0001");p.Emit("mov a0l r0");p.Emit("mov [r0] a0");
        p.Emit("mov r7 a1");p.Emit("cmpv 0x0000 a1l",0);
        std::string done="byte"+std::to_string(byteLabels++);jump(done,"eq");
        p.Emit("shfi a0 a0 -0x0008");label(done);p.Emit("and 0x0000 a0",255);
    };
    auto outputAddress=[&](int offset,const char* reg){
        p.Emit("clr a1 always");p.Emit("mov r3 a1l");
        if(offset>0)p.Emit("add 0x0000 a1",offset);
        if(offset<0)p.Emit("sub 0x0000 a1",-offset);
        p.Emit(std::string("mov a1l ")+reg);
    };
    auto advanceInput=[&](){
        p.Emit("clr a0 always");p.Emit("mov r5 a0l");p.Emit("add 0x0000 a0",twoBit?5:9);p.Emit("mov a0l r5");
    };
    auto decrement=[&](const char* target){
        p.Emit("mov r6 a0");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");jump(target,"neq");
    };
    p.Emit("load 0x0000u8 page");p.Emit("mov [page:0x0025u8] b0l");
    for(unsigned flags:{0u,1u,2u}){p.Emit("cmpv 0x0000 b0l",flags);jump("flags_valid","eq");}jump("invalid");
    label("flags_valid");p.Emit("mov [page:0x0024u8] b0l");
    if(mapped){p.Emit("clr a0 always");p.Emit("mov b0l a0l");p.Emit("sub 0x0000 a0",95);jump("invalid","gt");jump("count_valid");}
    else{for(unsigned blocks=0;blocks<=32;++blocks){p.Emit("cmpv 0x0000 b0l",blocks);jump("count_valid","eq");}jump("invalid");}
    label("count_valid");p.Emit("mov b0l r6");p.Emit("cmpv 0x0000 b0l",0);jump("history","eq");
    address(0x2c,0x1000,"r5");
    label("preflight");readByte(0);p.Emit("and 0x0000 a0",8);jump("invalid","neq");advanceInput();decrement("preflight");
    label("history");address(0x26,0x3000,"r0");p.Emit("mov [page:0x0025u8] b0l");
    p.Emit("cmpv 0x0000 b0l",1);jump("init","eq");
    address(0x2a,0x3800,"r1");p.Emit("cmpv 0x0000 b0l",2);jump("copy_history","neq");address(0x2b,0x3900,"r1");
    label("copy_history");for(unsigned i=0;i<16;++i){p.Emit("mov [r1++] a0l");p.Emit("mov a0l [r0++]");}jump("start");
    label("init");p.Emit("clr a0 always");for(unsigned i=0;i<16;++i)p.Emit("mov a0l [r0++]");
    label("start");
    if(mapped){p.Emit("clr a0 always");p.Emit("mov [page:0x0026u8] a0l");p.Emit("add 0x0000 a0",16);p.Emit("mov a0l r3");}
    else p.Emit("mov 0x0000 r3",0x3010);address(0x2c,0x1000,"r5");
    p.Emit("mov [page:0x0024u8] b0l");p.Emit("mov b0l r6");p.Emit("cmpv 0x0000 b0l",0);jump("save","eq");
    label("block");readByte(0);p.Emit("and 0x0000 a0",15);p.Emit("shfi a0 a0 +0x0004");
    if(mapped){p.Emit("clr a1 always");p.Emit("mov [page:0x002du8] a1l");p.Emit("add a1 a0");}
    else p.Emit("add 0x0000 a0",0x2000);p.Emit("mov a0l r1");p.Emit("mov 0x0000 r0",0x2500);
    for(unsigned i=0;i<16;++i){p.Emit("mov [r1++] a0l");p.Emit("mov a0l [r0++]");}
    readByte(0);p.Emit("shfi a0 a0 -0x0004");p.Emit("add 0x0000 a0",0x2800);p.Emit("mov a0l r4");
    for(unsigned half=0;half<2;++half){
        for(unsigned i=0;i<8;++i){
            unsigned sample=half*8+i,perByte=twoBit?4:2,bits=twoBit?2:4;
            readByte(1+sample/perByte);
            unsigned shift=8-bits-bits*(sample%perByte);
            if(shift)p.Emit("shfi a0 a0 -0x000"+std::to_string(shift));
            p.Emit("and 0x0000 a0",(1u<<bits)-1);p.Emit("xor 0x0000 a0",1u<<(bits-1));p.Emit("sub 0x0000 a0",1u<<(bits-1));
            p.Emit("mov a0l y0");p.Emit("mpy y0 [r4] a0");p.Emit("mov p* a0");p.Emit("mov 0x0000 r0",0x1000+i);p.Emit("mov a0l [r0]");
        }
        for(unsigned j=0;j<8;++j){
            outputAddress(-2,"r0");p.Emit("mov [r0] y0");p.Emit("mov 0x0000 r1",0x2500+j);p.Emit("mpy y0 [r1] a0");p.Emit("mov p* a0");
            outputAddress(-1,"r0");p.Emit("mov [r0] y0");p.Emit("mov 0x0000 r1",0x2508+j);p.Emit("mpy y0 [r1] a1");p.Emit("add p* a0");
            p.Emit("mov 0x0000 r0",0x1000+j);p.Emit("mov [r0] a1");p.Emit("shfi a1 a1 +0x000b");p.Emit("add a1 a0");
            for(unsigned k=0;k<j;++k){
                p.Emit("mov 0x0000 r0",0x1000+k);p.Emit("mov [r0] y0");p.Emit("mov 0x0000 r1",0x2508+j-k-1);p.Emit("mpy y0 [r1] a1");p.Emit("add p* a0");
            }
            p.Emit("shfi a0 a0 +0x0008");p.Emit("shfi a0 a0 -0x0008");p.Emit("shfi a0 a0 +0x0005");
            outputAddress(j,"r2");p.Emit("mov a0h [r2]");
        }
        p.Emit("clr a0 always");p.Emit("mov r3 a0l");p.Emit("add 0x0000 a0",8);p.Emit("mov a0l r3");
    }
    advanceInput();decrement("block");
    label("save");outputAddress(-16,"r0");address(0x2a,0x3800,"r1");
    for(unsigned i=0;i<16;++i){p.Emit("mov [r0++] a0l");p.Emit("mov a0l [r1++]");}
    p.Emit("clr b0 always");jump("complete");label("invalid");p.Emit("mov 0x0000 b0l",3);
    label("complete");p.Emit("mov b0l [page:0x0027u8]");label("done");jump("done");
    for(const auto& fix:jumps){p.words[fix.first]=labels.at(fix.second);p.relocations.push_back(fix.first);}
    return p;
}
