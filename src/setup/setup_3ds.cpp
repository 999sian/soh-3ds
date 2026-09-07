#include "setup_3ds.h"

#ifdef __3DS__

#include "setup_core.h"
#include "setup_bundle.h"
#include "torch_adapter.h"
#include "setup_version.h"

#ifndef SOH3DS_SETUP_VERSION
#error "setup_version.h must define SOH3DS_SETUP_VERSION from the Ship build version"
#endif

#include <3ds.h>
#include <ship/utils/logging_3ds.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <malloc.h>
#include <string>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char* kSupportSource = "romfs:/soh.o2r.gz";
constexpr const char* kMetadataFile = "romfs:/roms.tsv";
constexpr const char* kTorchMetadata = ".setup/torch";
constexpr const char* kStagingDirectory = ".setup";
constexpr uint64_t kFreeSpaceReserve = 64ULL * 1024 * 1024;
bool gGameArchiveReady[2] = { false, false };
const devoptab_t* gSetupConsoleOutput = nullptr;

struct UiProgress {
    std::string lastStage;
    unsigned lastPercent = 101;
    unsigned lastDiagnosticPercent = 101;
};

class ExtractionCleanup {
  public:
    ~ExtractionCleanup() {
        std::string ignored;
        Soh3dsSetup::CleanupSetupArtifacts(kStagingDirectory, ignored);
    }
};

void Present() {
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

void Header(const char* subtitle) {
    consoleClear();
    std::printf("Ship of Harkinian 3DS\n");
    std::printf("======================\n\n%s\n\n", subtitle);
}

bool WaitRetry(const std::string& message) {
    Header("First-run setup could not continue");
    std::printf("%s\n\n", message.c_str());
    std::printf("A: retry\nSTART: exit to HOME Menu\n");
    Present();
    while (aptMainLoop()) {
        hidScanInput();
        const u32 pressed = hidKeysDown();
        if ((pressed & KEY_A) != 0) return true;
        if ((pressed & KEY_START) != 0) return false;
        gspWaitForVBlank();
    }
    return false;
}

bool SetupProgress(const char* stage, size_t done, size_t total, void* user) {
    if (!aptMainLoop()) return false;
    hidScanInput();
    if ((hidKeysDown() & KEY_START) != 0) return false;

    UiProgress* state = static_cast<UiProgress*>(user);
    const unsigned percent = total == 0 ? 0 : static_cast<unsigned>((static_cast<uint64_t>(done) * 100) / total);
    // Torch alternates parsing and writing for every asset. Display one phase
    // so these checkpoints still poll input without adding a vblank per asset.
    const std::string current = stage == nullptr ? "Working" :
                                std::strcmp(stage, "Writing assets") == 0 ? "Extracting assets" : stage;
    const bool stageChanged = current != state->lastStage;
    if (stageChanged || state->lastDiagnosticPercent > 100 || percent >= state->lastDiagnosticPercent + 5 ||
        percent == 100) {
        const struct mallinfo memory = mallinfo();
        char line[256];
        std::snprintf(line, sizeof(line),
                      "soh-3ds setup: stage=%s done=%llu total=%llu percent=%u heap-used=%uKiB heap-free=%uKiB\n",
                      current.c_str(), static_cast<unsigned long long>(done), static_cast<unsigned long long>(total),
                      percent, static_cast<unsigned>(memory.uordblks / 1024),
                      static_cast<unsigned>(memory.fordblks / 1024));
        std::fputs(line, stderr);
        state->lastDiagnosticPercent = percent;
    }
    if (current != state->lastStage || percent != state->lastPercent) {
        Header("Preparing game data");
        std::printf("%s\n\n%u%%  (%llu / %llu)\n\n", current.c_str(), percent,
                    static_cast<unsigned long long>(done), static_cast<unsigned long long>(total));
        std::printf("START: cancel\n");
        Present();
        state->lastStage = current;
        state->lastPercent = percent;
    }
    return true;
}

bool IsRegularFile(const std::string& path) {
    struct stat info = {};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool HasRomExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string extension = name.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".z64" || extension == ".n64" || extension == ".v64";
}

std::vector<std::string> DiscoverRoms(std::string& error) {
    std::vector<std::string> roms;
    DIR* directory = ::opendir(".");
    if (directory == nullptr) {
        error = std::string("Could not scan /3ds/soh: ") + std::strerror(errno);
        return roms;
    }
    while (dirent* entry = ::readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == ".." || !HasRomExtension(name) || !IsRegularFile(name)) continue;
        roms.push_back(name);
    }
    ::closedir(directory);
    std::sort(roms.begin(), roms.end());
    return roms;
}

