#include "setup_3ds.h"
#include "setup_core.h"
#include "setup_version.h"

#include <3ds.h>
#include <cstdio>
#include <string>
#include <sys/iosupport.h>
#include <ship/utils/logging_3ds.h>

namespace {
bool ready[2] = {};

bool ValidateInstalled(std::string& error) {
    ready[0] = ready[1] = false;
    if (!Soh3dsSetup::ValidateArchive("soh.o2r", SOH3DS_SETUP_VERSION,
                                    Soh3dsSetup::ArchiveKind::Support, false, error)) return false;
    std::string normalError, mqError;
    ready[0] = Soh3dsSetup::ValidateArchive("oot.o2r", SOH3DS_SETUP_VERSION,
                                         Soh3dsSetup::ArchiveKind::Game, false, normalError);
    ready[1] = Soh3dsSetup::ValidateArchive("oot-mq.o2r", SOH3DS_SETUP_VERSION,
                                         Soh3dsSetup::ArchiveKind::Game, false, mqError);
    if (ready[0] || ready[1]) return true;
    error = "oot.o2r: " + normalError + "\noot-mq.o2r: " + mqError;
    return false;
}
}

extern "C" bool Soh3dsGameArchiveReady(bool masterQuest) {
    return ready[masterQuest ? 1 : 0];
}

extern "C" void Soh3dsRestoreConsoleOutput() {}

extern "C" bool Soh3dsEnsureGameData() {
    std::string error;
    if (ValidateInstalled(error)) return true;
    gfxInitDefault();
    consoleInit(GFX_TOP, nullptr);
    bool success = false;
    while (aptMainLoop()) {
        consoleClear();
        std::printf("Ship of Harkinian 3DS\n\n"
                    "Copy compatible soh.o2r and\noot.o2r (or oot-mq.o2r) into\n/3ds/soh on the SD card.\n\n"
                    "Generate game archives using desktop SoH.\n"
                    "This build does not extract ROMs.\n\n%s\n\nA: retry   START: exit\n", error.c_str());
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
        hidScanInput();
        const u32 pressed = hidKeysDown();
        if (pressed & KEY_START) break;
        if ((pressed & KEY_A) && ValidateInstalled(error)) {
            success = true;
            break;
        }
    }
    Soh3dsConfigureDebugOutput();
    devoptab_list[STD_OUT] = devoptab_list[STD_ERR];
    setvbuf(stdout, nullptr, _IONBF, 0);
    gfxExit();
    return success;
}
