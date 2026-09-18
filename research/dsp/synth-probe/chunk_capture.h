#pragma once
#include <algorithm>
#include <array>
#include <cstring>
#include "mapped_parameters.h"
namespace ResidentDsp {
// Single-owner, fixed storage. Host pointer ranges must remain alive/exclusively
// owned until commit or discard. This buffers saves; it does not commit voice
// states, execute DSP transfers, or provide CPU replay by itself.
class ChunkCapture {
public:
    static constexpr unsigned MaxEntries=192,SaveBytes=4096;
    enum class Kind:uint8_t {Command,Load,Save};
    enum class Phase:uint8_t {Recording,Sealed,Executing,Running,Failed,Committed};
    enum class Error:uint8_t {None,Invalid,Capacity,Runtime};
    struct Entry {
        Kind kind=Kind::Command;
        Command command{};
        MappedParameters parameters{};
        uintptr_t host=0;
        uint16_t dataOffset=0,bytes=0,sourceOffset=0;
        bool hasParameters=false;
    };
    ChunkCapture()=default;
    ChunkCapture(const ChunkCapture&)=delete;ChunkCapture& operator=(const ChunkCapture&)=delete;
    ChunkCapture(ChunkCapture&&)=delete;ChunkCapture& operator=(ChunkCapture&&)=delete;
    bool append(const Command& command,const MappedParameters* parameters=nullptr){
        if(phase_!=Phase::Recording)return false;
        if(command.type<7 || command.type>23 || bool(parameters)!=(command.type>=9))return reject(Error::Invalid);
        if(size_==MaxEntries)return reject(Error::Capacity);
        Entry e;e.command=command;e.hasParameters=parameters!=nullptr;if(parameters)e.parameters=*parameters;
        entries_[size_++]=e;return true;
    }
    bool load(const void* source,uint16_t destination,uint16_t bytes){
        if(phase_!=Phase::Recording)return false;
        Command c;
        if(size_==MaxEntries || bytes>PayloadBytes-payloadUsed_)return reject(Error::Capacity);
        if(!hostRange(source,bytes) || !lowerMappedLoad(payloadUsed_,destination,bytes,c))return reject(Error::Invalid);
        Entry e;e.kind=Kind::Load;e.command=c;e.host=reinterpret_cast<uintptr_t>(source);e.dataOffset=payloadUsed_;e.bytes=bytes;
        if(bytes)std::memcpy(payload_.data()+payloadUsed_,source,bytes);
        payloadUsed_+=bytes;entries_[size_++]=e;return true;
    }
    bool save(uint16_t source,void* destination,uint16_t bytes){
        if(phase_!=Phase::Recording)return false;
        unsigned actual=bytes&~15u;uint16_t sampleOffset=0;
        if(!hostRange(destination,actual) || !lowerSampleAddress(source,actual/2,sampleOffset))return reject(Error::Invalid);
        if(size_==MaxEntries || actual>SaveBytes-saveUsed_)return reject(Error::Capacity);
        Entry e;e.kind=Kind::Save;e.host=reinterpret_cast<uintptr_t>(destination);e.bytes=actual;e.sourceOffset=sampleOffset*2;e.dataOffset=saveUsed_;
        saveUsed_+=actual;entries_[size_++]=e;return true;
    }
    bool seal(){if(phase_!=Phase::Recording)return false;phase_=Phase::Sealed;return true;}
    bool beginExecution(){if(phase_!=Phase::Sealed)return false;phase_=Phase::Executing;return true;}
    // Resolve immutable captured input against preceding tentative saves. Refuse
    // unresolved overlaps before writing output. Last save wins, byte by byte.
    bool prepareLoad(unsigned index,void* output,unsigned capacity){
        if(phase_!=Phase::Executing || index<cursor_ || index>=size_)return false;
        const auto& e=entries_[index];if(e.kind!=Kind::Load || capacity<e.bytes || !hostRange(output,e.bytes))return false;
        for(unsigned i=0;i<index;++i)if(entries_[i].kind==Kind::Save && overlaps(e,entries_[i]) && !ready_[i])return false;
        auto* dest=static_cast<uint8_t*>(output);if(e.bytes)std::memcpy(dest,payload_.data()+e.dataOffset,e.bytes);
        for(unsigned i=0;i<index;++i){
            const auto& s=entries_[i];if(s.kind!=Kind::Save || !overlaps(e,s))continue;
            uintptr_t first=std::max(e.host,s.host),last=std::min(e.host+e.bytes,s.host+s.bytes);
            std::memcpy(dest+(first-e.host),saves_.data()+s.dataOffset+(first-s.host),last-first);
        }
        ready_[index]=true;return true;
    }
    // Begin before publishing shared data; any failure after this requires DSP
    // stop before discard/reset. Caller uploads each prepared load's resolved
    // bytes, not the unchanged baseline returned by payloadData().
    bool beginBatch(unsigned count){
        if(phase_!=Phase::Executing || !count || count>32 || count>size_-cursor_)return false;
        for(unsigned i=cursor_;i<cursor_+count;++i)if(entries_[i].kind==Kind::Save || (entries_[i].kind==Kind::Load && !ready_[i]))return false;
        batch_=count;published_=true;phase_=Phase::Running;return true;
    }
    // Call only after Session verifies matching successful DSP completion.
    bool finishBatch(){if(phase_!=Phase::Running)return false;cursor_+=batch_;batch_=0;phase_=Phase::Executing;return true;}
    bool collectSave(const void* source,unsigned bytes){
        if(phase_!=Phase::Executing || cursor_==size_)return false;
        const auto& e=entries_[cursor_];if(e.kind!=Kind::Save || bytes!=e.bytes || !hostRange(source,bytes))return false;
        if(bytes)std::memcpy(saves_.data()+e.dataOffset,source,bytes);
        ready_[cursor_++]=true;return true;
    }
    // Caller must validate/collect DSP state before this final host publication.
    bool commitSaves(){
        if(phase_!=Phase::Executing || cursor_!=size_)return false;
        for(unsigned i=0;i<size_;++i)if(entries_[i].kind==Kind::Save && !ready_[i])return false;
        for(unsigned i=0;i<size_;++i){const auto& e=entries_[i];if(e.kind==Kind::Save && e.bytes)std::memcpy(reinterpret_cast<void*>(e.host),saves_.data()+e.dataOffset,e.bytes);}
        phase_=Phase::Committed;return true;
    }
    void fail(){if(phase_==Phase::Executing || phase_==Phase::Running){phase_=Phase::Failed;error_=Error::Runtime;}}
    bool resetBeforeSubmit(){if(published_)return false;reset();return true;}
    bool nextChunk(){if(phase_!=Phase::Committed)return false;reset();return true;}
    void resetAfterStop(){reset();} // lifecycle owner guarantees firmware stopped
    Phase phase()const{return phase_;}Error error()const{return error_;}
    unsigned size()const{return size_;}unsigned cursor()const{return cursor_;}
    unsigned payloadBytes()const{return payloadUsed_;}unsigned saveBytes()const{return saveUsed_;}
    const Entry* at(unsigned index)const{return index<size_?&entries_[index]:nullptr;}
    const uint8_t* payloadData()const{return payload_.data();}
private:
    static bool hostRange(const void* p,unsigned bytes){auto address=reinterpret_cast<uintptr_t>(p);return !bytes || (p && address<=UINTPTR_MAX-bytes);}
    static bool overlaps(const Entry& a,const Entry& b){return a.bytes && b.bytes && a.host<b.host+b.bytes && b.host<a.host+a.bytes;}
    bool reject(Error e){phase_=Phase::Failed;error_=e;return false;}
    void reset(){size_=cursor_=batch_=payloadUsed_=saveUsed_=0;ready_.fill(false);phase_=Phase::Recording;error_=Error::None;published_=false;}
    std::array<Entry,MaxEntries> entries_{};
    std::array<uint8_t,PayloadBytes> payload_{};
    std::array<uint8_t,SaveBytes> saves_{};
    std::array<bool,MaxEntries> ready_{};
    unsigned size_=0,cursor_=0,batch_=0,payloadUsed_=0,saveUsed_=0;
    Phase phase_=Phase::Recording;Error error_=Error::None;bool published_=false;
};
static_assert(sizeof(ChunkCapture)<=24*1024,"capture storage exceeded fixed RAM budget");
}
