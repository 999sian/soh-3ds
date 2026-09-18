#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum SohDspProducerPause {
    SOH_DSP_PAUSE_LIFECYCLE=1,
    SOH_DSP_PAUSE_CONTROL=2,
    SOH_DSP_PAUSE_SHUTDOWN=4
};
// A successful try holds the gate through buffer accounting, synthesis and
// output submission. A paused/busy producer returns to its existing timed work
// wait; it must never block shutdown waiting for a lid wake event.
bool SohDspProducerTryBegin(void);
void SohDspProducerEnd(void);
// Control thread only, never from a producer callback. Pause waits for the
// current full batch to finish. Independent reasons prevent a control change
// from reopening the producer during an outstanding SDK sleep transition.
void SohDspProducerPause(unsigned reasons);
void SohDspProducerResume(unsigned reasons);
#ifdef __cplusplus
}
class SohDspProducerScope {
public:
    SohDspProducerScope():held_(SohDspProducerTryBegin()){}
    ~SohDspProducerScope(){if(held_)SohDspProducerEnd();}
    explicit operator bool()const{return held_;}
    SohDspProducerScope(const SohDspProducerScope&)=delete;
    SohDspProducerScope& operator=(const SohDspProducerScope&)=delete;
private:
    bool held_;
};
#endif
