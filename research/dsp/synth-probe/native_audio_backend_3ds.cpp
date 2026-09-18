#include "mapped_firmware_component.h" // selects the matching host completion protocol
#include "native_runtime_3ds.h"
#include "native_ndsp_port_3ds.h"
static_assert(mapped_firmware_mailbox_completion,"Game DSP backend requires hardware-tested mailbox completion");
#include "ship/audio/DspAudioBackend3DS.h"
#include "ship/utils/audio_diagnostics_3ds.h"
namespace {
ResidentDsp::NativeNdspPort port;
ResidentDsp::NativeSynthRuntime<ResidentDsp::NativeNdspPort> runtime(port,mapped_firmware_component,sizeof(mapped_firmware_component));
bool initialize(unsigned rate,unsigned desired){return runtime.initialize(rate,desired);}
void close(){runtime.close();}
void shutdown(){runtime.shutdown();}
int buffered(){return runtime.buffered();}
void play(const uint8_t* pcm,size_t bytes){
    if(bytes/4>UINT32_MAX)return;
    runtime.play(reinterpret_cast<const int16_t*>(pcm),unsigned(bytes/4));
    // Report after publishing PCM; normal play does no diagnostic file I/O.
    static unsigned batches=0;
    if(++batches%256 || !Soh3dsLoggingEnabled(SOH3DS_LOG_GENERAL))return;
    auto stats=runtime.stats();
    char record[320];
    // startFail/startUs localize an Error::Start (outputError=3) to one of the
    // five distinct CsndStream::start failure points; startUs is the elapsed
    // time from CSND playback start to the post-start verification, which is
    // what the timeline health check bounds.
    // tlState is StreamTimeline::State on entry to start {Idle,Running,
    // Reserved,Fault,Stopped} = 0..4. A startFail=4 (Timeline) with tlState=3
    // means an earlier failure left it in Fault; tlState=1 means a previous
    // session was never stopped. Those need different fixes.
    // maxGapUs is the worst service gap the worker has seen. The service deadline
    // is 4000us, in front of a ring holding 256ms with an 8ms guard - so this says
    // whether that proxy threshold is what kills the path, and what it should be.
    snprintf(record,sizeof(record),"dsp synth: mode=%u dspChunks=%u cpuChunks=%u capturedCpuChunks=%u recoveries=%u outputError=%u ndspDrops=%u startFail=%u startUs=%lu activeMask=%u activityKnown=%u tlState=%u maxGapUs=%u\n",
             unsigned(runtime.mode()),stats.dspChunks,stats.cpuChunks,stats.capturedCpuChunks,runtime.recoveries(),runtime.outputError(),port.dropped(),
             runtime.outputStartFail(),
             (unsigned long)(runtime.outputStartElapsedTicks()*1000000ULL/SYSCLOCK_ARM11),
             runtime.outputActiveMask(),runtime.outputActivityKnown(),
             runtime.outputStartState(),runtime.outputMaxGapUs());
    Soh3dsWriteAudioDiagnostic(record);
    svcOutputDebugString(record,std::strlen(record));
}
const Soh3dsDspAudioBackendApi api={initialize,close,shutdown,buffered,play};
}
extern "C" const Soh3dsDspAudioBackendApi* Soh3dsDspAudioBackend(){return &api;}
