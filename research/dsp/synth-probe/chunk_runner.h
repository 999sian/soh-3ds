#pragma once
#include "chunk_capture.h"
#include "stream_io_trace.h"
namespace ResidentDsp {
// Polling is one bounded action, never a busy loop. The platform worker sleeps
// on Session's completion event while Waiting. Caller stages voice state/books
// before start, and validates/collects persistent state after Collected before
// committing any host outputs. This runner never publishes saves or replays CPU.
template<class Transport> class ChunkRunner {
public:
    enum class Phase {Idle,Working,Waiting,Collected,Fault};
    enum class Error {None,Capture,Transfer,Session};
    ChunkRunner(Transport& transport,Session<Transport>& session,ChunkCapture& capture):io(transport),session_(session),capture_(capture){}
    ChunkRunner(const ChunkRunner&)=delete;ChunkRunner& operator=(const ChunkRunner&)=delete;
    ChunkRunner(ChunkRunner&&)=delete;ChunkRunner& operator=(ChunkRunner&&)=delete;
    bool start(uint64_t timeout){
        if((phase_!=Phase::Idle && phase_!=Phase::Collected) || !timeout || session_.state()!=State::Ready || capture_.phase()!=ChunkCapture::Phase::Sealed)return false;
        if(!io.invalidate(0,32) || io.read(0x10)!=0x4458 || io.read(0)!=0)return false;
        if(!capture_.beginExecution())return false;
        timeout_=timeout;batches_=loads_=saves_=0;error_=Error::None;phase_=Phase::Working;return true;
    }
    Phase step(uint64_t now){
        if(phase_==Phase::Waiting){
            auto state=session_.poll(now);
            if(state==State::Running)return phase_;
            if(state!=State::Done)return fail(Error::Session);
            if(!capture_.finishBatch() || !session_.acknowledge())return fail(Error::Capture);
            phase_=Phase::Working;return phase_;
        }
        if(phase_!=Phase::Working)return phase_;
        if(session_.state()!=State::Ready || capture_.phase()!=ChunkCapture::Phase::Executing)return fail(Error::Capture);
        unsigned cursor=capture_.cursor();
        if(cursor==capture_.size()){phase_=Phase::Collected;return phase_;}
        const auto* first=capture_.at(cursor);
        if(first->kind==ChunkCapture::Kind::Save){
            unsigned words=first->bytes/2,base=AudioDmemWordBase+first->sourceOffset/2;
            if(words && !io.invalidate(base,words))return fail(Error::Transfer);
            for(unsigned i=0;i<words;++i){uint16_t value=io.read(base+i);scratch_[i*2]=uint8_t(value);scratch_[i*2+1]=uint8_t(value>>8);}
            if(!capture_.collectSave(scratch_.data(),first->bytes))return fail(Error::Capture);
            ++saves_;return phase_;
        }
        unsigned count=0,parameters=0,begin=PayloadBytes,end=0;
        while(count<32 && cursor+count<capture_.size()){
            const auto& e=*capture_.at(cursor+count);if(e.kind==ChunkCapture::Kind::Save)break;
            commands_[count]=e.command;
            if(e.hasParameters)commands_[count].argument=parameters++;
            if(e.kind==ChunkCapture::Kind::Load){
                SOH_DSP_TRACE_IO("prepare_load_begin",cursor+count,e.bytes,0);
                if(!capture_.prepareLoad(cursor+count,scratch_.data()+e.dataOffset,PayloadBytes-e.dataOffset))return fail(Error::Capture);
                SOH_DSP_TRACE_IO("prepare_load_end",cursor+count,e.bytes,0);
                if(e.bytes){begin=std::min(begin,unsigned(e.dataOffset));end=std::max(end,unsigned(e.dataOffset)+e.bytes);}
            }
            ++count;
        }
        // Capture owns everything before the first shared write or cache call.
        if(!capture_.beginBatch(count))return fail(Error::Capture);
        unsigned payloadBase=PayloadWordBase+begin/2,payloadWords=end?(end+1)/2-begin/2:0;
        // Invalidate once BEFORE any writes. Per-load invalidation could discard
        // earlier unflushed writes when adjacent loads share a cache line.
        if(payloadWords && !io.invalidate(payloadBase,payloadWords))return fail(Error::Transfer);
        unsigned slot=0;
        for(unsigned i=0;i<count;++i){
            const auto& e=*capture_.at(cursor+i);
            if(e.kind==ChunkCapture::Kind::Load){
                SOH_DSP_TRACE_IO("payload_write_begin",PayloadWordBase+e.dataOffset/2,e.bytes,0);
                writeBytes(e.dataOffset,scratch_.data()+e.dataOffset,e.bytes);++loads_;
                SOH_DSP_TRACE_IO("payload_write_end",PayloadWordBase+e.dataOffset/2,e.bytes,0);
            }
            if(e.hasParameters){
                SOH_DSP_TRACE_IO("parameter_write_begin",ParameterWordBase+slot*ParameterWords,ParameterWords,0);
                for(unsigned j=0;j<ParameterWords;++j)io.write(ParameterWordBase+slot*ParameterWords+j,e.parameters[j]);
                SOH_DSP_TRACE_IO("parameter_write_end",ParameterWordBase+slot*ParameterWords,ParameterWords,0);
                ++slot;
            }
        }
        if(payloadWords && !io.flush(payloadBase,payloadWords))return fail(Error::Transfer);
        if(parameters && !io.flush(ParameterWordBase,parameters*ParameterWords))return fail(Error::Transfer);
        SOH_DSP_TRACE_IO("command_publish_begin",0x100,count,0);
        if(!session_.submit(commands_.data(),count,now,timeout_))return fail(Error::Session);
        SOH_DSP_TRACE_IO("command_publish_end",0x100,count,0);
        ++batches_;phase_=Phase::Waiting;return phase_;
    }
    // Lifecycle owner stops/unloads firmware and resets Session and capture first.
    bool resetAfterStop(){if(session_.state()!=State::Unbound || capture_.phase()!=ChunkCapture::Phase::Recording)return false;phase_=Phase::Idle;error_=Error::None;return true;}
    Phase phase()const{return phase_;}Error error()const{return error_;}
    unsigned batches()const{return batches_;}unsigned loads()const{return loads_;}unsigned saves()const{return saves_;}
private:
    Phase fail(Error error){error_=error;if(session_.state()!=State::Fault)session_.cancel();capture_.fail();phase_=Phase::Fault;return phase_;}
    void writeBytes(unsigned offset,const uint8_t* input,unsigned count){
        if(!count)return;
        unsigned word=PayloadWordBase+offset/2;
        if(offset&1){io.write(word,uint16_t((io.read(word)&255)|(unsigned(*input++)<<8)));++word;--count;}
        while(count>=2){io.write(word++,uint16_t(unsigned(input[0])|(unsigned(input[1])<<8)));input+=2;count-=2;}
        if(count)io.write(word,uint16_t((io.read(word)&0xff00)|*input));
    }
    Transport& io;Session<Transport>& session_;ChunkCapture& capture_;
    std::array<uint8_t,PayloadBytes> scratch_{};
    std::array<Command,32> commands_{};
    uint64_t timeout_=0;unsigned batches_=0,loads_=0,saves_=0;
    Phase phase_=Phase::Idle;Error error_=Error::None;
};
}
