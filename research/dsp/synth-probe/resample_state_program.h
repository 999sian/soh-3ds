#pragma once
#include <map>
#include "resample_program.h"
// Isolated full-state kernel. Caller validates commands before entry.
// Data words20=count (>0),21=flags (0,1,2),22=pitch; state3000; table4000.
// Input400/output1000. Valid saved adjustment is zero or -9..-15.
inline Program makeResampleState(unsigned stateBase=0x3000) {
    Program p;std::map<std::string,unsigned> labels;
    std::vector<std::pair<unsigned,std::string>> jumps;
    auto label=[&](const char* n){labels[n]=p.words.size();};
    auto jump=[&](const char* n,const char* c="always"){p.Emit(std::string("br 0x00000000 ")+c);jumps.emplace_back(p.words.size()-1,n);};
    auto copy=[&](unsigned n){for(unsigned i=0;i<n;++i){p.Emit("mov [r1++] a0l");p.Emit("mov a0l [r0++]");}};
    p.Emit("load 0x0000u8 page");p.Emit("mov [page:0x0021u8] b0l");
    p.Emit("cmpv 0x0000 b0l",1);jump("init","eq");
    p.Emit("mov 0x0000 r0",0x400);
    p.Emit("cmpv 0x0000 b0l",2);jump("normal","neq");
    p.Emit("mov 0x0000 r1",stateBase+8);p.Emit("mov 0x0000 r0",0x3f8);copy(8);
    p.Emit("mov 0x0000 r1",stateBase+5);p.Emit("mov [r1] a0");
    // Match the production unsigned sizeof division and wrapped byte address:
    // an odd negative adjustment advances ceil(-adjustment/2) samples.
    p.Emit("shfi a0 a0 -0x0001");
    p.Emit("clr a1 always");p.Emit("mov 0x0000 a1l",0x400);
    p.Emit("sub a0 a1");p.Emit("mov a1l r0");
    label("normal");
    p.Emit("clr a0 always");p.Emit("mov r0 a0l");p.Emit("sub 0x0000 a0",4);p.Emit("mov a0l r0");
    p.Emit("mov r0 b1l");p.Emit("mov 0x0000 r1",stateBase);copy(4);p.Emit("mov b1l r0");
    p.Emit("mov [r1] r4");jump("run");
    label("init");p.Emit("mov 0x0000 r0",0x3fc);p.Emit("clr a0 always");
    for(unsigned i=0;i<4;++i)p.Emit("mov a0l [r0++]");
    p.Emit("mov 0x0000 r0",0x3fc);p.Emit("mov 0x0000 r4",0);
    label("run");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0022u8] a0l");p.Emit("shfi a0 a0 +0x0001");
    p.Emit("mov a0l r5");p.Emit("shfi a0 a0 -0x0010");p.Emit("mov a0l r7");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0020u8] a0l");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");
    p.Emit("mov 0x0000 r2",0x1000);p.Emit("mov 0x0000 r3",0x4000);
    auto kernel=makeResampleBuffer();kernel.words[1]+=p.words.size();
    p.relocations.push_back(p.words.size()+1);
    p.words.insert(p.words.end(),kernel.words.begin(),kernel.words.end());
    p.Emit("mov 0x0000 r1",stateBase+4);p.Emit("mov r4 [r1]");
    p.Emit("mov r0 b1l");p.Emit("mov 0x0000 r1",stateBase);
    for(unsigned i=0;i<4;++i){p.Emit("mov [r0++] a0l");p.Emit("mov a0l [r1++]");}
    p.Emit("mov b1l r0");
    p.Emit("clr a0 always");p.Emit("mov r0 a0l");p.Emit("add 0x0000 a0",4);p.Emit("and 0x0000 a0",7);
    p.Emit("clr a1 always");p.Emit("mov r0 a1l");p.Emit("sub a0 a1");p.Emit("mov a1l r0");
    p.Emit("cmpv 0x0000 a0l",0);jump("save_adjustment","eq");
    p.Emit("clr a1 always");p.Emit("sub 0x0000 a1",8);p.Emit("sub a0 a1");p.Emit("mov a1l a0l");
    label("save_adjustment");p.Emit("mov 0x0000 r1",stateBase+5);p.Emit("mov a0l [r1]");
    p.Emit("mov 0x0000 r1",stateBase+8);
    for(unsigned i=0;i<8;++i){p.Emit("mov [r0++] a0l");p.Emit("mov a0l [r1++]");}
    p.Emit("mov 0x0000 a0l",1);p.Emit("mov a0l [page:0x0023u8]");
    label("done");jump("done");
    for(auto& fix:jumps){p.words[fix.first]=labels.at(fix.second);p.relocations.push_back(fix.first);}
    return p;
}
