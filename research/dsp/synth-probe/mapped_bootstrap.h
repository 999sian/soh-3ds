#pragma once
#include "resident_session.h"
namespace ResidentDsp {
constexpr unsigned MappedSharedWords=0x7420; // through filter count7408, cache aligned
// Validate service conversions of word0 and the last required word. The caller
// must request both addresses after loading the component; never derive a map
// solely from a fixed virtual address. End is inclusive in this function.
inline bool validMappedRegion(uintptr_t base,uintptr_t last){
    return base>=0x1ff40000 && base<=0x1ff80000-MappedSharedWords*2 && !(base&31) &&
        last==base+(MappedSharedWords-1)*2;
}
// Once per loaded mapped component, after Ready attachment and before capture.
// Both tables occupy whole cache lines. A failure faults Session and requires
// firmware stop/unload before reclaiming or starting a different backend.
template<class Transport> bool initializeMappedTables(Transport& io,Session<Transport>& session,const int16_t* resample){
    if(session.state()!=State::Ready || !resample)return false;
    auto fail=[&](){session.cancel();return false;};
    if(!io.invalidate(0,32) || io.read(0x10)!=0x4458 || io.read(0)!=0)return fail();
    for(unsigned i=0;i<16;++i)io.write(0x2800+i,uint16_t(1u<<i));
    for(unsigned i=0;i<256;++i)io.write(0x4000+i,uint16_t(resample[i]));
    if(!io.flush(0x2800,16) || !io.flush(0x4000,256))return fail();
    return true;
}
}
