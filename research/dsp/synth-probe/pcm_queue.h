#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <algorithm>
namespace ResidentDsp {
// Stereo PCM FIFO. Caller holds the output queue lock for every operation.
// Peek does not relinquish ownership; consume follows confirmed publication.
template<unsigned Capacity> class PcmQueue {
public:
    bool push(const int16_t* source,unsigned frames){
        if(!source || !frames || frames>Capacity-used_)return false;
        unsigned tail=(head_+used_)%Capacity,n=std::min(frames,Capacity-tail);
        std::memcpy(data_.data()+tail*2,source,n*4);
        if(n<frames)std::memcpy(data_.data(),source+n*2,(frames-n)*4);
        used_+=frames;return true;
    }
    unsigned peek(int16_t* destination,unsigned limit)const{
        if(!destination)return 0;
        unsigned frames=std::min(limit,used_),n=std::min(frames,Capacity-head_);
        std::memcpy(destination,data_.data()+head_*2,n*4);
        if(n<frames)std::memcpy(destination+n*2,data_.data(),(frames-n)*4);
        return frames;
    }
    bool consume(unsigned frames){if(frames>used_)return false;head_=(head_+frames)%Capacity;used_-=frames;return true;}
    unsigned size()const{return used_;}
    void clear(){head_=used_=0;}
private:
    static_assert(Capacity>0 && Capacity<=65536,"bounded PCM queue");
    std::array<int16_t,Capacity*2> data_{};
    unsigned head_=0,used_=0;
};
}
