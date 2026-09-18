#include <cstdio>
#include <cstring>
#include <array>
#include <vector>
#include "teakra/teakra.h"
#include "teakra/impl/register.h"
#include "dmem_transfer_program.h"
extern "C" void dmemReference(uint8_t*,unsigned,unsigned,unsigned,unsigned);
int main(){
    ResidentDsp::DmemTransfer lowered{};
    for(int bytes:{-1,0,1,15,16,17,3071,3072,3073,65535,2147483647}){
        for(unsigned address:{0u,0x3bfu,0x3c0u,0x3c1u,0xfb0u,0xfc0u,0xffffu}){
            auto before=lowered;
            bool expected=bytes>=0 && bytes<=3072 && address>=0x3c0 && address<=0xfc0 &&
                unsigned((bytes+15)&~15)<=0xfc0-address;
            bool ok=ResidentDsp::lowerDmemTransfer(false,address,address,bytes,lowered);
            if(ok!=expected || (!ok && std::memcmp(&before,&lowered,sizeof(lowered))))return 4;
            if(ok && (lowered.source!=address-0x3c0 || lowered.destination!=address-0x3c0 || lowered.bytes!=unsigned((bytes+15)&~15) || lowered.operation))return 5;
        }
    }
    if(!ResidentDsp::lowerDmemTransfer(true,0xffff,0x3c1,1,lowered) || lowered.source || lowered.destination!=1 || lowered.bytes!=16 || lowered.operation!=1)return 6;
    auto p=makeDmemTransfer();Teakra::Teakra dsp({});
    for(unsigned i=0;i<p.words.size();++i)dsp.ProgramWrite(i,p.words[i]);
    uint32_t rng=0x1ca374;auto next=[&](){rng=rng*1664525u+1013904223u;return uint16_t(rng>>16);};
    std::vector<uint16_t> memory(0x5800);unsigned jobs=0;
    auto run=[&](unsigned op,unsigned src,unsigned dst,unsigned bytes,bool production){
        for(auto& x:memory)x=next();
        unsigned count=production?(bytes+15)&~15u:bytes;
        memory[0x50]=src;memory[0x51]=dst;memory[0x52]=count;memory[0x53]=0xffff;memory[0x54]=op;
        for(unsigned i=0;i<memory.size();++i)dsp.DataWrite(i,memory[i]);
        bool valid=op<=1 && count<=0xc00 && dst<=0xc00 && dst+count<=0xc00 && (op==1 || (src<=0xc00 && src+count<=0xc00));
        auto* data=reinterpret_cast<uint8_t*>(memory.data()+0x5000);
        if(valid){
            if(production)dmemReference(data,op,src,dst,bytes);
            else if(op==1)std::memset(data+dst,0,count);
            else std::memmove(data+dst,data+src,count);
        }
        memory[0x53]=valid?0:3;
        dsp.GetRegisterState().pc=0;dsp.Run(valid && !(count&1) && !(dst&1) && (op==1 || !(src&1))?40000:350000);
        for(unsigned i=0;i<memory.size();++i)if(dsp.DataRead(i)!=memory[i]){
            std::printf("FAIL op%u src%u dst%u bytes%u count%u production%d addr%x got%x expected%x\n",op,src,dst,bytes,count,production,i,dsp.DataRead(i),memory[i]);return false;
        }
        ++jobs;return true;
    };
    for(unsigned bytes:{0u,1u,2u,7u,8u,15u,16u,17u,31u,32u,127u,128u,160u,176u,192u,512u,1024u}){
        for(unsigned op:{0u,1u})for(bool production:{false,true}){
            for(auto pair:std::array<std::array<unsigned,2>,13>{{{0,2},{2,0},{0,1024},{1024,0},{0,0},{0,1},{1,0},{1,2},{2,1},{3,3},{0,1025},{1025,0},{1026,1025}}})
                if(!run(op,pair[0],pair[1],bytes,production))return 1;
        }
    }
    for(unsigned op:{0u,1u,2u,65535u})for(unsigned count:{0u,1u,16u,3071u,3072u,3073u,65535u}){
        for(auto pair:std::array<std::array<unsigned,2>,7>{{{0,0},{0,3072},{3072,0},{0,3071},{3071,0},{65535,0},{0,65535}}})
            if(!run(op,pair[0],pair[1],count,false))return 2;
    }
    std::printf("PASS: %u mapped DSP byte transfer jobs, production rounding, odd endpoints, both overlap directions, zero/full DMEM and preflight guards; %zu words\n",jobs,p.words.size());
}
