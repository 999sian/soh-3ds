#pragma once
#include "firmware.h"
#include "resample_state_program.h"
#include "adpcm_program.h"
#include "envelope_program.h"
#include "filter_program.h"
#include "dynamic_adpcm_program.h"
#include "dmem_transfer_program.h"
#include "mapped_arithmetic_program.h"
#include "mapped_resample_program.h"
#include "mapped_adpcm_program.h"
#include "mapped_zoh_program.h"
#include "mapped_duplicate_program.h"
#include "mapped_filter_program.h"
#include "mapped_scalar_program.h"
// Prototype dispatcher for resident kernels, separate from the released v4
// firmware. Layout is intentionally fixed until shared command-DMEM mapping.
// word5 type: 1=resample,2=ADPCM4bit,3=ADPCM2bit; word1 sample count,
// word2 pitch, word6 flags. word0 busy,3 sequence; reply11seq/12status.
inline Program makeTypedFirmware(bool extended=false,bool multiBlock=false,bool completionInterrupt=false,bool mappedDmem=false,bool mailboxOnly=false) {
    if(mappedDmem)completionInterrupt=true;
    if(completionInterrupt)multiBlock=true;
    if(multiBlock)extended=true;
    Program p;std::map<std::string,unsigned> labels;
    std::vector<std::pair<unsigned,std::string>> jumps;
    auto label=[&](std::string n){labels[n]=p.words.size();};
    auto jump=[&](std::string n,const char* c="always"){p.Emit(std::string("br 0x00000000 ")+c);jumps.emplace_back(p.words.size()-1,n);};
    auto append=[&](Program kernel){
        kernel.words.resize(kernel.words.size()-2); // standalone terminal loop
        unsigned base=p.words.size();
        for(unsigned fix:kernel.relocations)if(fix<kernel.words.size())kernel.words[fix]+=base;
        p.words.insert(p.words.end(),kernel.words.begin(),kernel.words.end());
    };
    p.Emit("mov 0x0000 mod0",2);p.Emit("mov 0x0000 mod1",0);p.Emit("mov 0x0000 mod2",0);p.Emit("mov 0x0000 mod3",0);
    p.Emit("load 0x0080u8 page");p.Emit("clr b0 always");p.Emit("mov b0l [page:0x00c8u8]");
    p.Emit("load 0x0000u8 page");p.Emit("mov b0l [page:0x0000u8]");
    p.Emit("mov 0x0000 b0l",mappedDmem?0x4458:completionInterrupt?0x4457:multiBlock?0x4456:extended?0x4455:0x4454);p.Emit("mov b0l [page:0x0010u8]");
    label("idle");p.Emit("load 0x0080u8 page");p.Emit("mov [page:0x00d8u8] a0l");p.Emit("and 0x0000 a0",0x8000);jump("request","eq");
    p.Emit("mov [page:0x00cau8] a0l");p.Emit("cmpv 0x0000 a0l",0x8000);jump("shutdown","eq");
    label("request");p.Emit("load 0x0000u8 page");p.Emit("mov [page:0x0000u8] b0l");jump("idle","eq");
    // word7=0 is a single command; 1..32 runs resident four-word descriptors
    // at100. A later invalid descriptor aborts with partial progress: callers
    // must discard/reset that batch's state rather than retry it blindly.
    p.Emit("clr b0 always");p.Emit("mov b0l [page:0x0031u8]");
    p.Emit("mov [page:0x0007u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("dispatch","eq");
    for(unsigned count=1;count<=32;++count){p.Emit("cmpv 0x0000 b0l",count);jump("queue_valid","eq");}jump("bad_command");
    label("queue_valid");p.Emit("mov b0l [page:0x0031u8]");p.Emit("mov 0x0000 b0l",0x100);p.Emit("mov b0l [page:0x0030u8]");
    label("dequeue");p.Emit("mov [page:0x0030u8] r1");
    for(unsigned field:{5,1,2,6}){p.Emit("mov [r1++] a0l");char op[64];std::snprintf(op,sizeof(op),"mov a0l [page:0x%04xu8]",field);p.Emit(op);}
    p.Emit("mov r1 [page:0x0030u8]");p.Emit("clr a0 always");p.Emit("mov [page:0x0031u8] a0l");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l [page:0x0031u8]");
    label("dispatch");
    if(mappedDmem){
        // Transfer descriptors use count=bytes, argument=source offset and
        // flags=destination offset, so bypass the synthesis flags validator.
        p.Emit("mov [page:0x0005u8] b0l");p.Emit("cmpv 0x0000 b0l",7);jump("mapped_move","eq");
        p.Emit("cmpv 0x0000 b0l",8);jump("mapped_clear","eq");
        p.Emit("cmpv 0x0000 b0l",9);jump("mapped_gain","eq");
        p.Emit("cmpv 0x0000 b0l",10);jump("mapped_envelope","eq");
        p.Emit("cmpv 0x0000 b0l",11);jump("mapped_interleave","eq");
        p.Emit("cmpv 0x0000 b0l",12);jump("mapped_resample","eq");
        p.Emit("cmpv 0x0000 b0l",13);jump("mapped_adpcm4","eq");
        p.Emit("cmpv 0x0000 b0l",14);jump("mapped_adpcm2","eq");
        p.Emit("cmpv 0x0000 b0l",15);jump("mapped_add","eq");
        p.Emit("cmpv 0x0000 b0l",16);jump("mapped_interl","eq");
        p.Emit("cmpv 0x0000 b0l",17);jump("mapped_zoh","eq");
        p.Emit("cmpv 0x0000 b0l",18);jump("mapped_duplicate","eq");
        p.Emit("cmpv 0x0000 b0l",19);jump("mapped_s8","eq");
        p.Emit("cmpv 0x0000 b0l",20);jump("mapped_filter_setup","eq");
        p.Emit("cmpv 0x0000 b0l",21);jump("mapped_filter","eq");
        p.Emit("cmpv 0x0000 b0l",22);jump("mapped_hilo","eq");
        p.Emit("cmpv 0x0000 b0l",23);jump("mapped_table","eq");
        p.Emit("cmpv 0x0000 b0l",24);jump("mapped_load","eq");
    }
    p.Emit("mov [page:0x0006u8] b0l");
    for(unsigned flags:{0,1,2}){p.Emit("cmpv 0x0000 b0l",flags);jump("flags_valid","eq");}jump("bad_command");
    label("flags_valid");p.Emit("mov b0l [page:0x0021u8]");
    p.Emit("mov [page:0x0005u8] b0l");
    for(unsigned type=0;type<(extended?7u:5u);++type){p.Emit("cmpv 0x0000 b0l",type);jump("type"+std::to_string(type),"eq");}jump("bad_command");
    label("type0");p.Emit("mov [page:0x0001u8] b0l");
    p.Emit("cmpv 0x0000 b0l",0);jump("success","eq");
    if(multiBlock){for(unsigned n=16;n<=512;n+=16){p.Emit("cmpv 0x0000 b0l",n);jump("gain_valid","eq");}}
    else for(unsigned n:{16,32,64,128,192,256,512}){p.Emit("cmpv 0x0000 b0l",n);jump("gain_valid","eq");}
    jump("bad_command");
    label("gain_valid");p.Emit("clr a0 always");p.Emit("mov b0l a0l");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");
    p.Emit("mov 0x0000 r0",0x1000);p.Emit("mov 0x0000 r1",0x1400);p.Emit("mov [page:0x0002u8] r2");
    p.Emit("mov r2 b0l");p.Emit("cmpv 0x0000 b0l",0x8000);jump("subtract","eq");
    {auto k=MakeProgram(false,true);k.words[1]+=p.words.size();p.words.insert(p.words.end(),k.words.begin(),k.words.end());}jump("success");
    label("subtract");{auto k=MakeProgram(true,true);k.words[1]+=p.words.size();p.words.insert(p.words.end(),k.words.begin(),k.words.end());}jump("success");
    label("type4"); // stage decoded samples for the resampler, wholly on DSP
    p.Emit("mov [page:0x0001u8] b0l");
    if(multiBlock){
        p.Emit("cmpv 0x0000 b0l",0);jump("success","eq");
        for(unsigned n=16;n<=512;n+=16){p.Emit("cmpv 0x0000 b0l",n);jump("copy_valid","eq");}jump("bad_command");
        label("copy_valid");p.Emit("clr a0 always");p.Emit("mov b0l a0l");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");
    }else{p.Emit("cmpv 0x0000 b0l",16);jump("bad_command","neq");}
    p.Emit("mov 0x0000 r0",0x3010);p.Emit("mov 0x0000 r1",0x400);
    if(multiBlock){p.Emit("bkrep r6 0x00000000");unsigned end=p.words.size()-1;p.Emit("mov [r0++] a0l");p.Emit("mov a0l [r1++]");p.words[end]=p.words.size()-1;}
    else for(unsigned i=0;i<16;++i){p.Emit("mov [r0++] a0l");p.Emit("mov a0l [r1++]");}
    jump("success");
    label("type1");p.Emit("mov [page:0x0001u8] b0l");
    p.Emit("cmpv 0x0000 b0l",0);jump("min_count","eq");
    if(multiBlock){for(unsigned n=8;n<=512;n+=8){p.Emit("cmpv 0x0000 b0l",n);jump("count_valid","eq");}}
    else for(unsigned n:{8,16,32,64,128,192,256,512}){p.Emit("cmpv 0x0000 b0l",n);jump("count_valid","eq");}
    jump("bad_command");
    label("min_count");p.Emit("mov 0x0000 b0l",8);
    label("count_valid");p.Emit("mov b0l [page:0x0020u8]");p.Emit("mov [page:0x0002u8] b0l");p.Emit("mov b0l [page:0x0022u8]");
    p.Emit("mov [page:0x0006u8] b0l");p.Emit("cmpv 0x0000 b0l",2);jump("resample","neq");
    p.Emit("mov 0x0000 r0",0x3a05);p.Emit("mov [r0] b0l");
    for(int adjustment:{0,-9,-10,-11,-12,-13,-14,-15}){p.Emit("cmpv 0x0000 b0l",uint16_t(adjustment));jump("resample","eq");}jump("bad_state");
    label("resample");
    {
        auto k=makeResampleState(0x3a00);k.words.resize(k.words.size()-2); // remove probe's infinite done loop
        unsigned base=p.words.size();
        for(unsigned fix:k.relocations)if(fix<k.words.size())k.words[fix]+=base;
        p.words.insert(p.words.end(),k.words.begin(),k.words.end());
    }
    jump("success");
    for(unsigned type:{2,3}) {
        if(multiBlock){
            label("type"+std::to_string(type));p.Emit("mov [page:0x0001u8] b0l");
            std::string valid="adpcm_count"+std::to_string(type);
            for(unsigned n=0;n<=512;n+=16){p.Emit("cmpv 0x0000 b0l",n);jump(valid,"eq");}jump("bad_command");
            label(valid);p.Emit("clr a0 always");p.Emit("mov b0l a0l");p.Emit("shfi a0 a0 -0x0004");p.Emit("mov a0l [page:0x0024u8]");
            p.Emit("mov [page:0x0006u8] b0l");p.Emit("mov b0l [page:0x0025u8]");
            append(makeDynamicAdpcm(type==3));p.Emit("mov [page:0x0027u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("bad_command","neq");jump("success");
            continue;
        }
        label("type"+std::to_string(type));p.Emit("mov [page:0x0001u8] b0l");p.Emit("cmpv 0x0000 b0l",16);jump("bad_command","neq");
        p.Emit("mov [page:0x0006u8] b0l");
        for(unsigned flags:{0,1,2}){p.Emit("cmpv 0x0000 b0l",flags);jump("decode"+std::to_string(type)+std::to_string(flags),"eq");}
        jump("bad_command");
        for(unsigned flags:{0,1,2}) {
            label("decode"+std::to_string(type)+std::to_string(flags));
            // Reject predictor selectors beyond the production eight-book bank.
            p.Emit("mov 0x0000 r0",0x800);p.Emit("mov [r0] a0");p.Emit("and 0x0000 a0",8);jump("bad_command","neq");
            auto k=makeAdpcmState(type==3,1,flags);p.words.insert(p.words.end(),k.words.begin(),k.words.end());jump("success");
        }
    }
    if(extended){
        label("type5");jump("extended_count");
        label("type6");
        p.Emit("mov [page:0x0006u8] b0l");p.Emit("cmpv 0x0000 b0l",2);jump("bad_command","eq");
        label("extended_count");p.Emit("mov [page:0x0001u8] b0l");
        p.Emit("cmpv 0x0000 b0l",0);jump("extended_min","eq");
        if(multiBlock){for(unsigned n=8;n<=512;n+=8){p.Emit("cmpv 0x0000 b0l",n);jump("extended_valid","eq");}}
        else for(unsigned n:{8,16,32,64,128,192,256,512}){p.Emit("cmpv 0x0000 b0l",n);jump("extended_valid","eq");}
        jump("bad_command");
        label("extended_min");p.Emit("mov 0x0000 b0l",8);
        label("extended_valid");p.Emit("clr a0 always");p.Emit("mov b0l a0l");p.Emit("shfi a0 a0 -0x0003");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");
        p.Emit("mov [page:0x0005u8] b0l");p.Emit("cmpv 0x0000 b0l",6);jump("filter_select","eq");
        // Envelope n_samples rounds positive8 up to16, whereas its zero
        // do/while case and the filter's eight-sample case both process8.
        p.Emit("mov [page:0x0001u8] b0l");
        if(multiBlock){
            p.Emit("cmpv 0x0000 b0l",0);jump("envelope_count_ready","eq");
            p.Emit("clr a0 always");p.Emit("mov b0l a0l");p.Emit("add 0x0000 a0",15);p.Emit("and 0x0000 a0",0xfff0);
            p.Emit("shfi a0 a0 -0x0003");p.Emit("sub 0x0000 a0",1);p.Emit("mov a0l r6");
        }else{p.Emit("cmpv 0x0000 b0l",8);jump("envelope_count_ready","neq");p.Emit("mov 0x0000 r6",1);}
        label("envelope_count_ready");
        p.Emit("mov [page:0x0002u8] a0");p.Emit("and 0x0000 a0",0xffe0);jump("bad_command","neq");
        p.Emit("mov [page:0x0002u8] a0l");p.Emit("mov 0x0000 r5",0x3b06);p.Emit("mov a0l [r5]");
        p.Emit("mov 0x0000 r0",0x1000);p.Emit("mov 0x0000 r1",0x1400);p.Emit("mov 0x0000 r2",0x1800);
        p.Emit("mov 0x0000 r3",0x1c00);p.Emit("mov 0x0000 r4",0x2c00);
        append(makeEnvelope(0,true));jump("success");
        label("filter_select");p.Emit("mov 0x0000 r0",0x1400);
        p.Emit("mov [page:0x0006u8] b0l");p.Emit("cmpv 0x0000 b0l",1);jump("filter_init","eq");
        append(makeFilter(false));jump("success");
        label("filter_init");append(makeFilter(true));jump("success");
    }
    if(mappedDmem){
        for(auto operation:{MappedArithmetic::Gain,MappedArithmetic::Envelope,MappedArithmetic::Interleave,MappedArithmetic::Add,MappedArithmetic::Interl}){
            label(operation==MappedArithmetic::Add?"mapped_add":operation==MappedArithmetic::Interl?"mapped_interl":operation==MappedArithmetic::Envelope?"mapped_envelope":operation==MappedArithmetic::Interleave?"mapped_interleave":"mapped_gain");append(makeMappedArithmetic(operation));
            p.Emit("mov [page:0x0061u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("success","eq");jump("complete");
        }
        for(bool twoBit:{false,true}){
            label(twoBit?"mapped_adpcm2":"mapped_adpcm4");append(makeMappedAdpcm(twoBit));
            p.Emit("mov [page:0x0061u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("success","eq");jump("complete");
        }
        for(bool table:{false,true}){
            label(table?"mapped_table":"mapped_hilo");append(makeMappedScalar(table));
            p.Emit("mov [page:0x0061u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("success","eq");jump("complete");
        }
        for(bool setup:{true,false}){
            label(setup?"mapped_filter_setup":"mapped_filter");append(makeMappedFilter(setup));
            p.Emit("mov [page:0x0061u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("success","eq");jump("complete");
        }
        label("mapped_s8");append(makeMappedAdpcm(false,true));
        p.Emit("mov [page:0x0061u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("success","eq");jump("complete");
        label("mapped_duplicate");append(makeMappedDuplicate());
        p.Emit("mov [page:0x0061u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("success","eq");jump("complete");
        label("mapped_zoh");append(makeMappedZoh());
        p.Emit("mov [page:0x0061u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("success","eq");jump("complete");
        label("mapped_resample");append(makeMappedResample());
        p.Emit("mov [page:0x0061u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("success","eq");jump("complete");
        label("mapped_load");p.Emit("clr b0 always");p.Emit("mov b0l [page:0x0054u8]");
        for(auto pair:std::vector<std::pair<unsigned,unsigned>>{{1,0x52},{2,0x50},{6,0x51}}){
            char op[80];std::snprintf(op,sizeof(op),"mov [page:0x%04xu8] b0l",pair.first);p.Emit(op);
            std::snprintf(op,sizeof(op),"mov b0l [page:0x%04xu8]",pair.second);p.Emit(op);
        }
        append(makeDmemTransfer(true));p.Emit("mov [page:0x0053u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("success","eq");jump("complete");
        label("mapped_move");p.Emit("clr b0 always");jump("mapped_transfer");
        label("mapped_clear");p.Emit("mov 0x0000 b0l",1);
        label("mapped_transfer");p.Emit("mov b0l [page:0x0054u8]");
        for(auto pair:std::vector<std::pair<unsigned,unsigned>>{{1,0x52},{2,0x50},{6,0x51}}){
            char op[80];std::snprintf(op,sizeof(op),"mov [page:0x%04xu8] b0l",pair.first);p.Emit(op);
            std::snprintf(op,sizeof(op),"mov b0l [page:0x%04xu8]",pair.second);p.Emit(op);
        }
        append(makeDmemTransfer());p.Emit("mov [page:0x0053u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("success","eq");jump("complete");
    }
    label("bad_state");p.Emit("mov 0x0000 b0l",4);jump("complete");
    label("bad_command");p.Emit("mov 0x0000 b0l",3);jump("complete");
    label("success");p.Emit("mov [page:0x0031u8] b0l");p.Emit("cmpv 0x0000 b0l",0);jump("dequeue","neq");p.Emit("clr b0 always");
    label("complete");p.Emit("mov b0l [page:0x0012u8]");p.Emit("mov [page:0x0003u8] b0l");p.Emit("mov b0l [page:0x0011u8]");p.Emit("clr b0 always");p.Emit("mov b0l [page:0x0000u8]");
    if(completionInterrupt && !mailboxOnly){
        // Publish status/sequence/busy before signaling DR0. Host must consume
        // the notification before another submission; loader/shutdown use DR2.
        p.Emit("mov [page:0x0003u8] b0l");p.Emit("load 0x0080u8 page");p.Emit("mov b0l [page:0x00c0u8]");
    }
    jump("idle");
    label("shutdown");p.Emit("clr b0 always");p.Emit("mov b0l [page:0x00c8u8]");label("stopped");jump("stopped");
    for(auto& fix:jumps){if(labels.at(fix.second)>65535)throw std::runtime_error("firmware too large");p.words[fix.first]=labels.at(fix.second);}
    return p;
}
