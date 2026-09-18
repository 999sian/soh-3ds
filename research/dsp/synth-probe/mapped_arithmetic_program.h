#pragma once
#include <cstdio>
#include <map>
#include "mapped_parameters.h"
#include "envelope_program.h"
// Dispatcher registers remain private. Public fields1=count samples,2=slot,
// 6=reserved0. Parameter record format is defined in mapped_parameters.h.
// Returns status at61; all validation precedes any PCM/synthesis scratch writes.
enum class MappedArithmetic {Gain,Envelope,Interleave,Add,Interl};
inline Program makeMappedArithmetic(MappedArithmetic operation){
    bool envelope=operation==MappedArithmetic::Envelope,interleave=operation==MappedArithmetic::Interleave;
    bool add=operation==MappedArithmetic::Add,interl=operation==MappedArithmetic::Interl;
    unsigned pointers=envelope?5:interleave?3:2;
    Program p;std::map<std::string,unsigned> labels;
    std::vector<std::pair<unsigned,std::string>> jumps;
    auto label=[&](std::string n){labels[n]=p.words.size();};
    auto jump=[&](std::string n,const char* c="always"){p.Emit(std::string("br 0x00000000 ")+c);jumps.emplace_back(p.words.size()-1,n);};
    p.Emit("load 0x0000u8 page");
    p.Emit("mov [page:0x0006u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("invalid","neq");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0002u8] a0l");p.Emit("sub 0x0000 a0",ResidentDsp::ParameterSlots-1);jump("invalid","gt");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0001u8] a0l");p.Emit("sub 0x0000 a0",ResidentDsp::AudioDmemBytes/2);jump("invalid","gt");
    p.Emit("mov [page:0x0001u8] a0l");p.Emit("and 0x0000 a0",(envelope || interl)?7:interleave?3:15);jump("invalid","neq");
    if(envelope || add || interl){p.Emit("mov [page:0x0001u8] a0l");p.Emit("cmpv 0x0000 a0l",0);jump("invalid","eq");}
    p.Emit("clr a0 always");p.Emit("mov [page:0x0002u8] a0l");p.Emit("shfi a0 a0 +0x0004");
    p.Emit("add 0x0000 a0",ResidentDsp::ParameterWordBase);p.Emit("mov a0l r5");
    // Validate every sample offset before entering the arithmetic kernel.
    for(unsigned i=0;i<pointers;++i){
        p.Emit("clr a0 always");p.Emit("mov [r5++] a0l");p.Emit("sub 0x0000 a0",ResidentDsp::AudioDmemBytes/2);jump("invalid","gt");
        p.Emit("clr a1 always");p.Emit("mov [page:0x0001u8] a1l");if((interleave && i==2) || (interl && i==0))p.Emit("shfi a1 a1 +0x0001");p.Emit("add a1 a0");jump("invalid","gt");
    }
    if(envelope){p.Emit("clr a0 always");p.Emit("mov [r5] a0l");p.Emit("sub 0x0000 a0",31);jump("invalid","gt");}
    p.Emit("mov [page:0x0001u8] a0l");p.Emit("cmpv 0x0000 a0l",0);jump("success","eq");
    p.Emit("clr a0 always");p.Emit("mov [page:0x0002u8] a0l");p.Emit("shfi a0 a0 +0x0004");p.Emit("add 0x0000 a0",ResidentDsp::ParameterWordBase);p.Emit("mov a0l r5");
    for(unsigned i=0;i<pointers;++i){
        p.Emit("clr a0 always");p.Emit("mov [r5++] a0l");p.Emit("add 0x0000 a0",ResidentDsp::AudioDmemWordBase);
        char op[64];std::snprintf(op,sizeof(op),"mov a0l r%u",i);p.Emit(op);
    }
    if(add || interl){
        p.Emit("clr a0 always");p.Emit("mov [page:0x0001u8] a0l");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");
        p.Emit("bkrep r6 0x00000000");unsigned repeat=p.words.size()-1;
        if(add){
            p.Emit("mov [r1] a0");p.Emit("add [r0++] a0");
            p.Emit("shfi a0 a0 +0x0010");p.Emit("mov a0h [r1++]");
        }else{
            p.Emit("mov [r0++] a0l");p.Emit("mov a0l [r1++]");
            p.Emit("modr [r0++]"); // skip alternate sample after storing, preserving aliases
        }
        p.words[repeat]=p.words.size()-1;p.relocations.push_back(repeat);jump("success");
    }else if(interleave){
        p.Emit("clr a0 always");p.Emit("mov [page:0x0001u8] a0l");p.Emit("shfi a0 a0 -0x0002");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");
        p.Emit("bkrep r6 0x00000000");unsigned repeat=p.words.size()-1;
        // Production loads four samples from both channels before any stores.
        // Keeping that grouping preserves even partially overlapping buffers.
        p.Emit("mov 0x0000 r3",0x3d00);
        for(unsigned channel=0;channel<2;++channel)for(unsigned i=0;i<4;++i){p.Emit(channel?"mov [r1++] a0l":"mov [r0++] a0l");p.Emit("mov a0l [r3++]");}
        p.Emit("mov 0x0000 r3",0x3d00);p.Emit("mov 0x0000 r4",0x3d04);
        for(unsigned i=0;i<4;++i){p.Emit("mov [r3++] a0l");p.Emit("mov a0l [r2++]");p.Emit("mov [r4++] a0l");p.Emit("mov a0l [r2++]");}
        p.words[repeat]=p.words.size()-1;p.relocations.push_back(repeat);jump("success");
    }else if(envelope){
        p.Emit("mov [r5++] a0l");p.Emit("mov 0x0000 r7",0x3b06);p.Emit("mov a0l [r7]");
        p.Emit("mov 0x0000 r7",0x3b00);
        for(unsigned i=0;i<6;++i){p.Emit("mov [r5++] a0l");p.Emit("mov a0l [r7++]");}
        p.Emit("clr a0 always");p.Emit("mov [page:0x0001u8] a0l");p.Emit("shfi a0 a0 -0x0003");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");
        auto k=makeEnvelope(0,true);k.words.resize(k.words.size()-2);
        for(unsigned fix:k.relocations)if(fix<k.words.size()){k.words[fix]+=p.words.size();p.relocations.push_back(p.words.size()+fix);}
        p.words.insert(p.words.end(),k.words.begin(),k.words.end());jump("success");
    }else{
        p.Emit("mov r5 a0");p.Emit("add 0x0000 a0",10);p.Emit("mov a0l r5");p.Emit("mov [r5] r2");
        p.Emit("clr a0 always");p.Emit("mov [page:0x0001u8] a0l");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");
        p.Emit("mov r2 b0l");p.Emit("cmpv 0x0000 b0l",0x8000);jump("subtract","eq");
        auto appendGain=[&](bool subtract){auto k=MakeProgram(subtract,true);k.words[1]+=p.words.size();p.relocations.push_back(p.words.size()+1);p.words.insert(p.words.end(),k.words.begin(),k.words.end());};
        appendGain(false);jump("success");label("subtract");appendGain(true);jump("success");
    }
    label("invalid");p.Emit("mov 0x0000 b0l",3);jump("complete");label("success");p.Emit("clr b0 always");
    label("complete");p.Emit("mov b0l [page:0x0061u8]");label("done");jump("done");
    for(auto& fix:jumps){p.words[fix.first]=labels.at(fix.second);p.relocations.push_back(fix.first);}
    return p;
}
