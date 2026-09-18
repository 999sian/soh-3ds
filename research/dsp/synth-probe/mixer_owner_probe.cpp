#include <cstdio>
#include <stdexcept>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>
#include <sys/wait.h>
#include <unistd.h>
#include "teakra/teakra.h"
#include <fstream>
#include "mapped_bootstrap.h"
#include "mixer_owner.h"
#include "cpu_replay_dispatch.h"
using namespace ResidentDsp;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
extern "C" int cpuTestGuard(const void* p,uint32_t n){auto a=reinterpret_cast<uintptr_t>(p);return a>=0x1000 && a<=UINTPTR_MAX-n;}
struct OggOpusFile {};
extern "C" OggOpusFile* op_open_memory(const unsigned char*,size_t,int*){std::abort();}
extern "C" int op_pcm_seek(OggOpusFile*,int64_t){std::abort();}
extern "C" int op_read(OggOpusFile*,int16_t*,int,int*){std::abort();}
extern "C" void op_free(OggOpusFile*){std::abort();}
struct Transport {
    Teakra::Teakra& dsp;bool event=false,fail=false;
    explicit Transport(Teakra::Teakra& d):dsp(d){dsp.SetRecvDataHandler(0,[this](){event=true;});}
    uint16_t read(unsigned a){return dsp.DataRead(a);}void write(unsigned a,uint16_t v){dsp.DataWrite(a,v);}
    bool flush(unsigned a,unsigned n){return !fail && a<=0x7420 && n<=0x7420-a;}
    bool invalidate(unsigned a,unsigned n){return a<=0x7420 && n<=0x7420-a;}
    Receive tryReceive(uint16_t& v){
#ifdef SOH3DS_DSP_MAILBOX_COMPLETION
        require(!event && !dsp.RecvDataIsReady(0),"mailbox firmware must not signal DR0");
        if(read(0)!=0)return Receive::Pending;
        v=read(0x11);return Receive::Received;
#else
        if(!event)return Receive::Pending;if(!dsp.RecvDataIsReady(0))return Receive::Error;v=dsp.RecvData(0);event=false;return Receive::Received;
#endif
    }
};
struct Life {
    Transport& io;Session<Transport>& session;const std::vector<uint16_t>& code;
    std::mutex mutex;bool held=false,dsp=false,output=false,ndsp=true;
    bool bootFail=false,outputFail=false,ndspFail=false,waitFail=false,stopDspFail=false,stopOutputFail=false,stopNdspFail=false,unhealthy=false;
    bool expireBeforeWait=false,timeoutWait=false,cancelOnWait=false,unhealthyOnWait=false,cancelled_=false;
    uint64_t ticks=0;std::vector<std::string> events;
    Life(Transport& t,Session<Transport>& s,const std::vector<uint16_t>& c):io(t),session(s),code(c){}
    void lock(){mutex.lock();require(!held,"nonrecursive owner lock");held=true;}
    void unlock(){require(held,"owned unlock");held=false;mutex.unlock();}
    void event(const char* name){require(held,"lifecycle under lock");events.push_back(name);}
    bool startDsp(){
        event("startDSP");require(!ndsp && !dsp,"no dual firmware");io.dsp.Reset();io.event=false;io.fail=false;
        for(unsigned i=0;i<code.size();++i)io.dsp.ProgramWrite(i,code[i]);
        io.dsp.Run(700);require(io.dsp.RecvDataIsReady(2),"boot reply");io.dsp.RecvData(2);dsp=true;
        require(session.attach(0x4458) && initializeMappedTables(io,session,SohDspMixerResampleTable()),"bootstrap");return !bootFail;
    }
    bool stopDsp(){
        event("stopDSP");if(stopDspFail)return false;
        if(dsp){io.dsp.Run(50000);io.dsp.SendData(2,0x8000);io.dsp.Run(700);require(io.dsp.RecvDataIsReady(2) && io.dsp.RecvData(2)==0,"actual firmware stop");dsp=false;io.event=false;}
        return true;
    }
    bool startOutput(){event("startOutput");require(dsp && !ndsp,"custom output firmware");output=true;return !outputFail;}
    bool stopOutput(){event("stopOutput");if(stopOutputFail)return false;output=false;return true;}
    bool startNdsp(){event("startNDSP");require(!dsp && !output,"NDSP after both custom owners stop");ndsp=true;return !ndspFail;}
    bool stopNdsp(){event("stopNDSP");if(stopNdspFail)return false;ndsp=false;return true;}
    uint64_t now(){if(expireBeforeWait && session.state()==State::Running){expireBeforeWait=false;ticks+=10000;}return ++ticks;}
    bool wait(uint64_t n){event("wait");require(n>0 && session.state()==State::Running,"wait actual submission");if(waitFail)return false;
        if(timeoutWait){ticks+=n;return true;}
        if(cancelOnWait){cancelled_=true;return true;}
        if(unhealthyOnWait){unhealthy=true;return true;}
        io.dsp.Run(5000);return true;}
    bool cancelled(){return cancelled_;}bool outputHealthy(){return !unhealthy;}
    [[noreturn]] void fatal(const char*){_exit(77);}
};
struct Cpu {
    Life& life;unsigned executed=0;
    bool read(DspMixerImage& image){return SohDspMixerReadImage(&image,sizeof(image));}
    bool restore(const void* p,unsigned n){life.event("replay");require(life.session.state()!=State::Running,"no replay while submitted");return SohDspMixerWriteImage(static_cast<const DspMixerImage*>(p),n);}
    void execute(const CpuCall& c,const void* p){int previous=SohDspMixerSetCpuBypass(1);++executed;executeCpuCall(c,p);SohDspMixerSetCpuBypass(previous);}
    bool canApply(const DspMixerImage&)const{return true;}
    void apply(const DspMixerImage& image){require(SohDspMixerWriteImage(&image,sizeof(image)),"apply");}
};
using Owner=MixerOwner<Transport,Cpu,Life>;
static const SohDspMixerHooks hooks={
    [](void* p){static_cast<Owner*>(p)->begin();},[](void* p){static_cast<Owner*>(p)->end();},
    [](void* p){static_cast<Owner*>(p)->barrier();},
    [](void* p,uint8_t op,const uint32_t a[5],uintptr_t host){return static_cast<Owner*>(p)->record(op,a,host);}
};
struct Test {
    Teakra::Teakra dsp{Teakra::UserConfig{}};Transport io{dsp};Session<Transport> session{io};
    ChunkCapture capture;StateTransaction states;CpuReplay replay;ChunkFrontend<Transport> frontend{io,session,capture,states,replay};
    Life life;Cpu cpu{life};Owner owner{session,frontend,cpu,life,10000};
    DspMixerImage before{},expected{},actual{};
    std::array<int16_t,64> input{};std::array<int16_t,16> history{},pcm{};
    explicit Test(const std::vector<uint16_t>& code):life{io,session,code}{for(unsigned i=0;i<input.size();++i)input[i]=int16_t(i*171-4000);}
    void sequence(){aLoadBufferImpl(input.data(),0x3e0,128);aSetBufferImpl(0,0x3e0,0x600,32);aResampleImpl(1,0x4000,history.data());aSaveBufferImpl(0x600,pcm.data(),32);}
    void chunk(bool barrier=false){
        SohDspMixerReadImage(&before,sizeof(before));auto oldHistory=history;int bypass=SohDspMixerSetCpuBypass(1);sequence();SohDspMixerSetCpuBypass(bypass);
        SohDspMixerReadImage(&expected,sizeof(expected));auto expectedHistory=history,expectedPcm=pcm;
        history=oldHistory;pcm.fill(0);SohDspMixerWriteImage(&before,sizeof(before));
        SohDspMixerBeginChunk();require(life.held,"chunk lock spans producer");
        if(barrier)owner.barrier();
        sequence();SohDspMixerEndChunk();
        require(!life.held,"end releases producer lock");SohDspMixerReadImage(&actual,sizeof(actual));
        require(!std::memcmp(&actual,&expected,sizeof(actual)) && history==expectedHistory && pcm==expectedPcm,"owner PCM/image/history equality");
    }
};
int main(int argc,char** argv){
    require(argc==2,"firmware path");std::ifstream file(argv[1],std::ios::binary);
    require(bool(file),"firmware open");std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
    require(!bytes.empty() && bytes.size()%2==0,"firmware words");std::vector<uint16_t> code(bytes.size()/2);
    for(unsigned i=0;i<code.size();++i)code[i]=uint16_t(bytes[2*i])|(uint16_t(bytes[2*i+1])<<8);
    for(unsigned mode=0;mode<10;++mode){
        auto t=std::make_unique<Test>(code);require(SohDspMixerInstallHooks(&hooks,&t->owner),"install");
        require(t->owner.enableCustom(),"enable custom");t->chunk();require(t->cpu.executed==0,"offloaded kernels");
        t->chunk(true);require(t->owner.mode()==Owner::Mode::Custom,"CPU barrier preserves custom backend");
        auto stats=t->owner.stats();
        require(stats.dspChunks==1 && stats.cpuChunks==1 && stats.capturedCpuChunks==1,
                "count committed DSP chunks separately from captured CPU replay");
        if(mode==0){
            t->chunk(true);require(t->owner.suspend() && !t->life.dsp && !t->life.output,"custom suspend stops resources");
            require(t->owner.resume() && t->owner.mode()==Owner::Mode::Custom,"custom resume");t->chunk();
            require(t->owner.suspend(),"suspend before output resume failure");t->life.outputFail=true;
            require(t->owner.resume() && t->owner.mode()==Owner::Mode::Cpu && !t->life.dsp && !t->life.output,"partial output resume falls back");
            t->life.outputFail=false;require(t->owner.enableCustom() && t->owner.suspend(),"suspend before boot resume failure");t->life.bootFail=true;
            require(t->owner.resume() && t->owner.mode()==Owner::Mode::Cpu && !t->life.dsp,"partial boot resume falls back");
        }else if(mode<=6){
            auto begin=t->life.events.size();
            if(mode==1)t->io.fail=true;
            if(mode==2)t->life.waitFail=true;
            if(mode==3)t->life.unhealthy=true;
            if(mode==4)t->life.timeoutWait=true;
            if(mode==5)t->life.cancelOnWait=true;
            if(mode==6)t->life.unhealthyOnWait=true;
            t->chunk();require(t->owner.mode()==Owner::Mode::Cpu,"runtime failure falls back");
            std::vector<std::string> events(t->life.events.begin()+begin,t->life.events.end());
            auto pos=[&](const char* name){return std::find(events.begin(),events.end(),name)-events.begin();};
            require(pos("stopDSP")<pos("stopOutput") && pos("stopOutput")<pos("startNDSP"),"serialized fallback order");
            if(mode!=3)require(pos("stopDSP")<pos("replay") && pos("replay")<pos("startNDSP") && t->owner.recoveries()==1,"unload before failed replay");
            t->chunk();
            stats=t->owner.stats();
            require(stats.dspChunks==1 && stats.cpuChunks==3 && stats.capturedCpuChunks==(mode==3?1u:2u),
                    "failed DSP work and live CPU chunks never count as committed DSP");
        }else{
            require(t->owner.disableCustom(),"disable before boot failures");
            if(mode==7)t->life.bootFail=true;
            if(mode==8)t->life.outputFail=true;
            if(mode==9){t->life.bootFail=true;t->life.ndspFail=true;}
            require(!t->owner.enableCustom(),"partial init rejected");
            require(!t->life.dsp && !t->life.output,"partial custom cleanup");
            if(mode==9){require(t->owner.mode()==Owner::Mode::Silent && !t->life.ndsp,"failed NDSP cleaned before silent");t->life.bootFail=false;require(t->owner.enableCustom(),"custom after failed NDSP cleanup");t->life.ndspFail=false;require(t->owner.disableCustom(),"retry NDSP");}
        }
        require(t->owner.disableCustom() && t->owner.suspend() && t->owner.resume(),"CPU sleep lifecycle");
        require(t->owner.shutdown() && !t->life.ndsp && t->owner.mode()==Owner::Mode::Closed,"CPU shutdown without restart");
        require(SohDspMixerInstallHooks(nullptr,nullptr),"uninstall");
    }
    {auto deadline=std::make_unique<Test>(code);require(SohDspMixerInstallHooks(&hooks,&deadline->owner) && deadline->owner.enableCustom(),"deadline setup");
        deadline->life.expireBeforeWait=true;deadline->chunk();
        require(deadline->owner.recoveries()==1 && std::find(deadline->life.events.begin(),deadline->life.events.end(),"wait")==deadline->life.events.end(),"expired deadline repolls without zero wait");
        require(SohDspMixerInstallHooks(nullptr,nullptr),"deadline uninstall");}
    // Real lock exclusion: suspend cannot tear down a producer-owned chunk.
    auto t=std::make_unique<Test>(code);require(SohDspMixerInstallHooks(&hooks,&t->owner) && t->owner.enableCustom(),"concurrency setup");
    SohDspMixerBeginChunk();std::atomic<bool> entered{false},done{false};
    std::thread control([&](){entered=true;require(t->owner.suspend(),"concurrent suspend");done=true;});
    while(!entered.load())std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(!done.load(),"suspend waits for producer lock");t->sequence();SohDspMixerEndChunk();control.join();
    require(done && !t->life.dsp && !t->life.output,"suspend after chunk completion");require(t->owner.resume(),"resume concurrency");
    require(SohDspMixerInstallHooks(nullptr,nullptr),"concurrency uninstall");
    // Unconfirmed ownership is not allowed to return into a live producer.
    for(unsigned mode=0;mode<3;++mode){pid_t child=fork();require(child>=0,"fork");if(child==0){
        if(mode==0){t->life.stopDspFail=true;t->io.fail=true;require(SohDspMixerInstallHooks(&hooks,&t->owner),"fatal capture install");t->chunk();_exit(1);}
        if(mode==1)t->life.stopOutputFail=true;
        if(mode==2){t->life.ndspFail=true;t->life.stopNdspFail=true;}
        t->owner.disableCustom();_exit(1);
    }int status=0;require(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==77,"unconfirmed teardown fatal instead of unsafe return");}
    require(t->owner.shutdown() && !t->life.dsp && !t->life.output && !t->life.ndsp,"custom shutdown without NDSP restart");
    require(t->owner.shutdown() && !t->owner.enableCustom() && !t->owner.disableCustom(),"closed owner cannot restart");
    std::puts("PASS: real hooked DSP chunks, CPU barriers, failed submission/wait replay after unload, serialized NDSP switch, partial-init cleanup, silent retry, sleep/resume, lock exclusion and unconfirmed-stop fail-closed");
}
