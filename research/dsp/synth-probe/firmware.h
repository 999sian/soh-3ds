#pragma once
#include <map>
#include "program.h"
// Fixed DSP word addresses: request 0x00, reply 0x10, input 0x1000, output 0x1400.
// Only one job in flight. Host flushes request last and never writes it until
// firmware clears busy. Reply sequence/status are written before busy clears.
inline Program MakeFirmware() {
    Program p;
    std::map<std::string,unsigned> labels;
    std::vector<std::pair<unsigned,std::string>> fixups;
    auto label=[&](const char* name){labels[name]=p.words.size();};
    auto branch=[&](const char* name,const char* condition="always") {
        p.Emit(std::string("br 0x00000000 ")+condition);
        fixups.emplace_back(p.words.size()-1,name);
    };
    branch("boot");
    // Unused interrupt vectors remain disabled; reset branches past them.
    p.words.resize(0x100,0);
    label("boot");
    p.Emit("mov 0x0000 mod0",2); // signed arithmetic, saturation on high-word store, product shift 0
    p.Emit("mov 0x0000 mod1",0);
    p.Emit("mov 0x0000 mod2",0); // linear addressing, no modulo/bit reversal
    p.Emit("mov 0x0000 mod3",0); // interrupts disabled
    p.Emit("load 0x0080u8 page");
    p.Emit("clr b0 always");
    p.Emit("mov b0l [page:0x00c8u8]"); // T_REPLY2: loader pipe-base reply (unused by this probe)
    p.Emit("load 0x0000u8 page");
    p.Emit("mov b0l [page:0x0000u8]");
    p.Emit("mov b0l [page:0x0011u8]");
    p.Emit("mov b0l [page:0x0012u8]");
    p.Emit("mov 0x0000 b0l",0x4453);
    p.Emit("mov b0l [page:0x0010u8]");
    label("idle");
    p.Emit("load 0x0080u8 page");
    p.Emit("mov [page:0x00d8u8] a0l");
    p.Emit("and 0x0000 a0",0x8000); // CPU command2 ready bit
    branch("request","eq");
    p.Emit("mov [page:0x00cau8] a0l");
    p.Emit("cmpv 0x0000 a0l",0x8000);
    branch("shutdown","eq");
    label("request");
    p.Emit("load 0x0000u8 page");
    p.Emit("mov [page:0x0000u8] b0l");branch("idle","eq");
    p.Emit("mov [page:0x0004u8] b0l");
    for(unsigned batch:{1,2,8,32}) {
        p.Emit("cmpv 0x0000 b0l",batch);branch("batch_valid","eq");
    }
    p.Emit("mov 0x0000 b0l",2);branch("complete");
    label("batch_valid");
    p.Emit("mov b0l r5");
    p.Emit("mov [page:0x0001u8] b0l");
    p.Emit("cmpv 0x0000 b0l",0);branch("success","eq");
    for(unsigned n:{16,32,64,128,192,256,512}) {
        p.Emit("cmpv 0x0000 b0l",n);branch("mix","eq");
    }
    p.Emit("mov 0x0000 b0l",1);branch("complete");
    label("mix");
    p.Emit("clr a0 always");
    p.Emit("mov [page:0x0001u8] a0l");
    p.Emit("sub 0x0000 a0",1);
    p.Emit("mov a0l r6");
    p.Emit("mov 0x0000 r0",0x1000);
    p.Emit("mov 0x0000 r1",0x1400);
    p.Emit("mov [page:0x0002u8] r2");
    p.Emit("mov r2 b0l");p.Emit("cmpv 0x0000 b0l",0x8000);branch("subtract","eq");
    auto kernel=[&](bool sub){
        auto k=MakeProgram(sub,true);
        unsigned base=p.words.size(); k.words[1]+=base;
        p.words.insert(p.words.end(),k.words.begin(),k.words.end());
    };
    kernel(false);branch("batch_end");
    label("subtract");kernel(true);
    label("batch_end");
    p.Emit("clr a0 always");p.Emit("mov r5 a0l");
    p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r5");
    p.Emit("cmpv 0x0000 a0l",0);branch("mix","neq");
    label("success");p.Emit("clr b0 always");
    label("complete");
    p.Emit("mov b0l [page:0x0012u8]");
    p.Emit("mov [page:0x0003u8] b0l");
    p.Emit("mov b0l [page:0x0011u8]");
    p.Emit("clr b0 always");
    p.Emit("mov b0l [page:0x0000u8]");branch("idle");
    label("shutdown");
    p.Emit("clr b0 always");
    p.Emit("mov b0l [page:0x00c8u8]");
    label("stopped");branch("stopped");
    for(auto& fix:fixups)p.words[fix.first]=labels.at(fix.second);
    return p;
}