bool ChooseRom(const std::vector<std::string>& roms, std::string& selected) {
    size_t index = 0;
    while (aptMainLoop()) {
        Header("Select an Ocarina of Time ROM");
        const size_t first = index > 5 ? index - 5 : 0;
        const size_t last = std::min(roms.size(), first + 11);
        for (size_t i = first; i < last; ++i) {
            std::printf("%c %.44s\n", i == index ? '>' : ' ', roms[i].c_str());
        }
        std::printf("\nD-pad: select   A: use ROM\nSTART: exit\n");
        Present();
        hidScanInput();
        const u32 pressed = hidKeysDown();
        if ((pressed & KEY_DUP) != 0 && index != 0) --index;
        if ((pressed & KEY_DDOWN) != 0 && index + 1 < roms.size()) ++index;
        if ((pressed & KEY_A) != 0) {
            selected = roms[index];
            return true;
        }
        if ((pressed & KEY_START) != 0) return false;
        gspWaitForVBlank();
    }
    return false;
}

bool MakeStagingDirectory(std::string& error) {
    if (::mkdir(kStagingDirectory, 0777) == 0 || errno == EEXIST) return true;
    error = std::string("Could not create .setup: ") + std::strerror(errno);
    return false;
}

bool InstalledDataIsUsable(std::string& error) {
    gGameArchiveReady[0] = false;
    gGameArchiveReady[1] = false;
    std::string supportError;
    const bool supportReady =
        Soh3dsSetup::ValidateArchive("soh.o2r", SOH3DS_SETUP_VERSION, Soh3dsSetup::ArchiveKind::Support, false,
                                     supportError);
    std::string normalError;
    std::string masterQuestError;
    const bool normalReady =
        Soh3dsSetup::ValidateArchive("oot.o2r", SOH3DS_SETUP_VERSION, Soh3dsSetup::ArchiveKind::Game, false,
                                     normalError);
    const bool masterQuestReady =
        Soh3dsSetup::ValidateArchive("oot-mq.o2r", SOH3DS_SETUP_VERSION, Soh3dsSetup::ArchiveKind::Game, false,
                                     masterQuestError);
    if (!supportReady) {
        error = supportError;
        return false;
    }
    gGameArchiveReady[0] = normalReady;
    gGameArchiveReady[1] = masterQuestReady;
    if (!normalReady && !masterQuestReady) {
        error = "Neither game archive is usable. oot.o2r: " + normalError + "; oot-mq.o2r: " + masterQuestError;
        return false;
    }
    return true;
}

bool InstallSupportArchive(UiProgress& progress, std::string& error) {
    std::string currentError;
    if (Soh3dsSetup::ValidateArchive("soh.o2r", SOH3DS_SETUP_VERSION, Soh3dsSetup::ArchiveKind::Support,
                                     false, currentError)) {
        return true;
    }
    uint64_t available = 0;
    if (!Soh3dsSetup::HasDiskHeadroom(".", 32ULL * 1024 * 1024, kFreeSpaceReserve, available, error)) return false;
    const std::string staged = std::string(kStagingDirectory) + "/soh.o2r";
    if (!Soh3dsSetup::InflateGzipFile(kSupportSource, staged, 32 * 1024 * 1024,
                                    SetupProgress, &progress, error)) return false;
    if (!Soh3dsSetup::ValidateArchive(staged, SOH3DS_SETUP_VERSION, Soh3dsSetup::ArchiveKind::Support, true, error,
                                      SetupProgress, &progress)) {
        std::remove(staged.c_str());
        error = "The staged support archive did not validate. " + error;
        return false;
    }
    if (!Soh3dsSetup::AtomicInstall(staged, "soh.o2r", error)) return false;
    return true;
}

