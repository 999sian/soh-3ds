#pragma once

#include "ship/utils/logging_3ds.h"
#include <stdio.h>

// Audio-worker summaries only, once per several seconds. Close each record
// so FTP can retrieve a complete capture after exiting the game. Normal play
// never opens a file; failed diagnostic output must not stop playback.
static inline void Soh3dsWriteAudioDiagnostic(const char* record) {
    if (!Soh3dsLoggingEnabled(SOH3DS_LOG_GENERAL)) return;
    FILE* file = fopen("audio-perf.log", "ab");
    if (file) {
        fputs(record, file);
        fclose(file);
    }
}
