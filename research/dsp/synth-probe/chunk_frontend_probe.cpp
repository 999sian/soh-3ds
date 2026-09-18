#include <cstdio>
#include <stdexcept>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
#include "chunk_frontend.h"
#include "cpu_replay_dispatch.h"
extern "C" const int16_t* cpuResampleTable();
extern "C" unsigned cpuSnapshotSize();
extern "C" void cpuSnapshot(void*);
extern "C" int cpuRestore(const void*,unsigned);
using namespace ResidentDsp;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static CpuCall call(CpuOp op,std::initializer_list<uint32_t> args={},const void* ptr=nullptr){CpuCall c;c.op=op;std::copy(args.begin(),args.end(),c.args.begin());c.pointer=reinterpret_cast<uintptr_t>(ptr);return c;}
struct Cpu {
    unsigned calls=0;bool rejectApply=false;
    bool restore(const void* p,unsigned bytes){return cpuRestore(p,bytes);}
    void execute(const CpuCall& c,const void* p){++calls;executeCpuCall(c,p);}
    bool canApply(const DspMixerImage&)const{return !rejectApply;}
    void apply(const DspMixerImage& image){require(cpuRestore(&image,sizeof(image)),"apply size");}
};
struct Transport {
    Teakra::Teakra& dsp;bool event=false;unsigned flushes=0,failFlush=0;bool failFinalRead=false;
    explicit Transport(Teakra::Teakra& d):dsp(d){dsp.SetRecvDataHandler(0,[this](){event=true;});}
    uint16_t read(unsigned a){return dsp.DataRead(a);}
    void write(unsigned a,uint16_t v){dsp.DataWrite(a,v);}
    bool flush(unsigned a,unsigned n){return ++flushes!=failFlush && a<=0x7420 && n<=0x7420-a;}
    bool invalidate(unsigned a,unsigned n){return !(failFinalRead && a==AudioDmemWordBase && n==AudioDmemBytes/2) && a<=0x7420 && n<=0x7420-a;}
    Receive tryReceive(uint16_t& value){if(!event)return Receive::Pending;if(!dsp.RecvDataIsReady(0))return Receive::Error;value=dsp.RecvData(0);event=false;return Receive::Received;}
};
int main(){
    using Frontend=ChunkFrontend<Transport>;using Phase=Frontend::Phase;
    auto firmware=makeTypedFirmware(true,true,true,true);
    DspMixerImage baseline{};require(cpuSnapshotSize()==sizeof(baseline),"bridge layout");cpuSnapshot(&baseline);
    // Populate nonzero untouched globals and DMEM to test initial staging and
    // final CPU reconciliation, rather than assuming every chunk clears DMEM.
    baseline.in=0x400;baseline.out=0x600;baseline.nbytes=16;baseline.vol[0]=2345;baseline.vol[1]=4321;
    baseline.filter_count=16;for(unsigned i=0;i<8;++i)baseline.filter[i]=i*77;
    for(unsigned i=0;i<1536;++i)baseline.buf.as_s16[i]=int(i*17)-12000;
    unsigned success=0,recovered=0,fallback=0;
    for(unsigned mode=0;mode<10;++mode){
        Teakra::Teakra dsp({});for(unsigned i=0;i<firmware.words.size();++i)dsp.ProgramWrite(i,firmware.words[i]);dsp.Run(700);
        require(dsp.RecvDataIsReady(2),"boot");dsp.RecvData(2);
        // Required resident arithmetic lookup tables, initialized once per boot.
        for(unsigned i=0;i<256;++i)dsp.DataWrite(0x4000+i,uint16_t(cpuResampleTable()[i]));
        for(unsigned i=0;i<16;++i)dsp.DataWrite(0x2800+i,uint16_t(1u<<i));
        Transport io(dsp);Session<Transport> session(io);require(session.attach(0x4458),"session");
        ChunkCapture capture;StateTransaction states;CpuReplay replay;Frontend frontend(io,session,capture,states,replay);Cpu cpu;
        std::array<int16_t,128> input{},book{};for(unsigned i=0;i<128;++i){input[i]=int(i*251)-14000;book[i]=int(i*3)-100;}
        std::array<int16_t,16> state{},loop{},decoder{},resampler{},s8{};for(unsigned i=0;i<16;++i)loop[i]=i*11;
        std::array<int16_t,8> coeff={1000,-3000,4000,12000,-10000,2000,500,700};
        std::array<uint8_t,64> host{};for(unsigned i=0;i<64;++i)host[i]=i*23;auto oldHost=host;
        std::array<uint8_t,32> output{};output.fill(0x55);auto oldOutput=output;
        std::array<uint8_t,18> compressed{};for(unsigned i=0;i<18;++i)compressed[i]=i*23;compressed[0]=0x21;compressed[9]=0x32;
        auto initial=baseline;initial.adpcm_loop_state=reinterpret_cast<uintptr_t>(loop.data());
        std::vector<CpuCall> calls={
            call(CpuOp::Load,{0x3c1,255},input.data()),call(CpuOp::Buffer,{0,0x3e0,0x600,32}),
            call(CpuOp::Book,{16},book.data()),call(CpuOp::Loop,{},loop.data()),
            call(CpuOp::Env1,{170,300,400,500}),call(CpuOp::Env2,{30000,20000}),
            call(CpuOp::Envelope,{0x3c0,16,21,0x90a0b0c0,0}),
            call(CpuOp::Mix,{2,0x4000,0x3c0,0x900}),call(CpuOp::Add,{32,0x900,0xa00}),
            call(CpuOp::Duplicate,{0,0x3c1,0x740}),call(CpuOp::Zoh,{0x4000,0}),call(CpuOp::Interl,{0x700,0x840,8}),
            call(CpuOp::Filter,{2,32},coeff.data()),call(CpuOp::Filter,{1,0x900},state.data()),
            call(CpuOp::HiLo,{8,0,0x900}),call(CpuOp::TableMultiply,{1,64,0xa00,0x3c0}),call(CpuOp::Move,{0x900,0x901,16}),
            call(CpuOp::Interleave,{0xd00,0x900,0xa00,32}),call(CpuOp::Save,{0xd00,31},host.data()+8),
            call(CpuOp::Save,{0xd10,16},host.data()+12),call(CpuOp::Load,{0xe00,31},host.data()+4),call(CpuOp::Save,{0xe00,32},output.data()),
            call(CpuOp::Noop3,{1,2,3})
        };
        calls.insert(calls.begin()+6,{
            call(CpuOp::Load,{0xdc0,18},compressed.data()),call(CpuOp::Buffer,{0,0xdc0,0x3c0,64}),call(CpuOp::Adpcm,{2},decoder.data()),
            call(CpuOp::Buffer,{0,0x3e0,0x600,32}),call(CpuOp::Resample,{1,0x4000},resampler.data()),
            call(CpuOp::Buffer,{0,0xdc0,0x700,32}),call(CpuOp::S8,{1},s8.data()),call(CpuOp::Buffer,{0,0x3e0,0x600,32})
        });
        if(mode==5)calls.insert(calls.begin()+22,call(CpuOp::Filter,{2,16},state.data()+8)); // metadata depends on DSP state
        if(mode==6)calls.insert(calls.begin()+22,call(CpuOp::Load,{0x600,32},state.data())); // deferred state-to-load alias rejected at seal
        if(mode==7)for(unsigned i=0;i<200;++i)calls.insert(calls.end()-1,call(CpuOp::Clear,{0x600,0})); // bounded DSP capture refusal
        if(mode==8)calls.insert(calls.end()-1,call(CpuOp::Book,{16},host.data()+8));
        require(cpuRestore(&initial,sizeof(initial)),"direct initial");
        for(const auto& c:calls)executeCpuCall(c,reinterpret_cast<const void*>(c.pointer));
        DspMixerImage expected{};cpuSnapshot(&expected);auto expectedState=state,expectedDecoder=decoder,expectedResampler=resampler,expectedS8=s8;auto expectedHost=host;auto expectedOutput=output;
        state.fill(0);decoder.fill(0);resampler.fill(0);s8.fill(0);host=oldHost;output=oldOutput;require(cpuRestore(&initial,sizeof(initial)) && frontend.begin(initial),"frontend begin");
        for(unsigned i=0;i<calls.size();++i){require(frontend.record(calls[i],cpu),"frontend record");if(mode==9 && i==6)require(frontend.flushToCpu(cpu),"unsupported operation CPU barrier");}
        require(frontend.seal(cpu),"frontend seal or prefix fallback");
        if(mode>=5){require(frontend.phase()==Phase::Cpu && cpu.calls==calls.size(),"unsupported dependency/capacity switches whole prefix and suffix to CPU");++fallback;}
        else{
            DspMixerImage unchanged{};cpuSnapshot(&unchanged);require(!std::memcmp(&unchanged,&initial,sizeof(initial)) && state==std::array<int16_t,16>{} && host==oldHost && output==oldOutput,"recording makes no CPU mutations");
            if(mode==1)io.failFlush=1;
            bool started=frontend.start(mode==2?1:10000);
            if(mode==1)require(!started && frontend.phase()==Phase::Fault,"stage fault");
            else{
                require(started,"start");if(mode==3)io.failFinalRead=true;if(mode==4)cpu.rejectApply=true;
                uint64_t tick=0;
                while(frontend.phase()==Phase::Dsp && tick<2000){
                    if(frontend.waiting() && mode!=2)dsp.Run(5000);
                    frontend.step(++tick,cpu);
                }
            }
            if(mode==0){require(frontend.phase()==Phase::Committed && cpu.calls==0,"entire supported chunk offloads without CPU kernels");++success;}
            else{
                require(frontend.phase()==Phase::Fault && state==std::array<int16_t,16>{} && host==oldHost && output==oldOutput,"fault withholds PCM/history");
                require(!frontend.recoverAfterStop(cpu),"cannot recover before Session shutdown");
                dsp.Run(50000);dsp.SendData(2,0x8000);dsp.Run(700);require(dsp.RecvDataIsReady(2) && dsp.RecvData(2)==0,"actual stop before replay");session.resetAfterStop();
                require(frontend.recoverAfterStop(cpu) && cpu.calls==calls.size(),"whole chunk replay after stop");++recovered;
            }
        }
        DspMixerImage actual{};cpuSnapshot(&actual);
        if(std::memcmp(&actual,&expected,sizeof(actual))){auto* a=reinterpret_cast<uint8_t*>(&actual);auto* e=reinterpret_cast<uint8_t*>(&expected);for(unsigned i=0;i<sizeof(actual);++i)if(a[i]!=e[i]){std::printf("mode%u image byte%u got%u expected%u\n",mode,i,a[i],e[i]);break;}throw std::runtime_error("complete CPU mixer image differs");}
        require(state==expectedState && decoder==expectedDecoder && resampler==expectedResampler && s8==expectedS8 && host==expectedHost && output==expectedOutput,"frontend PCM/state matches CPU");
        if(mode==0 || mode>=5){dsp.SendData(2,0x8000);dsp.Run(700);require(dsp.RecvDataIsReady(2) && dsp.RecvData(2)==0,"normal stop");}
    }
    {
        Teakra::Teakra dsp({});for(unsigned i=0;i<firmware.words.size();++i)dsp.ProgramWrite(i,firmware.words[i]);dsp.Run(700);
        require(dsp.RecvDataIsReady(2),"lifecycle boot");dsp.RecvData(2);
        Transport io(dsp);Session<Transport> session(io);require(session.attach(0x4458),"lifecycle attach");
        ChunkCapture capture;StateTransaction states;CpuReplay replay;Frontend frontend(io,session,capture,states,replay);Cpu cpu;
        std::array<int16_t,16> state{},input{},output{};for(unsigned i=0;i<16;++i)input[i]=int(i*700)-9000;
        std::array<int16_t,8> coeff={1000,-3000,4000,12000,-10000,2000,500,700};
        require(cpuRestore(&baseline,sizeof(baseline)),"lifecycle baseline");
        for(unsigned chunk=0;chunk<6;++chunk){
            DspMixerImage initial{};cpuSnapshot(&initial);auto oldState=state,oldOutput=output;
            std::vector<CpuCall> calls;
            if(chunk==1)calls={call(CpuOp::Env1,{100,200,300,400}),call(CpuOp::Env2,{20000,10000}),call(CpuOp::Book,{16},coeff.data())};
            if(chunk>=2){calls.push_back(call(CpuOp::Load,{0x900,32},input.data()));
                if(chunk==2)calls.push_back(call(CpuOp::Filter,{2,32},coeff.data()));
                calls.push_back(call(CpuOp::Filter,{chunk==2?1u:0u,0x900},state.data()));
                if(chunk==4)calls.push_back(call(CpuOp::Filter,{2,32},state.data()+8));
                calls.push_back(call(CpuOp::Save,{0x900,32},output.data()));
            }
            for(const auto& c:calls)executeCpuCall(c,reinterpret_cast<const void*>(c.pointer));
            DspMixerImage expected{};cpuSnapshot(&expected);auto expectedState=state,expectedOutput=output;
            state=oldState;output=oldOutput;require(cpuRestore(&initial,sizeof(initial)) && frontend.begin(initial),"lifecycle begin");unsigned before=cpu.calls;
            for(const auto& c:calls)require(frontend.record(c,cpu),"lifecycle record");require(frontend.seal(cpu),"lifecycle seal");
            if(chunk==4)require(frontend.phase()==Phase::Cpu && cpu.calls-before==calls.size(),"success to CPU fallback");
            else{require(frontend.start(10000),"lifecycle start");uint64_t tick=0;while(frontend.phase()==Phase::Dsp && tick<1000){if(frontend.waiting())dsp.Run(5000);frontend.step(++tick,cpu);}require(frontend.phase()==Phase::Committed && cpu.calls==before,"empty/metadata/continuation DSP execution");}
            DspMixerImage actual{};cpuSnapshot(&actual);require(!std::memcmp(&actual,&expected,sizeof(actual)) && state==expectedState && output==expectedOutput,"persistent global filter state and lifecycle PCM");
        }
        dsp.SendData(2,0x8000);dsp.Run(700);require(dsp.RecvDataIsReady(2) && dsp.RecvData(2)==0,"lifecycle stop");
    }
    std::printf("PASS: %u full frontend DSP chunk, %u coordinated failure/stop/CPU replays, %u metadata/alias/capacity fallbacks, initial/final full mixer globals and PCM/history versus production, empty/metadata chunks and success-to-CPU-to-DSP persistent filter continuation; %zu frontend bytes including runner\n",success,recovered,fallback,sizeof(Frontend));
}
