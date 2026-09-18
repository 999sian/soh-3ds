#pragma once
namespace ResidentDsp {
// libctru DSP_UnloadComponent clears its loaded flag BEFORE IPC succeeds.
// Retrying after an error can return0 without contacting the service. Never
// treat that no-op as confirmation that DMA/scratch ownership has ended.
class ComponentStopGuard {
public:
    template<class Stop> bool stop(Stop&& operation){
        if(uncertain_)return false;
        uncertain_=true;
        if(!operation())return false;
        uncertain_=false;return true;
    }
    bool uncertain()const{return uncertain_;}
private:
    bool uncertain_=false;
};
}
