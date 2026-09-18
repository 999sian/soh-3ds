#pragma once
#include "mapped_parameters.h"
#include "dynamic_adpcm_program.h"
#include "s8_program.h"
// Validated direct decoder. Record0=compressed byte offset,1=output sample
// offset (16 history samples before decoded PCM),2=state,3=loop,4=book,5=flags.
// Private24..2d and80..84. ADPCM rejects compressed/output overlap. S8 uses
// record4=0 and retains sequential aliases without predictor header preflight.
inline Program makeMappedAdpcm(bool twoBit,bool s8=false){
    Program p;std::map<std::string,unsigned> labels;std::vector<std::pair<unsigned,std::string>> jumps;
    auto label=[&](std::string n){labels[n]=p.words.size();};
    auto jump=[&](std::string n,const char* c="always"){p.Emit(std::string("br 0x00000000 ")+c);jumps.emplace_back(p.words.size()-1,n);};
    auto load=[&](unsigned field,const char* dest){char op[80];std::snprintf(op,sizeof(op),"mov [page:0x%04xu8] %s",field,dest);p.Emit(op);};
    auto store=[&](const char* src,unsigned field){char op[80];std::snprintf(op,sizeof(op),"mov %s [page:0x%04xu8]",src,field);p.Emit(op);};
    p.Emit("load 0x0000u8 page");load(6,"b0l");p.Emit("cmpv 0x0000 b0l",0);jump("invalid","neq");
    p.Emit("clr a0 always");load(2,"a0l");p.Emit("sub 0x0000 a0",63);jump("invalid","gt");
    p.Emit("clr a0 always");load(1,"a0l");p.Emit("sub 0x0000 a0",1520);jump("invalid","gt");
    load(1,"a0l");p.Emit("and 0x0000 a0",15);jump("invalid","neq");
    p.Emit("clr a0 always");load(1,"a0l");p.Emit("add 0x0000 a0",16);p.Emit("shfi a0 a0 +0x0001");store("a0l",0x83);
    p.Emit("clr a0 always");load(1,"a0l");p.Emit("shfi a0 a0 -0x0004");store("a0l",0x24);
    if(s8){p.Emit("clr a0 always");load(1,"a0l");store("a0l",0x82);}else{
    p.Emit("mov a0 a1");p.Emit(twoBit?"shfi a0 a0 +0x0002":"shfi a0 a0 +0x0003");p.Emit("add a1 a0");store("a0l",0x82);}
    p.Emit("clr a0 always");load(2,"a0l");p.Emit("shfi a0 a0 +0x0004");p.Emit("add 0x0000 a0",ResidentDsp::ParameterWordBase);p.Emit("mov a0l r5");
    // Region is immutable in this command record:0=audioDMEM,1=payload bank.
    p.Emit("mov r5 a0");p.Emit("add 0x0000 a0",6);p.Emit("mov a0l r0");p.Emit("mov [r0] b0l");
    p.Emit("cmpv 0x0000 b0l",0);jump("region_ok","eq");p.Emit("cmpv 0x0000 b0l",1);jump("invalid","neq");
    label("region_ok");store("b0l",0x84);
    p.Emit("clr a0 always");p.Emit("mov [r5++] a0l");store("a0l",0x80);
    p.Emit("cmpv 0x0000 b0l",1);jump("payload_extent","eq");p.Emit("sub 0x0000 a0",3072);jump("check_source_end");
    label("payload_extent");p.Emit("sub 0x0000 a0",ResidentDsp::PayloadBytes);
    label("check_source_end");jump("invalid","gt");p.Emit("clr a1 always");load(0x82,"a1l");p.Emit("add a1 a0");jump("invalid","gt");
    p.Emit("clr a0 always");load(0x80,"a0l");p.Emit("cmpv 0x0000 b0l",1);jump("payload_address","eq");p.Emit("add 0x0000 a0",ResidentDsp::AudioDmemWordBase*2);jump("source_address");
    label("payload_address");p.Emit("add 0x0000 a0",ResidentDsp::PayloadWordBase*2);
    label("source_address");store("a0l",0x2c);
    p.Emit("clr a0 always");p.Emit("mov [r5++] a0l");p.Emit("mov a0 a1");p.Emit("shfi a1 a1 +0x0001");store("a1l",0x81);
    p.Emit("sub 0x0000 a0",1536-16);jump("invalid","gt");p.Emit("clr a1 always");load(1,"a1l");p.Emit("add a1 a0");jump("invalid","gt");
    p.Emit("clr a0 always");load(0x81,"a0l");p.Emit("shfi a0 a0 -0x0001");p.Emit("add 0x0000 a0",ResidentDsp::AudioDmemWordBase);store("a0l",0x26);
    for(unsigned i=0;i<3;++i){
        p.Emit("clr a0 always");p.Emit("mov [r5++] a0l");p.Emit("sub 0x0000 a0",i==2?(s8?0:15):63);jump("invalid","gt");p.Emit("add 0x0000 a0",i==2?(s8?0:15):63);
        p.Emit(i==2?"shfi a0 a0 +0x0007":"shfi a0 a0 +0x0004");p.Emit("add 0x0000 a0",i==0?ResidentDsp::AdpcmStateWordBase:i==1?ResidentDsp::AdpcmLoopWordBase:ResidentDsp::AdpcmBookWordBase);store("a0l",i==0?0x2a:i==1?0x2b:0x2d);
    }
    p.Emit("mov [r5] b0l");for(unsigned flags:{0u,1u,2u}){p.Emit("cmpv 0x0000 b0l",flags);jump("flags_ok","eq");}jump("invalid");
    label("flags_ok");store("b0l",0x25);if(s8)jump("decode");load(0x84,"b0l");p.Emit("cmpv 0x0000 b0l",1);jump("decode","eq");load(0x82,"a0l");p.Emit("cmpv 0x0000 a0l",0);jump("decode","eq");
    // Source end <= output start, or output end <= source start.
    p.Emit("clr a0 always");load(0x80,"a0l");p.Emit("clr a1 always");load(0x82,"a1l");p.Emit("add a1 a0");p.Emit("clr a1 always");load(0x81,"a1l");p.Emit("sub a1 a0");jump("decode","le");
    p.Emit("clr a0 always");load(0x81,"a0l");p.Emit("clr a1 always");load(0x83,"a1l");p.Emit("add a1 a0");p.Emit("clr a1 always");load(0x80,"a1l");p.Emit("sub a1 a0");jump("invalid","gt");
    label("decode");{
        auto k=s8?makeS8():makeDynamicAdpcm(twoBit,true);k.words.resize(k.words.size()-2);
        for(unsigned fix:k.relocations)if(fix<k.words.size()){k.words[fix]+=p.words.size();p.relocations.push_back(p.words.size()+fix);}
        p.words.insert(p.words.end(),k.words.begin(),k.words.end());load(0x27,"b0l");jump("complete");
    }
    label("invalid");p.Emit("mov 0x0000 b0l",3);label("complete");store("b0l",0x61);label("done");jump("done");
    for(auto& fix:jumps){p.words[fix.first]=labels.at(fix.second);p.relocations.push_back(fix.first);}return p;
}
