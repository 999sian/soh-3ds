#pragma once

#include "ship/utils/logging_3ds.h"
#include <stdio.h>

// Scene-eviction and heap-pressure records. These previously went only to
// stderr, which is visible under an emulator but is not captured to a file on
// hardware - so a physical run could not confirm whether the pressure sweep had
// fired, which is exactly the question the 07:52 run left open. Scene
// transitions happen a few times a minute, so opening and closing per record
// costs nothing and leaves a complete capture retrievable over FTP even if the
// game is killed rather than exited. Normal play opens no file.
static inline void Soh3dsWriteMemoryDiagnostic(const char* record) {
    if (!Soh3dsLoggingEnabled(SOH3DS_LOG_GENERAL)) return;
    FILE* file = fopen("memory.log", "ab");
    if (file) {
        fputs(record, file);
        fclose(file);
    }
}
