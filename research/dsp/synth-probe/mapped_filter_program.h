#pragma once
#include <map>
#include "mapped_parameters.h"
#include "filter_program.h"
// Setup: count normalized samples incl0, record0..7 coefficients.
// Process: count0, record0=input offset,1=state slot,2=init flag0/1.
// Global coefficients7400..7407 mutate after each process, count7408 persists.
inline Program makeMappedFilter(bool setup){
    Program p;std::map<std::string,unsigned> labels;std::vector<std::pair<unsigned,std::string>> jumps;
    auto label=[&](std::string n){labels[n]=p.words.size();};
    auto jump=[&](std::string n,const char* c="always"){p.Emit(std::string("br 0x00000000 ")+c);jumps.emplace_back(p.words.size()-1,n);};
    p.Emit("load 0x0000u8 page");p.Emit("mov [page:0x0006u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("invalid","neq");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0002u8] a0l");p.Emit("sub 0x0000 a0",63);jump("invalid","gt");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0002u8] a0l");p.Emit("shfi a0 a0 +0x0004");p.Emit("add 0x0000 a0",ResidentDsp::ParameterWordBase);p.Emit("mov a0l r5");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0001u8] a0l");
    if(!setup){
        p.Emit("cmpv 0x0000 a0l",0);jump("invalid","neq");
        p.Emit("mov 0x0000 r1",ResidentDsp::FilterCountWord);p.Emit("mov [r1] a0l");
    }
    p.Emit("sub 0x0000 a0",1536);jump("invalid","gt");
    p.Emit("add 0x0000 a0",1536);p.Emit("mov a0l r6");p.Emit("and 0x0000 a0",7);jump("invalid","neq");
    if(setup){
        p.Emit("mov 0x0000 r1",ResidentDsp::FilterCoefficientWordBase);
        for(unsigned i=0;i<8;++i){p.Emit("mov [r5++] a0l");p.Emit("mov a0l [r1++]");}
        p.Emit("mov r6 [r1]");jump("success");
    }else{
        p.Emit("mov r6 a0");p.Emit("cmpv 0x0000 a0l",0);jump("count_ready","neq");p.Emit("mov 0x0000 r6",8);label("count_ready");
        p.Emit("clr a0 always");p.Emit("mov [r5++] a0l");p.Emit("sub 0x0000 a0",1536);jump("invalid","gt");
        p.Emit("mov r6 a1");p.Emit("add a1 a0");jump("invalid","gt");p.Emit("sub a1 a0");p.Emit("add 0x0000 a0",1536+ResidentDsp::AudioDmemWordBase);p.Emit("mov a0l r0");
        p.Emit("clr a0 always");p.Emit("mov [r5++] a0l");p.Emit("sub 0x0000 a0",63);jump("invalid","gt");p.Emit("add 0x0000 a0",63);p.Emit("shfi a0 a0 +0x0004");p.Emit("add 0x0000 a0",ResidentDsp::FilterStateWordBase);p.Emit("mov a0l r4");
        p.Emit("clr a0 always");p.Emit("mov [r5] a0l");p.Emit("sub 0x0000 a0",1);jump("invalid","gt");p.Emit("mov [r5] b0l");
        p.Emit("mov r6 a0");p.Emit("shfi a0 a0 -0x0003");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");
        p.Emit("cmpv 0x0000 b0l",1);jump("init","eq");
        for(bool init:{false,true}){
            if(init)label("init");auto k=makeFilter(init,true);k.words.resize(k.words.size()-2);
            for(auto fix:k.relocations)if(fix<k.words.size()){k.words[fix]+=p.words.size();p.relocations.push_back(p.words.size()+fix);}
            p.words.insert(p.words.end(),k.words.begin(),k.words.end());jump("success");
        }
    }
    label("invalid");p.Emit("mov 0x0000 b0l",3);jump("complete");label("success");p.Emit("clr b0 always");
    label("complete");p.Emit("mov b0l [page:0x0061u8]");label("done");jump("done");
    for(auto& fix:jumps){p.words[fix.first]=labels.at(fix.second);p.relocations.push_back(fix.first);}return p;
}
