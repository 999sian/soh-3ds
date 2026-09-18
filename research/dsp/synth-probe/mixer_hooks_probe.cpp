#include <cstdio>
#include <stdexcept>
#include <vector>
#include "teakra/teakra.h"
#include "typed_firmware.h"
#include "chunk_frontend.h"
#include "cpu_replay_dispatch.h"
using namespace ResidentDsp;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
extern "C" int cpuTestGuard(const void* p,uint32_t n){uintptr_t a=reinterpret_cast<uintptr_t>(p);return a>=0x1000 && a<=UINTPTR_MAX-n;}
struct OggOpusFile {int64_t pos=0;};static OggOpusFile opus;static unsigned opusReads=0,opusFrees=0;
extern "C" OggOpusFile* op_open_memory(const unsigned char*,size_t,int*){opus.pos=0;return &opus;}
extern "C" int op_pcm_seek(OggOpusFile* p,int64_t pos){p->pos=pos;return 0;}
extern "C" int op_read(OggOpusFile* p,int16_t* out,int n,int*){++opusReads;for(int i=0;i<n;++i)out[i]=int16_t(p->pos*100+i);p->pos+=n;return n;}
extern "C" void op_free(OggOpusFile*){++opusFrees;}
extern "C" void aOPUSFree(OggOpusFile*);
struct Cpu {
    unsigned executed=0;
    bool restore(const void* p,unsigned n){return SohDspMixerWriteImage(static_cast<const DspMixerImage*>(p),n);}
    void execute(const CpuCall& c,const void* p){int previous=SohDspMixerSetCpuBypass(1);++executed;executeCpuCall(c,p);SohDspMixerSetCpuBypass(previous);}
    bool canApply(const DspMixerImage&)const{return true;}
    void apply(const DspMixerImage& image){require(SohDspMixerWriteImage(&image,sizeof(image)),"apply");}
};
struct Transport {
    Teakra::Teakra& dsp;bool event=false,fail=false;
    explicit Transport(Teakra::Teakra& d):dsp(d){dsp.SetRecvDataHandler(0,[this](){event=true;});}
    uint16_t read(unsigned a){return dsp.DataRead(a);}void write(unsigned a,uint16_t v){dsp.DataWrite(a,v);}
    bool flush(unsigned a,unsigned n){return !fail && a<=0x7420 && n<=0x7420-a;}
    bool invalidate(unsigned a,unsigned n){return a<=0x7420 && n<=0x7420-a;}
    Receive tryReceive(uint16_t& v){if(!event)return Receive::Pending;if(!dsp.RecvDataIsReady(0))return Receive::Error;v=dsp.RecvData(0);event=false;return Receive::Received;}
};
using Frontend=ChunkFrontend<Transport>;
struct Context {
    Frontend& frontend;Transport& io;Session<Transport>& session;Cpu& cpu;
    unsigned begins=0,ends=0,records=0,barriers=0;bool recordZero=false,recovered=false;
    static void begin(void* ptr){auto& x=*static_cast<Context*>(ptr);++x.begins;DspMixerImage image{};require(!SohDspMixerInstallHooks(nullptr,nullptr),"cannot replace active hooks");require(SohDspMixerReadImage(&image,sizeof(image)) && x.frontend.begin(image),"callback begin");}
    static void barrier(void* ptr){auto& x=*static_cast<Context*>(ptr);++x.barriers;require(x.frontend.flushToCpu(x.cpu),"barrier flush");}
    static int record(void* ptr,uint8_t op,const uint32_t args[5],uintptr_t host){auto& x=*static_cast<Context*>(ptr);++x.records;
        if(x.recordZero && op==SOH_DSP_OP_HiLo){barrier(ptr);return 0;}
        CpuCall c;c.op=CpuOp(op);std::copy(args,args+5,c.args.begin());c.pointer=host;require(x.frontend.record(c,x.cpu),"callback record");return 1;
    }
    static void end(void* ptr){auto& x=*static_cast<Context*>(ptr);++x.ends;require(x.frontend.seal(x.cpu),"callback seal");
        if(x.frontend.phase()==Frontend::Phase::Sealed){x.frontend.start(10000);uint64_t tick=0;
            while(x.frontend.phase()==Frontend::Phase::Dsp && tick<2000){if(x.frontend.waiting())x.io.dsp.Run(5000);x.frontend.step(++tick,x.cpu);}
            if(x.frontend.phase()==Frontend::Phase::Fault){x.io.dsp.Run(50000);x.io.dsp.SendData(2,0x8000);x.io.dsp.Run(700);require(x.io.dsp.RecvDataIsReady(2) && x.io.dsp.RecvData(2)==0,"failure stop");x.session.resetAfterStop();require(x.frontend.recoverAfterStop(x.cpu),"callback recovery");x.recovered=true;}
            else require(x.frontend.phase()==Frontend::Phase::Committed,"callback completion");
        }
    }
};
int main(){
    DspMixerImage baseline{};require(SohDspMixerReadImage(&baseline,sizeof(baseline)),"image");
    require(!SohDspMixerReadImage(nullptr,sizeof(baseline)) && !SohDspMixerWriteImage(&baseline,sizeof(baseline)-1),"image validation");
    auto firmware=makeTypedFirmware(true,true,true,true);
    for(unsigned mode=0;mode<6;++mode){
        Teakra::Teakra dsp({});for(unsigned i=0;i<firmware.words.size();++i)dsp.ProgramWrite(i,firmware.words[i]);dsp.Run(700);require(dsp.RecvDataIsReady(2),"boot");dsp.RecvData(2);
        for(unsigned i=0;i<256;++i)dsp.DataWrite(0x4000+i,uint16_t(SohDspMixerResampleTable()[i]));for(unsigned i=0;i<16;++i)dsp.DataWrite(0x2800+i,uint16_t(1u<<i));
        Transport io(dsp);Session<Transport> session(io);require(session.attach(0x4458),"attach");ChunkCapture capture;StateTransaction states;CpuReplay replay;Frontend frontend(io,session,capture,states,replay);Cpu cpu;Context context{frontend,io,session,cpu};
        SohDspMixerHooks hooks={Context::begin,Context::end,Context::barrier,Context::record};
        std::array<uint8_t,18> compressed{};for(unsigned i=0;i<18;++i)compressed[i]=i*23;compressed[0]=0x21;compressed[9]=0x32;
        std::array<int16_t,16> decoder{},resampler{},s8{},filter{},loop{};std::array<int16_t,8> coeff={1000,-3000,4000,12000,-10000,2000,500,700};
        std::array<int16_t,128> book{};std::array<int16_t,16> output{};OggOpusFile* opusState=nullptr;
        auto initial=baseline;initial.adpcm_loop_state=mode==2?uintptr_t(0x10):reinterpret_cast<uintptr_t>(loop.data());
        auto sequence=[&](){
            aLoadADPCMImpl(256,book.data());aLoadBufferImpl(compressed.data(),0xdc0,18);
            aSetBufferImpl(0,0xdc0,0x3c0,64);aADPCMdecImpl(2,decoder.data());
            aSetBufferImpl(0,0x3e0,0x600,32);aResampleImpl(1,0x4000,resampler.data());
            aSetBufferImpl(0,0xdc0,0x700,32);aS8DecImpl(2,s8.data());
            aFilterImpl(2,32,coeff.data());aFilterImpl(1,0x600,filter.data());
            if(mode==3){aLoadBufferImpl(nullptr,0x600,32);aLoadADPCMImpl(0,nullptr);}
            if(mode==4){aOPUSdecImpl(compressed.data(),0x600,32,&opusState,7,compressed.size());aOPUSFree(opusState);}
            aHiLoGainImpl(8,0,0x600);aSaveBufferImpl(0x600,output.data(),32);
        };
        require(SohDspMixerWriteImage(&initial,sizeof(initial)),"reference initial");sequence();DspMixerImage expected{};SohDspMixerReadImage(&expected,sizeof(expected));
        auto expectedD=decoder,expectedR=resampler,expectedS=s8,expectedF=filter,expectedOutput=output;
        decoder.fill(0);resampler.fill(0);s8.fill(0);filter.fill(0);output.fill(0);opusState=nullptr;
        require(SohDspMixerWriteImage(&initial,sizeof(initial)) && SohDspMixerInstallHooks(&hooks,&context),"install");
        context.recordZero=mode==1;io.fail=mode==5;
        SohDspMixerBeginChunk();SohDspMixerBeginChunk();sequence();
        if(mode==0 || mode==5){DspMixerImage unchanged{};SohDspMixerReadImage(&unchanged,sizeof(unchanged));require(!std::memcmp(&unchanged,&initial,sizeof(initial)) && output==std::array<int16_t,16>{},"real C hooks defer mixer bodies");}
        SohDspMixerEndChunk();SohDspMixerEndChunk();
        require(context.begins==1 && context.ends==1,"chunk callbacks once");
        DspMixerImage actual{};SohDspMixerReadImage(&actual,sizeof(actual));require(!std::memcmp(&actual,&expected,sizeof(actual)) && decoder==expectedD && resampler==expectedR && s8==expectedS && filter==expectedF && output==expectedOutput,"hooked full image PCM/history versus original guarded bodies");
        if(mode==0)require(frontend.phase()==Frontend::Phase::Committed && cpu.executed==0,"real C calls offloaded");
        if(mode>=1 && mode<=4)require(frontend.phase()==Frontend::Phase::Cpu && context.barriers>0,"pointer/OPUS/record0 barrier fallback");
        if(mode==5)require(context.recovered,"real hooked failure replay");
        int outer=SohDspMixerSetCpuBypass(1);require(!SohDspMixerInstallHooks(nullptr,nullptr),"install forbidden during bypass");int inner=SohDspMixerSetCpuBypass(1);require(inner==1 && outer==0,"nested bypass state");SohDspMixerSetCpuBypass(inner);SohDspMixerSetCpuBypass(outer);
        require(SohDspMixerInstallHooks(nullptr,nullptr),"uninstall");unsigned records=context.records;SohDspMixerBeginChunk();aUnkCmd3Impl(1,2,3);SohDspMixerEndChunk();require(context.records==records,"uninstalled hooks inert");
        if(!context.recovered){dsp.SendData(2,0x8000);dsp.Run(700);require(dsp.RecvDataIsReady(2) && dsp.RecvData(2)==0,"stop");}
    }
    require(opusReads==2 && opusFrees==2,"OPUS stub runs exactly once per direct/hooked path");
    std::puts("PASS: actual production C entry hooks drive DSP/frontend, guarded initial invalid loops and null sources preserve CPU behavior, OPUS decode/free barriers, record0 live suffix, stop/replay, bypass/install lifecycle and full PCM/state/globals");
}
