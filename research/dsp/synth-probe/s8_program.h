#pragma once
#include "program.h"
#include <map>
// Called only after mapped preflight. Private1=samples,25=flags,26=output,
// 2a=state,2b=loop,2c=source BYTE address. Status27. No additional scratch.
inline Program makeS8(){
    Program p;std::map<std::string,unsigned> labels;std::vector<std::pair<unsigned,std::string>> jumps;
    auto label=[&](std::string n){labels[n]=p.words.size();};
    auto jump=[&](std::string n,const char* c="always"){p.Emit(std::string("br 0x00000000 ")+c);jumps.emplace_back(p.words.size()-1,n);};
    p.Emit("load 0x0000u8 page");p.Emit("mov [page:0x0026u8] r3");p.Emit("mov [page:0x0025u8] b0l");
    p.Emit("cmpv 0x0000 b0l",1);jump("init","eq");p.Emit("mov [page:0x002au8] r1");
    p.Emit("cmpv 0x0000 b0l",2);jump("history","neq");p.Emit("mov [page:0x002bu8] r1");
    label("history");for(unsigned i=0;i<16;++i){p.Emit("mov [r1++] a0l");p.Emit("mov a0l [r3++]");}jump("start");
    label("init");p.Emit("clr a0 always");for(unsigned i=0;i<16;++i)p.Emit("mov a0l [r3++]");
    label("start");p.Emit("mov [page:0x002cu8] r5");p.Emit("mov [page:0x0001u8] a0l");p.Emit("mov a0l r6");
    p.Emit("cmpv 0x0000 a0l",0);jump("save","eq");
    label("sample");
    p.Emit("clr a0 always");p.Emit("mov r5 a0l");p.Emit("mov a0 a1");p.Emit("and 0x0000 a1",1);p.Emit("mov a1l r7");
    p.Emit("shfi a0 a0 -0x0001");p.Emit("mov a0l r0");p.Emit("mov [r0] a0");p.Emit("mov r7 a1");p.Emit("cmpv 0x0000 a1l",0);jump("low","eq");p.Emit("shfi a0 a0 -0x0008");
    label("low");p.Emit("and 0x0000 a0",255);p.Emit("shfi a0 a0 +0x0008");p.Emit("mov a0l [r3++]");
    p.Emit("modr [r5++]");p.Emit("mov r6 a0");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");jump("sample","neq");
    label("save");p.Emit("mov r3 a0");p.Emit("sub 0x0000 a0",16);p.Emit("mov a0l r0");p.Emit("mov [page:0x002au8] r1");
    for(unsigned i=0;i<16;++i){p.Emit("mov [r0++] a0l");p.Emit("mov a0l [r1++]");}
    p.Emit("clr b0 always");p.Emit("mov b0l [page:0x0027u8]");label("done");jump("done");
    for(auto& fix:jumps){p.words[fix.first]=labels.at(fix.second);p.relocations.push_back(fix.first);}return p;
}