bool ExtractSelectedRom(const std::string& selected, const std::vector<Soh3dsSetup::RomMetadata>& metadata,
                        UiProgress& progress, std::string& error) {
    if (!Soh3dsSetup::CleanupSetupArtifacts(kStagingDirectory, error)) return false;
    ExtractionCleanup cleanup;
    struct stat romInfo = {};
    if (::stat(selected.c_str(), &romInfo) != 0 || romInfo.st_size < 4) {
        error = "The selected ROM could not be read.";
        return false;
    }
    constexpr uint64_t kRetailRomSize = 32ULL * 1024 * 1024;
    if (static_cast<uint64_t>(romInfo.st_size) != kRetailRomSize) {
        error = "On-device setup requires a 32 MiB retail Ocarina of Time ROM; debug dumps are not supported.";
        return false;
    }
    uint64_t available = 0;
    // A normalized ROM plus the uncompressed game archive are each around one ROM in size.
    // Reserve another ROM-sized margin as well as 64 MiB for saves and filesystem overhead.
    const uint64_t required = static_cast<uint64_t>(romInfo.st_size) * 3 + 16ULL * 1024 * 1024;
    if (!Soh3dsSetup::HasDiskHeadroom(".", required, kFreeSpaceReserve, available, error)) {
        error += " Need about " + std::to_string((required + kFreeSpaceReserve) / (1024 * 1024)) +
                 " MiB; available " + std::to_string(available / (1024 * 1024)) + " MiB.";
        return false;
    }

    const std::string normalized = std::string(kStagingDirectory) + "/normalized.z64";
    std::string sha1;
    if (!Soh3dsSetup::NormalizeRom(selected, normalized, SetupProgress, &progress, sha1, error)) return false;
    const Soh3dsSetup::RomMetadata* match = Soh3dsSetup::FindRomMetadata(metadata, sha1);
    if (match == nullptr) {
        std::remove(normalized.c_str());
        error = "This ROM is not supported (normalized SHA-1 " + sha1 + ").";
        return false;
    }

    const std::string bundle = "romfs:/torch/" + match->metadataVariant + ".tar.gz";
    if (!Soh3dsSetup::UnpackMetadata(bundle, kTorchMetadata, match->metadataVariant,
                                   SetupProgress, &progress, error)) return false;

    std::string outputName;
    const bool extracted = Soh3dsSetup::ExtractRom(normalized, kTorchMetadata, kStagingDirectory,
                                                   SOH3DS_SETUP_VERSION, SetupProgress, &progress, outputName, error);
    std::remove(normalized.c_str());
    if (!extracted) return false;
    if (outputName != match->outputArchive) {
        error = "Extractor output did not match the selected ROM metadata.";
        return false;
    }
    const std::string staged = std::string(kStagingDirectory) + "/" + outputName;
    if (!Soh3dsSetup::ValidateArchive(staged, SOH3DS_SETUP_VERSION, Soh3dsSetup::ArchiveKind::Game, true, error,
                                      SetupProgress, &progress)) {
        error = "The generated game archive did not pass validation. " + error;
        return false;
    }
    if (!Soh3dsSetup::AtomicInstall(staged, outputName, error)) return false;
    return true;
}

