#include <cstdio>
#include <stdexcept>
#include <vector>
#include "cpu_replay_dispatch.h"
extern "C" unsigned cpuSnapshotSize();
extern "C" void cpuSnapshot(void*);
extern "C" int cpuRestore(const void*,unsigned);
using namespace ResidentDsp;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static CpuCall call(CpuOp op,std::initializer_list<uint32_t> args={},const void* pointer=nullptr){
    CpuCall c;c.op=op;c.pointer=reinterpret_cast<uintptr_t>(pointer);std::copy(args.begin(),args.end(),c.args.begin());return c;
}
struct Cpu {
    unsigned restores=0,calls=0;bool failRestore=false;
    bool restore(const void* p,unsigned n){++restores;return !failRestore && cpuRestore(p,n);}
    void execute(const CpuCall& c,const void* p){++calls;executeCpuCall(c,p);}
};
int main(){
    require(cpuSnapshotSize()<=CpuReplay::BaselineBytes,"full production mixer baseline fits");
    std::vector<uint8_t> baseline(cpuSnapshotSize()),expectedGlobal(cpuSnapshotSize()),actualGlobal(cpuSnapshotSize());
    cpuSnapshot(baseline.data());
    std::array<int16_t,16> decoder{},resample{},s8{},filter{},loop{};
    for(unsigned i=0;i<16;++i)loop[i]=int(i*27)-140;
    std::array<int16_t,128> book{},input{};for(unsigned i=0;i<128;++i){book[i]=int(i*7)-200;input[i]=int(i*313)-21000;}
    std::array<int16_t,8> partialBook={1,2,3,4,5,6,7,8},coeff={1000,-3000,4000,12000,-10000,2000,500,700};
    std::array<uint8_t,18> compressed{};for(unsigned i=0;i<18;++i)compressed[i]=i*13;compressed[0]=0x21;compressed[9]=0x32;
    std::array<uint8_t,64> host{};for(unsigned i=0;i<64;++i)host[i]=i*19;auto oldHost=host;
    std::array<uint8_t,32> output{};output.fill(0x55);auto oldOutput=output;
    std::vector<CpuCall> calls={
        call(CpuOp::Clear,{0x3c0,3072}),call(CpuOp::Load,{0x3c0,256},input.data()),
        call(CpuOp::Book,{256},book.data()),call(CpuOp::Book,{16},partialBook.data()),call(CpuOp::Loop,{},loop.data()),
        call(CpuOp::Load,{0xdc0,18},compressed.data()),call(CpuOp::Buffer,{0,0xdc0,0x3c0,64}),call(CpuOp::Adpcm,{2},decoder.data()),
        call(CpuOp::Load,{0x900,32},decoder.data()),call(CpuOp::Buffer,{0,0x3e0,0x600,32}),call(CpuOp::Resample,{1,0x4000},resample.data()),
        call(CpuOp::Buffer,{0,0xdc0,0x700,32}),call(CpuOp::S8,{1},s8.data()),
        call(CpuOp::Env1,{170,300,400,500}),call(CpuOp::Env2,{30000,20000}),call(CpuOp::Envelope,{0x600,16,21,0x90a0b0c0,0}),
        call(CpuOp::Mix,{2,0x4000,0x600,0x900}),call(CpuOp::Add,{32,0x900,0xa00}),call(CpuOp::Duplicate,{0,0x3c1,0x740}),
        call(CpuOp::Buffer,{0,0x600,0x800,16}),call(CpuOp::Zoh,{0x4000,0}),call(CpuOp::Interl,{0x700,0x840,8}),
        call(CpuOp::Filter,{2,32},coeff.data()),call(CpuOp::Filter,{1,0x900},filter.data()),
        call(CpuOp::Filter,{2,16},filter.data()+8),call(CpuOp::Filter,{0,0x900},filter.data()),
        call(CpuOp::HiLo,{8,0,0x900}),call(CpuOp::TableMultiply,{1,64,0xa00,0x3c0}),call(CpuOp::Move,{0x900,0x901,16}),
        call(CpuOp::Interleave,{0xd00,0x900,0xa00,32}),call(CpuOp::Save,{0xd00,31},host.data()+8),
        call(CpuOp::Save,{0xd10,16},host.data()+12),call(CpuOp::Load,{0xe00,31},host.data()+4),call(CpuOp::Save,{0xe00,32},output.data()),
        call(CpuOp::Noop3,{1,2,3})
    };
    CpuReplay replay;require(replay.begin(baseline.data(),baseline.size()),"begin");unsigned covered=0;
    for(const auto& c:calls){require(replay.append(c),"record original calls");covered|=1u<<unsigned(c.op);}
    require(covered==(1u<<(unsigned(CpuOp::TableMultiply)+1))-1,"every dispatcher operation covered");
    Cpu cpu;
    // Independent expected path calls the named production entry points; it
    // must not share the dispatcher whose argument mapping is under test.
    aClearBufferImpl(0x3c0,3072);aLoadBufferImpl(input.data(),0x3c0,256);
    aLoadADPCMImpl(256,book.data());aLoadADPCMImpl(16,partialBook.data());aSetLoopImpl(reinterpret_cast<ADPCM_STATE*>(loop.data()));
    aLoadBufferImpl(compressed.data(),0xdc0,18);aSetBufferImpl(0,0xdc0,0x3c0,64);aADPCMdecImpl(2,decoder.data());
    aLoadBufferImpl(decoder.data(),0x900,32);aSetBufferImpl(0,0x3e0,0x600,32);aResampleImpl(1,0x4000,resample.data());
    aSetBufferImpl(0,0xdc0,0x700,32);aS8DecImpl(1,s8.data());
    aEnvSetup1Impl(170,300,400,500);aEnvSetup2Impl(30000,20000);aEnvMixerImpl(0x600,16,true,false,true,false,true,int32_t(0x90a0b0c0),0);
    aMixImpl(2,0x4000,0x600,0x900);aAddMixerImpl(32,0x900,0xa00);aDuplicateImpl(0,0x3c1,0x740);
    aSetBufferImpl(0,0x600,0x800,16);aResampleZohImpl(0x4000,0);aInterlImpl(0x700,0x840,8);
    aFilterImpl(2,32,coeff.data());aFilterImpl(1,0x900,filter.data());aFilterImpl(2,16,filter.data()+8);aFilterImpl(0,0x900,filter.data());
    aHiLoGainImpl(8,0,0x900);aUnkCmd19Impl(1,64,0xa00,0x3c0);aDMEMMoveImpl(0x900,0x901,16);
    aInterleaveImpl(0xd00,0x900,0xa00,32);aSaveBufferImpl(0xd00,reinterpret_cast<int16_t*>(host.data()+8),31);
    aSaveBufferImpl(0xd10,reinterpret_cast<int16_t*>(host.data()+12),16);aLoadBufferImpl(host.data()+4,0xe00,31);
    aSaveBufferImpl(0xe00,reinterpret_cast<int16_t*>(output.data()),32);aUnkCmd3Impl(1,2,3);
    cpuSnapshot(expectedGlobal.data());auto expectedDecoder=decoder,expectedResample=resample,expectedS8=s8,expectedFilter=filter;
    auto expectedHost=host;auto expectedOutput=output;
    decoder.fill(0);resample.fill(0);s8.fill(0);filter.fill(0);host=oldHost;output=oldOutput;
    input.fill(0x7777);book.fill(0x1111);partialBook.fill(0x2222);coeff.fill(0x3333);compressed.fill(0x44);
    // Simulate the CPU mixer being reused/touched before a failed DSP attempt.
    aClearBufferImpl(0x3c0,3072);aEnvSetup2Impl(1,2);
    require(replay.seal() && replay.markDspOwned(),"DSP ownership");unsigned before=cpu.calls;
    require(!replay.replay(cpu) && cpu.calls==before,"cannot CPU replay while DSP may run");
    require(replay.stopped() && replay.replay(cpu),"ordered replay after stop");cpuSnapshot(actualGlobal.data());
    require(actualGlobal==expectedGlobal,"full mixer DMEM/book/env/filter/buffer/loop baseline and outcome");
    require(decoder==expectedDecoder && resample==expectedResample && s8==expectedS8 && filter==expectedFilter,"CPU history writes");
    require(host==expectedHost && output==expectedOutput,"overlapping save and dependent load output");
    require(!replay.replay(cpu) && cpu.calls-before==calls.size(),"exactly once");
    // Refused current call is excluded; the retained prefix is still executable.
    require(replay.begin(baseline.data(),baseline.size()),"reuse");
    for(unsigned i=0;i<CpuReplay::MaxCalls;++i)require(replay.append(call(CpuOp::Noop3,{i,2,3})),"fill journal");
    auto suffix=call(CpuOp::Env2,{12345,23456});require(!replay.append(suffix) && replay.error()==CpuReplay::Error::Capacity,"overflow refuses only current call");
    before=cpu.calls;require(replay.replay(cpu) && cpu.calls-before==CpuReplay::MaxCalls,"overflow prefix replay");cpu.execute(suffix,nullptr);
    cpuSnapshot(actualGlobal.data());require(cpuRestore(baseline.data(),baseline.size()),"direct baseline");executeCpuCall(suffix,nullptr);cpuSnapshot(expectedGlobal.data());require(actualGlobal==expectedGlobal,"live suffix executes after prefix");
    require(replay.begin(baseline.data(),baseline.size()),"source capacity begin");std::array<uint8_t,3072> large{};
    for(unsigned i=0;i<4;++i)require(replay.append(call(CpuOp::Load,{0x3c0,3072},large.data())),"source arena fill");
    require(!replay.append(call(CpuOp::Load,{0x3c0,1},large.data())) && replay.size()==4,"source overflow preserves prefix");
    cpu.failRestore=true;before=cpu.calls;require(!replay.replay(cpu) && cpu.calls==before,"restore failure cannot publish");cpu.failRestore=false;
    require(replay.replay(cpu),"safe restore retry");
    require(replay.begin(baseline.data(),baseline.size()) && !replay.append(call(CpuOp::Load,{0x3c0,3073},large.data())) && replay.size()==0,"oversized load routes CPU");
    require(!replay.append(call(CpuOp::Load,{0x3c0,16},reinterpret_cast<void*>(UINTPTR_MAX-8))) && replay.size()==0,"source wrap rejected");
    require(!replay.append(call(CpuOp::Filter,{256,16},filter.data())) && !replay.append(call(CpuOp::Save,{0x3c0,65536},output.data())) && !replay.append(call(CpuOp::Envelope,{0x3c0,16,32,0,0})) && replay.size()==0,"narrowing cannot change dependency kind or extent");
    require(replay.seal() && replay.markDspOwned() && replay.acceptDsp() && !replay.replay(cpu),"successful DSP cannot replay");
    std::printf("PASS: all23 original mixer operations, complete global baseline, source snapshots, partial books, history/read dependencies, overlapping saves, DSP stop gate, prefix capacity fallback, exactly-once replay; %zu journal bytes\n",sizeof(CpuReplay));
}
