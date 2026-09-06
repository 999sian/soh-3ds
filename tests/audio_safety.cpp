// Compiled together with production factory functions by audio_safety_test.py.
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <new>
static int allocationsUntilFailure = -1;
static size_t liveArrays = 0;
void* operator new[](size_t bytes) {
    if (allocationsUntilFailure == 0) throw std::bad_alloc();
    if (allocationsUntilFailure > 0) --allocationsUntilFailure;
    auto p = std::malloc(bytes ? bytes : 1);
    if (!p) throw std::bad_alloc();
    ++liveArrays;
    return p;
}
void operator delete[](void* p) noexcept { if (p) { --liveArrays; std::free(p); } }
void operator delete[](void* p, size_t) noexcept { operator delete[](p); }
static void put32(std::vector<char>& v, uint32_t x) { for (int i=0;i<4;++i) v.push_back(x >> (8*i)); }
static std::shared_ptr<Ship::File> binary(const std::vector<char>& data) {
    auto file = std::make_shared<Ship::File>();
    file->Reader = std::make_shared<Ship::BinaryReader>(data);
    return file;
}
static std::vector<char> fixture() {
    std::vector<char> v{0,0,0,0}; put32(v, 9); v.resize(v.size()+9);
    put32(v,0); put32(v,16); put32(v,0); put32(v,16); v.resize(v.size()+32);
    put32(v,2); put32(v,1); put32(v,16); v.resize(v.size()+32);
    return v;
}
int main(int argc, char** argv) {
    auto init = std::make_shared<Ship::ResourceInitData>();
    SOH::ResourceFactoryBinaryAudioSampleV2 factory;
    auto bytes = fixture();
    for (size_t n=0; n<bytes.size(); ++n) {
        bool failed=false;
        try { factory.ReadResource(binary({bytes.begin(),bytes.begin()+n}),init); }
        catch (const std::exception&) { failed=true; }
        assert(failed); assert(liveArrays == 0);
    }
    for (int fail=0; fail<2; ++fail) {
        auto file=binary(bytes); allocationsUntilFailure=fail;
        bool failed=false;
        try { factory.ReadResource(file,init); } catch(const std::bad_alloc&) { failed=true; }
        allocationsUntilFailure=-1;
        assert(failed); assert(liveArrays == 0);
    }
    { auto result=factory.ReadResource(binary(bytes),init); assert(result); }
    assert(liveArrays == 0);
    // Loop-state overflow, hostile book dimensions/count, oversized sample payload.
    for (auto [offset,value] : {std::pair<size_t,uint32_t>{29,17}, {65,0xffffffff}, {73,0xffffffff}, {4,0xffffffff}}) {
        auto invalid=bytes; for(int i=0;i<4;++i) invalid[offset+i]=value>>(8*i);
        bool failed=false;
        try { factory.ReadResource(binary(invalid),init); } catch(const std::exception&) {failed=true;}
        assert(failed); assert(liveArrays==0);
    }
    // Callback arithmetic must reject wraparound and malformed seeking without moving the cursor.
    {
        char data[8] = {}; char output[8] = {};
        OggFileData input{data, 3, sizeof(data)};
        assert(VorbisReadCallback(output, SIZE_MAX, 2, &input) == 0 && input.pos == 3);
        assert(VorbisReadCallback(output, 0, 2, &input) == 0);
        assert(VorbisSeekCallback(&input, INT64_MIN, SEEK_CUR) == -1 && input.pos == 3);
        assert(VorbisSeekCallback(&input, INT64_MAX, SEEK_END) == -1 && input.pos == 3);
        assert(VorbisSeekCallback(&input, -2, SEEK_END) == 0 && input.pos == 6);
    }
    // Full production decoder with malformed data must contain its own failures.
    for (auto format : {"wav","mp3","ogg","flac"}) {
        SOH::AudioSample sample(init);
        std::vector<char> invalid(17, 'x');
        assert(!DecodeCustomAudio(sample,invalid,format));
        assert(sample.sample.sampleAddr == nullptr && sample.sample.size == 0);
        assert(sample.sample.codec == CODEC_S16_INMEMORY);
    }
    // Generated tiny real WAV/MP3/Vorbis/FLAC fixtures exercise the decoder libraries.
    assert(argc == 2);
    for (auto format : {"wav","mp3","ogg","flac"}) {
        std::vector<char> encoded;
        { std::ifstream in(std::string(argv[1])+"/tone."+format,std::ios::binary);
          encoded.assign(std::istreambuf_iterator<char>(in),{}); }
        assert(!encoded.empty());
        { SOH::AudioSample sample(init);
          assert(DecodeCustomAudio(sample,encoded,format));
          assert(sample.sample.sampleAddr && sample.sample.size > 0);
          assert(sample.tuning == (std::strcmp(format,"wav") == 0 ? 1.0f : -1.0f));
          bool nonzero=false; for(size_t i=0;i<sample.sample.size;++i) nonzero |= sample.sample.sampleAddr[i]!=0;
          assert(nonzero);
        }
        { SOH::AudioSample sample(init); allocationsUntilFailure=0;
          assert(!DecodeCustomAudio(sample,encoded,format)); allocationsUntilFailure=-1;
          assert(!sample.sample.sampleAddr && sample.sample.size==0);
        }
        assert(liveArrays==0);
    }
    // Chained Vorbis may reuse one PCM allocation only when all links share a format.
    {
        std::vector<char> encoded;
        { std::ifstream in(std::string(argv[1])+"/chain-changed.ogg",std::ios::binary);
          encoded.assign(std::istreambuf_iterator<char>(in),{}); }
        SOH::AudioSample sample(init);
        assert(!DecodeCustomAudio(sample,encoded,"ogg"));
        assert(!sample.sample.sampleAddr && sample.sample.size == 0);
    }
    // No multiplication overflow or zero-frame allocation; budget released after failure/destruction.
    for (auto frames : {uint64_t(0), UINT64_MAX, uint64_t(1)<<32}) {
        bool failed=false; try { CheckedPcmBytes(frames,2); } catch(const std::exception&) {failed=true;}
        assert(failed);
    }
#ifdef __3DS__
    {
        std::vector<char> encoded;
        { std::ifstream in(std::string(argv[1])+"/oversized.flac",std::ios::binary);
          encoded.assign(std::istreambuf_iterator<char>(in),{}); }
        SOH::AudioSample sample(init);
        assert(!DecodeCustomAudio(sample,encoded,"flac"));
        assert(!sample.sample.sampleAddr && sample.sample.size==0);
        allocationsUntilFailure=0;
        bool failed=false;
        try { sample.AllocateSampleData(8*1024*1024,true); } catch(const std::bad_alloc&) {failed=true;}
        allocationsUntilFailure=-1;
        assert(failed);
    }
    {
        SOH::AudioSample a(init), b(init), c(init);
        a.AllocateSampleData(8*1024*1024,true); b.AllocateSampleData(8*1024*1024,true);
        bool failed=false; try { c.AllocateSampleData(2,true); } catch(const std::exception&) {failed=true;}
        assert(failed && c.sample.sampleAddr==nullptr);
        a.MarkUnavailable(); c.AllocateSampleData(2,true);
    }
    assert(liveArrays==0);
#endif
    std::puts("audio safety passed");
}
