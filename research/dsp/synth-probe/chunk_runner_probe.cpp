#include <cstdio>
#include <stdexcept>
#include "teakra/teakra.h"
#include "typed_firmware.h"
#include "chunk_runner.h"
extern "C" void hiLoReference(int16_t*,unsigned,unsigned,unsigned);
struct Transport {
    Teakra::Teakra& dsp;bool event=false,failSave=false,failReceive=false,payloadDirty=false;unsigned flushes=0,invalidates=0,writes=0,failFlush=0,failInvalidate=0;
    explicit Transport(Teakra::Teakra& d):dsp(d){dsp.SetRecvDataHandler(0,[this](){event=true;});}
    uint16_t read(unsigned word){return dsp.DataRead(word);}
    void write(unsigned word,uint16_t value){++writes;if(word>=0x4100 && word<0x5000)payloadDirty=true;dsp.DataWrite(word,value);}
    bool flush(unsigned word,unsigned count){++flushes;if(word>=0x4100 && word<0x5000)payloadDirty=false;return word<=0x7420 && count<=0x7420-word && flushes!=failFlush;}
    bool invalidate(unsigned word,unsigned count){++invalidates;if(word>=0x4100 && word<0x5000 && payloadDirty)throw std::runtime_error("invalidating dirty payload before flush");return invalidates!=failInvalidate && word<=0x7420 && count<=0x7420-word && !(failSave && word>=0x5000 && word<0x5600);}
    ResidentDsp::Receive tryReceive(uint16_t& value){
        if(failReceive)return ResidentDsp::Receive::Error;
        if(!event)return ResidentDsp::Receive::Pending;
        if(!dsp.RecvDataIsReady(0))return ResidentDsp::Receive::Error;
        value=dsp.RecvData(0);event=false;return ResidentDsp::Receive::Received;
    }
};
using namespace ResidentDsp;using Runner=ChunkRunner<Transport>;using Phase=Runner::Phase;
static_assert(sizeof(Runner)<8192,"runner scratch exceeded8KiB");
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){
    auto firmware=makeTypedFirmware(true,true,true,true);
    auto boot=[&](Teakra::Teakra& dsp,Session<Transport>& session,std::vector<int16_t>& memory){
        for(unsigned i=0;i<firmware.words.size();++i)dsp.ProgramWrite(i,firmware.words[i]);
        uint32_t rng=0x399b184;for(auto& v:memory){rng=rng*1664525u+1013904223u;v=int16_t(rng>>16);}
        for(unsigned i=0;i<memory.size();++i)dsp.DataWrite(i,uint16_t(memory[i]));
        dsp.Run(700);require(dsp.RecvDataIsReady(2),"boot reply");dsp.RecvData(2);require(session.attach(0x4458),"session attach");
    };
    auto drive=[&](Teakra::Teakra& dsp,Runner& runner,uint64_t& tick){
        for(unsigned i=0;i<2000;++i){auto phase=runner.phase();if(phase==Phase::Collected || phase==Phase::Fault)return phase;if(phase==Phase::Waiting)dsp.Run(5000);runner.step(++tick);}
        throw std::runtime_error("bounded test driver exhausted");
    };
    {
        Teakra::Teakra dsp({});Transport io(dsp);Session<Transport> session(io);ChunkCapture capture;Runner runner(io,session,capture);
        std::vector<int16_t> expected(0x7500);boot(dsp,session,expected);
        std::array<uint8_t,128> host{};for(unsigned i=0;i<host.size();++i)host[i]=uint8_t(i*17);auto originalHost=host,expectedHost=host;
        std::array<uint8_t,63> a{};for(unsigned i=0;i<a.size();++i)a[i]=uint8_t(i*39);auto originalA=a;
        std::array<uint8_t,2> b={0xee,0x19};std::array<uint8_t,32> output;output.fill(0xcc);auto originalOutput=output;
        require(capture.load(a.data(),0x3c1,a.size()) && capture.load(b.data(),0x3c0,b.size()),"adjacent load capture");
        Command c;MappedParameters p;
        for(unsigned gain:{16u,8u}){require(lowerMappedHiLo(7,gain,0,0x3c0,c,p) && capture.append(c,&p),"duplicate parameter slot capture");}
        require(capture.save(0x3c1,host.data()+8,31) && capture.save(0x3d0,host.data()+12,16),"overlapping saves");
        require(capture.load(host.data()+4,0x440,31),"dependent load capture");
        require(lowerMappedHiLo(7,8,0,0x440,c,p) && capture.append(c,&p),"dependent gain");
        for(unsigned i=0;i<35;++i)require(capture.append({8,0,0,0}),"batch split no-op");
        require(capture.save(0x440,output.data(),32) && capture.seal(),"final save");
        auto* bytes=reinterpret_cast<uint8_t*>(expected.data());unsigned dmem=AudioDmemWordBase*2;
        std::memcpy(bytes+dmem+1,originalA.data(),originalA.size());std::memcpy(bytes+dmem,b.data(),b.size());
        hiLoReference(expected.data(),16,0,dmem);hiLoReference(expected.data(),8,0,dmem);
        std::memcpy(expectedHost.data()+8,bytes+dmem,16);std::memcpy(expectedHost.data()+12,bytes+dmem+16,16);
        std::memcpy(bytes+dmem+128,expectedHost.data()+4,31);hiLoReference(expected.data(),8,0,dmem+128);
        std::array<uint8_t,32> expectedOutput;std::memcpy(expectedOutput.data(),bytes+dmem+128,32);
        std::memcpy(bytes+PayloadWordBase*2,capture.payloadData(),capture.payloadBytes());
        std::memcpy(bytes+PayloadWordBase*2+65,expectedHost.data()+4,31);
        a.fill(0x99);require(runner.start(10000),"runner start");uint64_t tick=0;
        require(runner.step(tick)==Phase::Waiting && capture.phase()==ChunkCapture::Phase::Running,"submit without waiting");
        unsigned writes=io.writes;require(runner.step(++tick)==Phase::Waiting && io.writes==writes,"pending poll no shared writes");
        require(!capture.commitSaves() && host==originalHost && output==originalOutput,"no early publication");
        require(drive(dsp,runner,tick)==Phase::Collected,"captured DSP execution");
        require(runner.batches()==3 && runner.loads()==3 && runner.saves()==3,"bounded batch/save stats");
        require(host==originalHost && output==originalOutput,"tentative outputs remain private");
        for(unsigned i=0x400;i<expected.size();++i){if(i>=ParameterWordBase && i<ParameterWordBase+ParameterSlots*ParameterWords)continue;require(dsp.DataRead(i)==uint16_t(expected[i]),"DSP PCM/payload/full memory guards");}
        require(capture.commitSaves() && host==expectedHost && output==expectedOutput,"final ordered publication");
        require(capture.nextChunk() && capture.seal() && runner.start(1000) && runner.step(++tick)==Phase::Collected,"next empty chunk");
    }
    // All transfer failure positions include partial shared publication; a late
    // DSP completion cannot make any staged host output visible after failure.
    for(unsigned failure=1;failure<=5;++failure){
        Teakra::Teakra dsp({});Transport io(dsp);Session<Transport> session(io);ChunkCapture capture;Runner runner(io,session,capture);
        std::vector<int16_t> memory(0x7500);boot(dsp,session,memory);std::array<uint8_t,32> source{},host{};auto old=host;
        Command c;MappedParameters p;require(capture.load(source.data(),0x3c0,32) && lowerMappedHiLo(7,16,0,0x3c0,c,p) && capture.append(c,&p) && capture.save(0x3c0,host.data(),32) && capture.seal(),"fault fixture");
        require(runner.start(1000),"fault start");io.failFlush=failure;require(runner.step(0)==Phase::Fault,"injected transfer failure");
        dsp.Run(50000);require(runner.step(1)==Phase::Fault && !capture.commitSaves() && host==old && !capture.resetBeforeSubmit(),"late failure stays private");
        dsp.SendData(2,0x8000);dsp.Run(700);require(dsp.RecvDataIsReady(2) && dsp.RecvData(2)==0,"fault shutdown");
        require(!runner.resetAfterStop(),"require lifecycle reset first");session.resetAfterStop();capture.resetAfterStop();require(runner.resetAfterStop(),"ordered fault reset");
    }
    for(unsigned fault=0;fault<3;++fault){
        Teakra::Teakra dsp({});Transport io(dsp);Session<Transport> session(io);ChunkCapture capture;Runner runner(io,session,capture);
        std::vector<int16_t> memory(0x7500);boot(dsp,session,memory);std::array<uint8_t,32> source{},host{};auto old=host;
        require(capture.load(source.data(),0x3c0,32) && capture.save(0x3c0,host.data(),32),"runtime fixture");
        if(fault==2){MappedParameters invalid{};invalid[1]=256;require(capture.append({22,8,7,0},&invalid),"reject after tentative save");}
        require(capture.seal() && runner.start(fault==0?1:10000),"runtime start");
        require(runner.step(0)==Phase::Waiting,"runtime submit");uint64_t tick=0;
        if(fault==0){require(runner.step(1)==Phase::Fault,"timeout");dsp.Run(50000);}
        else{if(fault==1)io.failSave=true;require(drive(dsp,runner,tick)==Phase::Fault,"readback or DSP rejection");}
        require(!capture.commitSaves() && host==old,"runtime failure preserves host");
        if(fault==2)require(runner.saves()==1 && session.error()==Failure::Rejected,"partial save remains tentative");
    }
    // Publication/cache ordering and completion transport errors must withhold
    // all host writes, including when firmware has already completed.
    for(unsigned fault=0;fault<3;++fault){
        Teakra::Teakra dsp({});Transport io(dsp);Session<Transport> session(io);ChunkCapture capture;Runner runner(io,session,capture);
        std::vector<int16_t> memory(0x7500);boot(dsp,session,memory);
        std::array<uint8_t,32> source{},host{};host.fill(0xa5);auto old=host;
        require(capture.load(source.data(),0x3c0,32) && capture.save(0x3c0,host.data(),32) && capture.seal() && runner.start(1000),"cache fault fixture");
        if(fault==0)io.failInvalidate=io.invalidates+1;
        if(fault==0)require(runner.step(0)==Phase::Fault && io.writes==0,"payload invalidate failure precedes writes");
        else{
            require(runner.step(0)==Phase::Waiting,"completion fault submit");dsp.Run(50000);
            if(fault==1)io.failInvalidate=io.invalidates+1;else io.failReceive=true;
            require(runner.step(1)==Phase::Fault,"completion invalidate or receive error");
        }
        require(!capture.commitSaves() && host==old,"cache completion fault withholds output");
        dsp.SendData(2,0x8000);dsp.Run(700);require(dsp.RecvDataIsReady(2) && dsp.RecvData(2)==0,"cache fault shutdown");
        session.resetAfterStop();capture.resetAfterStop();require(runner.resetAfterStop(),"cache fault recovery");
    }
    printf("PASS: captured DSP chunks match CPU PCM, duplicate parameter slots remapped, odd adjacent loads, overlapping save dependencies,32-command splits, no early publication,5 flush faults, payload/completion invalidation and receive faults, timeout/readback/rejection, cache ordering and recovery; %zu runner bytes\n",sizeof(Runner));
}