void ReleaseSetupUi() {
    // consoleInit redirects stdout at the framebuffer. Restore it to the SVC
    // debug stream before gfxExit invalidates that framebuffer.
    if (Soh3dsConfigureDebugOutput) Soh3dsConfigureDebugOutput();
    else consoleDebugInit(debugDevice_NULL);
    devoptab_list[STD_OUT] = devoptab_list[STD_ERR];
    setvbuf(stdout, nullptr, _IONBF, 0);
    gfxExit();
    romfsExit();
}

} // namespace

extern "C" void Soh3dsRestoreConsoleOutput() {
    if (gSetupConsoleOutput != nullptr) {
        devoptab_list[STD_OUT] = gSetupConsoleOutput;
        setvbuf(stdout, nullptr, _IONBF, 0);
    }
}

extern "C" bool Soh3dsEnsureGameData() {
    gfxInitDefault();
    consoleInit(GFX_TOP, nullptr);
    gSetupConsoleOutput = devoptab_list[STD_OUT];
    // Keep setup text on stdout while restoring stderr diagnostics to the
    // SVC sink installed by the early constructor.
    if (Soh3dsConfigureDebugOutput) Soh3dsConfigureDebugOutput();
    else consoleDebugInit(debugDevice_NULL);
    setvbuf(stderr, nullptr, _IONBF, 0);
#ifndef SOH3DS_EMULATOR_SAFE
    osSetSpeedupEnable(true);
#endif

    while (R_FAILED(romfsInit())) {
        if (!WaitRetry("The bundled setup files could not be mounted. Reinstall the complete 3DSX or CIA, then press A.")) {
            if (Soh3dsConfigureDebugOutput) Soh3dsConfigureDebugOutput();
            else consoleDebugInit(debugDevice_NULL);
            devoptab_list[STD_OUT] = devoptab_list[STD_ERR];
            gfxExit();
            return false;
        }
    }

    ExtractionCleanup cleanup;
    bool success = false;
    while (aptMainLoop()) {
        std::string error;
        if (!MakeStagingDirectory(error)) {
            if (!WaitRetry(error)) break;
            continue;
        }
        if (!Soh3dsSetup::CleanupSetupArtifacts(kStagingDirectory, error)) {
            if (!WaitRetry(error)) break;
            continue;
        }
        if (InstalledDataIsUsable(error)) {
            success = true;
            break;
        }

        UiProgress progress;
        if (!InstallSupportArchive(progress, error)) {
            if (!WaitRetry(error)) break;
            continue;
        }
        if (InstalledDataIsUsable(error)) {
            success = true;
            break;
        }

        std::vector<Soh3dsSetup::RomMetadata> metadata;
        if (!Soh3dsSetup::LoadRomMetadata(kMetadataFile, metadata, error)) {
            if (!WaitRetry(error)) break;
            continue;
        }
        std::vector<std::string> roms = DiscoverRoms(error);
        if (roms.empty()) {
            if (error.empty()) {
                error = "No .z64, .n64 or .v64 ROM was found in /3ds/soh. Copy a supported, uncompressed ROM "
                        "there, then press A.";
            }
            if (!WaitRetry(error)) break;
            continue;
        }
        std::string selected;
        if (!ChooseRom(roms, selected)) break;

        progress = {};
        if (!ExtractSelectedRom(selected, metadata, progress, error)) {
            if (!WaitRetry(error)) break;
            continue;
        }
        if (!InstalledDataIsUsable(error)) {
            if (!WaitRetry("Installed data failed its final boot check. " + error)) break;
            continue;
        }
        Header("Setup complete");
        std::printf("Game data installed successfully.\nStarting the game...\n");
        Present();
        success = true;
        break;
    }

    ReleaseSetupUi();
    return success;
}

extern "C" bool Soh3dsGameArchiveReady(bool masterQuest) {
    return gGameArchiveReady[masterQuest ? 1 : 0];
}

#else

extern "C" bool Soh3dsEnsureGameData() {
    return false;
}

extern "C" bool Soh3dsGameArchiveReady(bool) {
    return false;
}
extern "C" void Soh3dsRestoreConsoleOutput() {
}

#endif
