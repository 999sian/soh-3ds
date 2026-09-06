// SoH-3DS: libultra spellings, matching the headers. devkitARM makes
// int32_t `long int`, a different type from s32.
#include "libultraship/libultraship.h"
#include "AudioDmaRegistry.h"

extern "C" {

void osCreatePiManager(OSPri pri, OSMesgQueue* cmdQ, OSMesg* cmdBuf, s32 cmdMsgCnt) {
}

s32 osPiReadIo(u32 a, u32* b) {
    return 0;
}

s32 osPiWriteIo(u32 devAddr, u32 data) {
    return 0;
}

s32 osPiStartDma(OSIoMesg* mb, s32 priority, s32 direction, uintptr_t devAddr, void* vAddr, size_t nbytes,
                     OSMesgQueue* mq) {
    // On N64, DMA reads from ROM which has no bounds — the last chunk of
    // an audio sample can extend past the data. On PC, clamp to the blob size
    // and zero-fill the remainder so ADPCM decoding sees silence.
    size_t safeBytes = AudioDma_Clamp(devAddr, nbytes);
    if (safeBytes < nbytes) {
        memset(vAddr, 0, nbytes);
    }
    memcpy(vAddr, (const void*)devAddr, safeBytes);
    return 0;
}
}