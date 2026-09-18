#pragma once
#include "chunk_capture.h"

namespace ResidentDsp {
// Chunk-local identities. Host state stays unchanged until every DSP command and
// state readback succeeds. Cross-bank/partial aliases take CPU fallback rather
// than silently losing producer ordering. Caller owns all host ranges exclusively.
class StateTransaction {
public:
    enum class Kind:uint8_t {Resample,Adpcm,Filter,Loop};
    enum class Phase:uint8_t {Recording,Sealed,Staged,Collected,Committed,Failed};
    static constexpr unsigned Slots=64,Words=16,MaxRecords=Slots*4;
    StateTransaction()=default;
    StateTransaction(const StateTransaction&)=delete;
    StateTransaction& operator=(const StateTransaction&)=delete;
    bool bind(Kind kind,int16_t* host,unsigned& slot){return bindInternal(kind,host,slot);}
    bool loop(const int16_t* host,unsigned& slot){return bindInternal(Kind::Loop,host,slot);}
private:
    bool bindInternal(Kind kind,const int16_t* host,unsigned& slot){
        if(phase_!=Phase::Recording)return false;
        auto address=reinterpret_cast<uintptr_t>(host);
        unsigned bank=unsigned(kind);
        if(bank>=4 || !range(address,Words*2) || address%alignof(int16_t))return reject();
        for(unsigned i=0;i<used_;++i){
            const auto& r=records_[i];
            if(!overlap(address,Words*2,r.host,Words*2))continue;
            if(r.host!=address || r.kind!=kind || std::memcmp(r.image.data(),host,Words*2))return reject();
            slot=r.slot;return true;
        }
        if(counts_[bank]==Slots)return reject();
        auto& r=records_[used_++];r.host=address;r.kind=kind;r.slot=counts_[bank]++;
        std::memcpy(r.image.data(),host,Words*2);slot=r.slot;return true;
    }
public:
    // The frontend passes its complete current ADPCM table, including bytes left
    // intact by partial aLoadADPCM calls. Identity is content, not source pointer.
    bool book(const int16_t* table,unsigned& slot){
        if(phase_!=Phase::Recording)return false;
        if(!range(reinterpret_cast<uintptr_t>(table),AdpcmBookWords*2))return reject();
        for(unsigned i=0;i<books_;++i)if(!std::memcmp(booksData_[i].data(),table,AdpcmBookWords*2)){slot=i;return true;}
        if(books_==AdpcmBookSlots)return reject();
        slot=books_++;std::memcpy(booksData_[slot].data(),table,AdpcmBookWords*2);return true;
    }
    bool seal(const ChunkCapture& capture){
        if(phase_!=Phase::Recording || capture.phase()!=ChunkCapture::Phase::Sealed)return false;
        // Loads or saves overlapping mutable state need inter-command host/state
        // synchronization, which this mapping deliberately rejects before submit.
        for(unsigned i=0;i<used_;++i)for(unsigned j=0;j<capture.size();++j){
            const auto& e=*capture.at(j);
            if(e.kind==ChunkCapture::Kind::Command)continue;
            if((records_[i].kind!=Kind::Loop || e.kind==ChunkCapture::Kind::Save) &&
               overlap(records_[i].host,Words*2,e.host,e.bytes))return reject();
        }
        capture_=&capture;phase_=Phase::Sealed;return true;
    }
    template<class Transport> bool stage(Transport& io,const Session<Transport>& session){
        if(phase_!=Phase::Sealed || session.state()!=State::Ready)return false;
        // A failed cache operation can follow shared writes; latch ownership first.
        published_=true;phase_=Phase::Staged;
        if(!io.invalidate(0,32) || io.read(0x10)!=0x4458 || io.read(0)!=0)return reject();
        for(unsigned i=0;i<used_;++i){const auto& r=records_[i];unsigned base=word(r);
            for(unsigned j=0;j<Words;++j)io.write(base+j,uint16_t(r.image[j]));
        }
        for(unsigned i=0;i<books_;++i)for(unsigned j=0;j<AdpcmBookWords;++j)
            io.write(AdpcmBookWordBase+i*AdpcmBookWords+j,uint16_t(booksData_[i][j]));
        for(unsigned bank=0;bank<4;++bank)if(counts_[bank] && !io.flush(baseWord(Kind(bank)),counts_[bank]*Words))return reject();
        if(books_ && !io.flush(AdpcmBookWordBase,books_*AdpcmBookWords))return reject();
        return true;
    }
    template<class Transport> bool collect(Transport& io,const Session<Transport>& session,const ChunkCapture& capture){
        if(capture_!=&capture || phase_!=Phase::Staged || session.state()!=State::Ready || capture.phase()!=ChunkCapture::Phase::Executing || capture.cursor()!=capture.size())return false;
        // All invalidations precede reads, and all reads precede host publication.
        for(unsigned bank=0;bank<3;++bank)if(counts_[bank] && !io.invalidate(baseWord(Kind(bank)),counts_[bank]*Words))return reject();
        for(unsigned i=0;i<used_;++i){auto& r=records_[i];if(r.kind==Kind::Loop)continue;
            for(unsigned j=0;j<Words;++j)r.image[j]=int16_t(io.read(word(r)+j));
        }
        phase_=Phase::Collected;return true;
    }
    bool commit(ChunkCapture& capture){
        if(capture_!=&capture || phase_!=Phase::Collected)return false;
        // commitSaves validates its complete readiness before writing anything.
        // seal rejected state/save aliases, so these publications commute.
        if(!capture.commitSaves())return false;
        for(unsigned i=0;i<used_;++i){const auto& r=records_[i];if(r.kind!=Kind::Loop)
            std::memcpy(reinterpret_cast<void*>(r.host),r.image.data(),Words*2);
        }
        phase_=Phase::Committed;return true;
    }
    void fail(){phase_=Phase::Failed;}
    bool resetBeforeSubmit(){if(published_)return false;reset();return true;}
    bool nextChunk(){if(phase_!=Phase::Committed)return false;reset();return true;}
    void resetAfterStop(){reset();} // firmware stop is lifecycle owner's obligation
    bool overlapsMutable(const void* host,unsigned bytes)const{
        auto address=reinterpret_cast<uintptr_t>(host);
        if(bytes && !range(address,bytes))return true;
        for(unsigned i=0;i<used_;++i)if(records_[i].kind!=Kind::Loop && overlap(address,bytes,records_[i].host,Words*2))return true;
        return false;
    }
    Phase phase()const{return phase_;}
private:
    // Host baseline remains untouched, so staged snapshots can be reused for
    // tentative readback without a second copy of every state.
    struct Record {uintptr_t host=0;std::array<int16_t,Words> image{};Kind kind=Kind::Resample;uint8_t slot=0;};
    static bool range(uintptr_t p,unsigned n){return p && p<=UINTPTR_MAX-n;}
    static bool overlap(uintptr_t a,unsigned an,uintptr_t b,unsigned bn){return an && bn && a<b+bn && b<a+an;}
    static unsigned baseWord(Kind kind){constexpr unsigned bases[]={0x6400,AdpcmStateWordBase,FilterStateWordBase,AdpcmLoopWordBase};return bases[unsigned(kind)];}
    static unsigned word(const Record& r){return baseWord(r.kind)+r.slot*Words;}
    bool reject(){phase_=Phase::Failed;return false;}
    void reset(){capture_=nullptr;used_=books_=0;counts_.fill(0);published_=false;phase_=Phase::Recording;}
    std::array<Record,MaxRecords> records_{};
    std::array<std::array<int16_t,AdpcmBookWords>,AdpcmBookSlots> booksData_{};
    std::array<unsigned,4> counts_{};
    const ChunkCapture* capture_=nullptr;
    unsigned used_=0,books_=0;bool published_=false;Phase phase_=Phase::Recording;
};
static_assert(sizeof(StateTransaction)<=17*1024,"state transaction exceeded fixed RAM budget");
}
