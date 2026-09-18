#pragma once
#include "mapped_arithmetic_program.h"
#include "resample_program.h"
namespace ResidentDsp {constexpr uint16_t ResampleStateWordBase=0x6400;}
// Record: input sample offset,output offset,state slot,pitch,flags0/1/2.
// Count is normalized positive multiple8. Private70..76; result61.
inline Program makeMappedResample(){
    Program p;std::map<std::string,unsigned> labels;std::vector<std::pair<unsigned,std::string>> jumps;
    auto label=[&](std::string n){labels[n]=p.words.size();};
    auto jump=[&](std::string n,const char* c="always"){p.Emit(std::string("br 0x00000000 ")+c);jumps.emplace_back(p.words.size()-1,n);};
    auto load=[&](unsigned field,const char* dest){char op[80];std::snprintf(op,sizeof(op),"mov [page:0x%04xu8] %s",field,dest);p.Emit(op);};
    auto store=[&](const char* src,unsigned field){char op[80];std::snprintf(op,sizeof(op),"mov %s [page:0x%04xu8]",src,field);p.Emit(op);};
    auto stateAddress=[&](unsigned offset,const char* reg){p.Emit("clr a1 always");load(0x72,"a1l");if(offset)p.Emit("add 0x0000 a1",offset);p.Emit(std::string("mov a1l ")+reg);};
    auto copy=[&](unsigned n){for(unsigned i=0;i<n;++i){p.Emit("mov [r1++] a0l");p.Emit("mov a0l [r0++]");}};
    p.Emit("load 0x0000u8 page");load(6,"b0l");p.Emit("cmpv 0x0000 b0l",0);jump("invalid","neq");
    p.Emit("clr a0 always");load(2,"a0l");p.Emit("sub 0x0000 a0",63);jump("invalid","gt");
    p.Emit("clr a0 always");load(1,"a0l");p.Emit("sub 0x0000 a0",1536);jump("invalid","gt");
    load(1,"a0l");p.Emit("and 0x0000 a0",7);jump("invalid","neq");load(1,"a0l");p.Emit("cmpv 0x0000 a0l",0);jump("invalid","eq");
    p.Emit("clr a0 always");load(2,"a0l");p.Emit("shfi a0 a0 +0x0004");p.Emit("add 0x0000 a0",ResidentDsp::ParameterWordBase);p.Emit("mov a0l r5");
    for(unsigned i=0;i<2;++i){
        p.Emit("clr a0 always");p.Emit("mov [r5++] a0l");p.Emit("sub 0x0000 a0",1536);jump("invalid","gt");
        if(i){p.Emit("clr a1 always");load(1,"a1l");p.Emit("add a1 a0");jump("invalid","gt");p.Emit("sub a1 a0");}
        p.Emit("add 0x0000 a0",1536+ResidentDsp::AudioDmemWordBase);store("a0l",0x70+i);
    }
    p.Emit("clr a0 always");p.Emit("mov [r5++] a0l");p.Emit("sub 0x0000 a0",63);jump("invalid","gt");p.Emit("add 0x0000 a0",63);p.Emit("shfi a0 a0 +0x0004");p.Emit("add 0x0000 a0",ResidentDsp::ResampleStateWordBase);store("a0l",0x72);
    p.Emit("mov [r5++] a0l");store("a0l",0x74);p.Emit("mov [r5] b0l");
    for(unsigned flags:{0u,1u,2u}){p.Emit("cmpv 0x0000 b0l",flags);jump("flags_ok","eq");}jump("invalid");
    label("flags_ok");store("b0l",0x73);
    // Prefix writes and adjusted starting position are computed before mutation.
    p.Emit("clr a0 always");load(0x70,"a0l");p.Emit("sub 0x0000 a0",ResidentDsp::AudioDmemWordBase+4);jump("invalid","lt");
    p.Emit("add 0x0000 a0",ResidentDsp::AudioDmemWordBase);store("a0l",0x76);
    p.Emit("cmpv 0x0000 b0l",2);jump("phase","neq");
    p.Emit("sub 0x0000 a0",ResidentDsp::AudioDmemWordBase+4);jump("invalid","lt");
    stateAddress(5,"r1");p.Emit("mov [r1] b0l");
    for(int adjustment:{0,-9,-10,-11,-12,-13,-14,-15}){p.Emit("cmpv 0x0000 b0l",uint16_t(adjustment));jump("adjustment_ok","eq");}jump("invalid");
    label("adjustment_ok");p.Emit("mov [r1] a0");p.Emit("shfi a0 a0 -0x0001");p.Emit("clr a1 always");load(0x76,"a1l");p.Emit("sub a0 a1");store("a1l",0x76);
    label("phase");load(0x73,"b0l");p.Emit("cmpv 0x0000 b0l",1);jump("phase_init","eq");stateAddress(4,"r1");p.Emit("mov [r1] a0l");jump("phase_store");
    label("phase_init");p.Emit("clr a0 always");label("phase_store");store("a0l",0x75);
    // End pointer = start + ((phase + 2*pitch*count) >>16). Count fits signed16,
    // so signed-count x unsigned-pitch gives a nonnegative full-width product.
    load(1,"y0");p.Emit("mov 0x0000 r5",0x74);p.Emit("mpysu y0 [r5] a0");p.Emit("mov p* a0");p.Emit("shfi a0 a0 +0x0001");
    p.Emit("clr a1 always");load(0x75,"a1l");p.Emit("add a1 a0");p.Emit("shfi a0 a0 -0x0010");
    p.Emit("clr a1 always");load(0x76,"a1l");p.Emit("add a1 a0");p.Emit("mov a0 b1");
    p.Emit("sub 0x0000 a0",ResidentDsp::AudioDmemWordBase+1536-4);jump("invalid","gt");
    p.Emit("mov b1 a0");p.Emit("clr a1 always");load(0x70,"a1l");p.Emit("sub a1 a0");p.Emit("add 0x0000 a0",4);p.Emit("and 0x0000 a0",7);
    p.Emit("sub a0 b1");p.Emit("mov b1 a0");p.Emit("sub 0x0000 a0",ResidentDsp::AudioDmemWordBase+1536-8);jump("invalid","gt");
    // All extents are valid. Match production's history-write order.
    load(0x73,"b0l");p.Emit("cmpv 0x0000 b0l",2);jump("prefix","neq");
    p.Emit("clr a0 always");load(0x70,"a0l");p.Emit("sub 0x0000 a0",8);p.Emit("mov a0l r0");stateAddress(8,"r1");copy(8);
    label("prefix");load(0x76,"r0");load(0x73,"b0l");p.Emit("cmpv 0x0000 b0l",1);jump("zero_prefix","eq");stateAddress(0,"r1");copy(4);jump("run");
    label("zero_prefix");p.Emit("clr a0 always");for(unsigned i=0;i<4;++i)p.Emit("mov a0l [r0++]");
    label("run");load(0x76,"r0");load(0x71,"r2");load(0x75,"r4");p.Emit("mov 0x0000 r3",0x4000);
    p.Emit("clr a0 always");load(0x74,"a0l");p.Emit("shfi a0 a0 +0x0001");p.Emit("mov a0l r5");p.Emit("shfi a0 a0 -0x0010");p.Emit("mov a0l r7");
    p.Emit("clr a0 always");load(1,"a0l");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");
    auto k=makeResampleBuffer();k.words[1]+=p.words.size();p.relocations.push_back(p.words.size()+1);p.words.insert(p.words.end(),k.words.begin(),k.words.end());
    stateAddress(4,"r1");p.Emit("mov r4 [r1]");p.Emit("mov r0 b1l");stateAddress(0,"r1");
    for(unsigned i=0;i<4;++i){p.Emit("mov [r0++] a0l");p.Emit("mov a0l [r1++]");}
    p.Emit("mov b1l r0");p.Emit("clr a0 always");p.Emit("mov r0 a0l");p.Emit("clr a1 always");load(0x70,"a1l");p.Emit("sub a1 a0");p.Emit("add 0x0000 a0",4);p.Emit("and 0x0000 a0",7);
    p.Emit("clr a1 always");p.Emit("mov r0 a1l");p.Emit("sub a0 a1");p.Emit("mov a1l r0");p.Emit("cmpv 0x0000 a0l",0);jump("save_adjustment","eq");
    p.Emit("clr a1 always");p.Emit("sub 0x0000 a1",8);p.Emit("sub a0 a1");p.Emit("mov a1l a0l");
    label("save_adjustment");stateAddress(5,"r1");p.Emit("mov a0l [r1]");stateAddress(8,"r1");for(unsigned i=0;i<8;++i){p.Emit("mov [r0++] a0l");p.Emit("mov a0l [r1++]");}
    p.Emit("clr b0 always");jump("complete");label("invalid");p.Emit("mov 0x0000 b0l",3);
    label("complete");store("b0l",0x61);label("done");jump("done");
    for(auto& fix:jumps){p.words[fix.first]=labels.at(fix.second);p.relocations.push_back(fix.first);}return p;
}
