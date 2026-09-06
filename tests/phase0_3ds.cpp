// Phase 0 acceptance test (SPEC.md section 7).
//
// Brings up a real Ship::Context on 3DS, mounts an .o2r from SD, loads a file
// out of it by name, and reports memory. No renderer — Phase 0 is about proving
// the archive and resource layer runs on-device. The graphics backend is Phase 1.
//
// Run under Azahar with scripts/run-in-azahar.sh, or copy the .3dsx to an SD
// card. Uses a real torch-extracted oot.o2r (39k entries, ~34 MB) so the
// measured heap numbers reflect the actual asset archive, not a toy one.

#include <3ds.h>

#include "ctr_log.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "ship/Context.h"
#include "ship/resource/File.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/archive/ArchiveManager.h"
#include "ship/utils/StrHash64.h"

namespace {

// R9: std::filesystem does not understand 3DS drive prefixes. absolute() turns
// "sdmc:/x" into "/sdmc:/x", and libultraship runs every archive path through
// absolute(), so a prefixed path silently never mounts. Un-prefixed paths work
// because sdmc is the default device. Both spellings are probed below to keep
// that documented and regression-checked.
const char* const kArchivePath = "/3ds/soh/oot.o2r";
const char* const kConfigPath = "/3ds/soh/config.json";
// A real entry from a torch-extracted oot.o2r.
const char* const kProbeFile = "objects/gameplay_keep/gArrow1Anim";

void ReportMemory(const char* when) {
    ctr_log("  [%-9s] linear %lu KiB, vram %lu KiB\n", when, (unsigned long)(linearSpaceFree() >> 10),
            (unsigned long)(vramSpaceFree() >> 10));
}

void ProbeFilesystem() {
    static const char* const kCandidates[] = { "sdmc:/3ds/soh/oot.o2r", "/3ds/soh/oot.o2r" };
    ctr_log("\nR9 -- std::filesystem vs 3DS drive prefixes\n");
    for (const char* cand : kCandidates) {
        std::error_code ec;
        const bool isFile = std::filesystem::is_regular_file(cand, ec);
        std::string abs;
        try {
            abs = std::filesystem::absolute(cand).string();
        } catch (const std::exception& e) {
            abs = std::string("threw: ") + e.what();
        }
        FILE* f = std::fopen(cand, "rb");
        ctr_log("  %-24s is_regular_file=%-3s fopen=%-4s absolute=\"%s\"\n", cand, isFile ? "yes" : "NO",
                f ? "ok" : "NULL", abs.c_str());
        if (f) {
            std::fclose(f);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    gfxInitDefault();
    ctr_log_init();

    bool isNew3ds = false;
    APT_CheckNew3DS(&isNew3ds);
#ifndef SOH3DS_EMULATOR_SAFE
    // Hangs under Azahar: ptm:sysm ConfigureNew3DSCPU is unimplemented there.
    if (isNew3ds) {
        osSetSpeedupEnable(true);
    }
#endif

    ctr_log("SoH-3DS Phase 0\n");
    ctr_log("model: %s | APPLICATION %lu KiB\n", isNew3ds ? "New 3DS" : "Old 3DS",
            (unsigned long)(osGetMemRegionSize(MEMREGION_APPLICATION) >> 10));
    ReportMemory("boot");

    ProbeFilesystem();

    bool ok = false;

    // --- real libultraship bring-up ------------------------------------------
    //
    // A Context is required, not optional: Archive::LoadFile(hash) resolves the
    // hash back to a path via Context::GetRawInstance()->GetResourceManager()
    // ->GetArchiveManager()->HashToString(). Without a Context that is a null
    // dereference, and the archive appears to mount while every lookup fails.
    ctr_log("\nContext::CreateUninitializedInstance\n");
    Ship::Context::CreateUninitializedInstance("SoH-3DS", "soh3ds", kConfigPath);
    auto* ctx = Ship::Context::GetRawInstance();
    ctr_log("  context   : %s\n", ctx ? "ok" : "NULL");

    if (ctx) {
        ctr_log("  InitLogging       : %s\n", ctx->InitLogging() ? "ok" : "FAILED");
        ctr_log("  InitConfiguration : %s\n", ctx->InitConfiguration() ? "ok" : "FAILED");
        ctr_log("  InitConsoleVariables: %s\n", ctx->InitConsoleVariables() ? "ok" : "FAILED");

        // reservedThreadCount 1: hardware_concurrency() is small here and the
        // resource thread pool would otherwise size itself to zero.
        const bool rm = ctx->InitResourceManager({ kArchivePath }, {}, 1);
        ctr_log("  InitResourceManager: %s\n", rm ? "ok" : "FAILED");
        ReportMemory("ctx init");

        auto resourceManager = ctx->GetResourceManager();
        auto archiveManager = resourceManager ? resourceManager->GetArchiveManager() : nullptr;
        ctr_log("  resourceManager   : %s\n", resourceManager ? "ok" : "NULL");
        ctr_log("  archiveManager    : %s\n", archiveManager ? "ok" : "NULL");

        if (archiveManager && archiveManager->IsLoaded()) {
            auto archives = archiveManager->GetArchives();
            ctr_log("  archives          : %u\n", archives ? (unsigned)archives->size() : 0u);

            ctr_log("\nArchiveManager::LoadFile(\"%s\")  [CRC64 %016llx]\n", kProbeFile,
                    (unsigned long long)CRC64(kProbeFile));
            auto file = archiveManager->LoadFile(kProbeFile);
            if (file == nullptr || file->Buffer == nullptr) {
                ctr_log("  FAILED: not found\n");
            } else {
                const size_t n = file->Buffer->size();
                ctr_log("  ok, %u bytes\n", (unsigned)n);

                std::string head(file->Buffer->data(), file->Buffer->data() + (n < 48 ? n : 48));
                for (char& c : head) {
                    if (c == '\n' || c == '\r') {
                        c = ' ';
                    }
                }
                ctr_log("  content: \"%s\"\n", head.c_str());
                ok = true;
            }
            ReportMemory("after load");
        } else {
            ctr_log("  FAILED: archive manager did not mount \"%s\"\n", kArchivePath);
        }
    }

    ctr_log("\n%s\n", ok ? "PHASE 0 PASS" : "PHASE 0 FAIL");
    ctr_log("START to exit.\n");

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) {
            break;
        }
        gspWaitForVBlank();
        gfxSwapBuffers();
    }

    gfxExit();
    return ok ? 0 : 1;
}
