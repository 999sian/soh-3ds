#include <cstdio>
#include "chunk_capture.h"
using namespace ResidentDsp;
#define CHECK(x) do{if(!(x)){std::printf("FAIL capture line%d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){
    using Phase=ChunkCapture::Phase;using Error=ChunkCapture::Error;
    {
        ChunkCapture c;std::array<uint8_t,32> host{};for(unsigned i=0;i<host.size();++i)host[i]=i;auto original=host;
        CHECK(c.load(host.data(),0x3c1,32));CHECK(c.save(0x3c1,host.data()+8,31));CHECK(c.save(0x3c0,host.data()+12,16));CHECK(c.load(host.data()+4,0x3c0,24));
        CHECK(c.size()==4 && c.payloadBytes()==56 && c.saveBytes()==32 && c.at(1)->sourceOffset==0);
        CHECK(c.seal() && !c.load(host.data(),0x3c0,1));CHECK(c.beginExecution());CHECK(!c.commitSaves());
        std::array<uint8_t,32> out;out.fill(0xcc);auto untouched=out;
        CHECK(!c.prepareLoad(3,out.data(),out.size()) && out==untouched);CHECK(!c.beginBatch(1));
        host[0]=199;CHECK(c.prepareLoad(0,out.data(),out.size()) && out==original);
        CHECK(!c.beginBatch(2));CHECK(c.beginBatch(1));CHECK(!c.prepareLoad(0,out.data(),out.size()));CHECK(!c.collectSave(out.data(),16));CHECK(!c.resetBeforeSubmit());CHECK(c.finishBatch());
        std::array<uint8_t,16> a,b;a.fill(0xaa);b.fill(0xbb);
        CHECK(!c.collectSave(a.data(),15));CHECK(c.collectSave(a.data(),16));CHECK(c.collectSave(b.data(),16));
        CHECK(host[8]==8 && host[12]==12);CHECK(c.prepareLoad(3,out.data(),out.size()));
        for(unsigned i=0;i<24;++i)CHECK(out[i]==(i<4?i+4:i<8?0xaa:0xbb));
        CHECK(c.beginBatch(1) && c.finishBatch());CHECK(c.commitSaves());CHECK(!c.commitSaves());
        for(unsigned i=0;i<32;++i)CHECK(host[i]==(i==0?199:i<8?i:i<12?0xaa:i<28?0xbb:i));
        CHECK(c.nextChunk() && c.size()==0 && c.payloadBytes()==0 && c.phase()==Phase::Recording);
    }
    {
        ChunkCapture c;std::array<uint8_t,32> host{};auto old=host;MappedParameters p{};p[12]=0x4000;
        CHECK(c.append({9,16,0,0},&p));p[12]=0;CHECK(c.at(0)->parameters[12]==0x4000);
        CHECK(c.save(0x3c0,host.data(),16));CHECK(c.seal() && c.beginExecution() && c.beginBatch(1));c.fail();
        CHECK(c.phase()==Phase::Failed && c.error()==Error::Runtime && !c.finishBatch() && !c.commitSaves() && host==old);
        CHECK(!c.resetBeforeSubmit());c.resetAfterStop();CHECK(c.phase()==Phase::Recording && c.size()==0);
        CHECK(!c.append({9,16,0,0}) && c.error()==Error::Invalid);CHECK(c.resetBeforeSubmit());CHECK(!c.append({99,0,0,0}));
    }
    {
        ChunkCapture c;std::array<uint8_t,3072> input{};
        CHECK(c.load(input.data(),0x3c0,3072) && c.load(input.data(),0x3c0,3072) && c.load(input.data(),0x3c0,1536));
        CHECK(!c.load(input.data(),0x3c0,1) && c.error()==Error::Capacity && c.size()==3 && c.payloadBytes()==PayloadBytes);
        CHECK(c.resetBeforeSubmit());std::array<uint8_t,4096> host{};
        CHECK(c.save(0x3c0,host.data(),3072) && c.save(0x3c0,host.data()+3072,1024));
        CHECK(!c.save(0x3c0,host.data(),16) && c.error()==Error::Capacity && c.size()==2 && c.saveBytes()==4096);
        CHECK(c.resetBeforeSubmit());for(unsigned i=0;i<ChunkCapture::MaxEntries;++i)CHECK(c.append({8,0,0,0}));
        CHECK(!c.append({8,0,0,0}) && c.error()==Error::Capacity && c.size()==ChunkCapture::MaxEntries);
    }
    {
        ChunkCapture c;for(unsigned i=0;i<40;++i)CHECK(c.append({8,0,0,0}));
        CHECK(c.seal() && c.beginExecution());CHECK(!c.beginBatch(0) && !c.beginBatch(33) && !c.finishBatch());
        CHECK(c.beginBatch(32) && c.finishBatch() && c.cursor()==32);CHECK(!c.commitSaves());
        CHECK(c.beginBatch(8) && c.finishBatch() && c.commitSaves() && c.nextChunk());
        CHECK(c.load(nullptr,0xfc0,0) && c.save(0xfc0,nullptr,15));CHECK(c.seal() && c.beginExecution());
        CHECK(c.prepareLoad(0,nullptr,0) && c.beginBatch(1) && c.finishBatch() && c.collectSave(nullptr,0) && c.commitSaves());
    }
    {
        ChunkCapture c;uint8_t byte=0;
        CHECK(!c.load(&byte,0x3bf,0) && c.size()==0);CHECK(c.resetBeforeSubmit());
        const void* invalid=reinterpret_cast<const void*>(UINTPTR_MAX-1);
        CHECK(!c.load(invalid,0x3c0,16) && c.size()==0);CHECK(c.resetBeforeSubmit());
        CHECK(!c.save(0x3c0,const_cast<void*>(invalid),16) && c.size()==0);
    }
    std::printf("PASS: bounded capture snapshots, overlapping save/load dependencies, deferred host publication,32-command batches, failure ownership, all storage caps and zero/invalid extents; %zu bytes fixed host storage\n",sizeof(ChunkCapture));
}
