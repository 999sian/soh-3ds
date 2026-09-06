// SoH-3DS: libultra spellings, matching the headers. devkitARM makes
// int32_t `long int`, a different type from s32.
#include "libultraship/libultraship.h"

#ifdef __3DS__
// SoH-3DS: these queues now cross physical cores (game thread on core 0,
// audio synthesis on core 2) with no atomics or barriers - torn
// validCount/first updates corrupt the command stream feeding synthesis.
// One short-held lock; traffic is a handful of messages per frame.
#include <mutex>
static std::mutex sMesgLock;
#define MESG_GUARD() std::lock_guard<std::mutex> guard(sMesgLock)
#else
#define MESG_GUARD() ((void)0)
#endif

extern "C" {

__OSEventState __osEventStateTab[OS_NUM_EVENTS] = { 0 };

void osCreateMesgQueue(OSMesgQueue* mq, OSMesg* msgBuf, s32 count) {
    mq->validCount = 0;
    mq->first = 0;
    mq->msgCount = count;
    mq->msg = msgBuf;
    return;
}

s32 osSendMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    MESG_GUARD();
    s32 index;
    if (mq->validCount >= mq->msgCount) {
        return -1;
    }
    index = (mq->first + mq->validCount) % mq->msgCount;
    mq->msg[index] = msg;
    mq->validCount++;

    return 0;
}

s32 osJamMesg(OSMesgQueue* mq, OSMesg msg, s32 flag) {
    MESG_GUARD();
    if (mq->validCount == 0) {
        return -1;
    }

    mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
    mq->msg[mq->first] = msg;
    mq->validCount++;

    return 0;
}

s32 osRecvMesg(OSMesgQueue* mq, OSMesg* msg, s32 flag) {
    MESG_GUARD();
    if (mq->validCount == 0) {
        return -1;
    }
    if (msg != NULL) {
        *msg = *(mq->first + mq->msg);
    }
    mq->first = (mq->first + 1) % mq->msgCount;
    mq->validCount--;

    return 0;
}

void osSetEventMesg(OSEvent event, OSMesgQueue* mq, OSMesg msg) {

    __OSEventState* es = &__osEventStateTab[event];

    es->queue = mq;
    es->msg = msg;
}
}