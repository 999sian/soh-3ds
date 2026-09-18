#include <cassert>
#include <cstdio>
#include "component_stop_guard.h"
int main(){
    // Match the SDK flag mutation, including the misleading retry success.
    bool loaded=true;unsigned requests=0;
    auto sdkUnload=[&](){if(!loaded)return true;loaded=false;++requests;return false;};
    ResidentDsp::ComponentStopGuard guard;
    assert(!guard.stop(sdkUnload) && guard.uncertain() && requests==1);
    assert(sdkUnload() && requests==1); // raw SDK no-op is not stop evidence
    assert(!guard.stop(sdkUnload) && guard.uncertain() && requests==1);
    ResidentDsp::ComponentStopGuard confirmed;
    assert(confirmed.stop([](){return true;}) && !confirmed.uncertain());
    assert(confirmed.stop([](){return true;}));
    std::puts("PASS: failed DSP unload remains uncertain despite SDK no-op retry success");
}
