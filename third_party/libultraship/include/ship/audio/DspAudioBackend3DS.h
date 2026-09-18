#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Soh3dsDspAudioBackendApi {
    bool (*initialize)(unsigned rate,unsigned desiredBuffered);
    void (*close)(void);
    void (*shutdown)(void);
    int (*buffered)(void);
    void (*play)(const uint8_t*,size_t);
} Soh3dsDspAudioBackendApi;
const Soh3dsDspAudioBackendApi* Soh3dsDspAudioBackend(void);
#ifdef __cplusplus
}
#endif
