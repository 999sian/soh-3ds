#include "RandomizerMenu3DS.h"
#include "RandomizerSettings3DS.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef __3DS__
extern "C" {
#include <3ds/applets/swkbd.h>
}
#include "soh/OTRGlobals.h"
extern "C" const char gBuildVersion[];
#endif

#if defined(__3DS__) || defined(SOH3DS_RANDO_MENU_TEST)
extern "C" int Soh3dsTouchRead(int* x, int* y) __attribute__((weak));
#ifdef SOH3DS_RANDO_MENU_TEST
extern "C" uint8_t Randomizer_GenerateFromSeed(const char* seed);
extern "C" uint8_t Randomizer_ImportSpoilerFile(const char* fileLoc);
extern "C" uint8_t Randomizer_ApplyPreset3DS(uint8_t presetIndex);
extern "C" bool Randomizer_IsGenerating();
extern "C" const char gBuildVersion[];
#endif
#endif

namespace {

constexpr uintmax_t kMaximumSeedFileBytes = 4U * 1024U * 1024U;
constexpr std::array<const char*, 4> kPresets = {
    "Beginner",
    "Standard",
    "Advanced",
    "Reset to Default",
};
constexpr std::array<const char*, 7> kStageNames = {
    "Idle", "Queued", "Applying settings", "Placing items", "Writing seed file", "Complete", "Failed",
};

std::atomic<int> sStage{ SOH3DS_RANDO_STAGE_IDLE };
std::atomic<int64_t> sStartedNanoseconds{ 0 };

int64_t MonotonicNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool IsSeedTextValid(const char* seed) {
    if (seed == nullptr) {
        return false;
    }
    const size_t length = std::strlen(seed);
    if (length > SOH3DS_RANDO_SEED_MAX) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = static_cast<unsigned char>(seed[i]);
        if (c < 32 || c > 126) {
            return false;
        }
    }
    return true;
}

bool HasJsonExtension(const std::filesystem::path& path) {
    return path.extension() == ".json";
}

void Copy(char* out, size_t size, const std::string& value) {
    if (size == 0) {
        return;
    }
    std::snprintf(out, size, "%s", value.c_str());
}

#if defined(__3DS__) || defined(SOH3DS_RANDO_MENU_TEST)

enum MenuPage { PAGE_SETUP, PAGE_FILES, PAGE_GROUPS, PAGE_SETTINGS, PAGE_HELP };

struct MenuState {
    bool open = false;
    MenuPage page = PAGE_SETUP;
    int selected = 0;
    int filePage = 0;
    int preset = 1;
    int group = 0;
    int setting = 0;
    std::vector<std::string> help;
    char seed[SOH3DS_RANDO_SEED_MAX + 1] = {};
    std::string status;
    std::vector<Soh3dsRandoSeedFile> files;
    bool generationPending = false;
    int touchFrames = 0;
    bool touchDown = false;
    int touchX = 0;
    int touchY = 0;
};

MenuState sMenu;

int StartGeneration(const char* seed) {
    return Randomizer_GenerateFromSeed(seed) != 0;
}

int ImportSeed(const char* path) {
    return Randomizer_ImportSpoilerFile(path) != 0;
}

const char* ResultText(int result) {
    switch (result) {
        case SOH3DS_RANDO_OK:
            return "Ready";
        case SOH3DS_RANDO_BUSY:
            return "Generator is already busy";
        case SOH3DS_RANDO_WRONG_VERSION:
            return "Seed is for another SoH version";
        case SOH3DS_RANDO_TOO_LARGE:
            return "Seed file is too large";
        case SOH3DS_RANDO_INVALID:
            return "Invalid seed or JSON file";
        default:
            return "Operation failed";
    }
}

void RefreshFiles() {
    Soh3dsRandoSeedFile found[SOH3DS_RANDO_MAX_FILES];
    const int count = Soh3dsRandoMenu_ListSeedFiles("./Randomizer", found, SOH3DS_RANDO_MAX_FILES);
    sMenu.files.assign(found, found + std::max(count, 0));
    sMenu.page = PAGE_FILES;
    sMenu.selected = 0;
    sMenu.filePage = 0;
    sMenu.status = count == 0 ? "No JSON seeds in /Randomizer" : "A: load   B: cancel";
}

void EditSeed() {
#ifdef __3DS__
    char candidate[SOH3DS_RANDO_SEED_MAX + 1];
    std::snprintf(candidate, sizeof(candidate), "%s", sMenu.seed);
    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_QWERTY, 2, SOH3DS_RANDO_SEED_MAX);
    swkbdSetHintText(&keyboard, "Blank creates a random seed");
    swkbdSetInitialText(&keyboard, candidate);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, "OK", true);
    // This is called by the file-select update, outside display-list recording.
    if (swkbdInputText(&keyboard, candidate, sizeof(candidate)) != SWKBD_BUTTON_RIGHT) {
        sMenu.status = "Seed entry cancelled";
        return;
    }
    if (!IsSeedTextValid(candidate)) {
        sMenu.status = ResultText(SOH3DS_RANDO_INVALID);
        return;
    }
    std::snprintf(sMenu.seed, sizeof(sMenu.seed), "%s", candidate);
    sMenu.status = sMenu.seed[0] == '\0' ? "A random seed will be used" : "Seed text updated";
#else
    sMenu.status = "Seed entry cancelled";
#endif
}

int RowCount() {
    switch (sMenu.page) {
        case PAGE_SETUP: return 7;
        case PAGE_FILES: return static_cast<int>(sMenu.files.size());
        case PAGE_GROUPS: return Soh3dsRandoSettings_GroupCount();
        case PAGE_SETTINGS: return Soh3dsRandoSettings_Count(sMenu.group);
        case PAGE_HELP: return static_cast<int>(sMenu.help.size());
    }
    return 0;
}

void Back() {
    switch (sMenu.page) {
        case PAGE_HELP:
            sMenu.page = PAGE_SETTINGS;
            sMenu.selected = sMenu.setting;
            sMenu.status = "Left/right: edit   A: details   B: back";
            break;
        case PAGE_SETTINGS:
            sMenu.page = PAGE_GROUPS;
            sMenu.selected = sMenu.group;
            break;
        case PAGE_GROUPS:
            sMenu.page = PAGE_SETUP;
            sMenu.selected = 4;
            break;
        case PAGE_FILES:
            sMenu.page = PAGE_SETUP;
            sMenu.selected = 2;
            sMenu.status = "File selection cancelled";
            break;
        default: Soh3dsRandoMenu_Close(); return;
    }
    sMenu.filePage = sMenu.selected / SOH3DS_RANDO_VISIBLE_ROWS;
}

void AdjustSetting(int direction) {
    if (Randomizer_IsGenerating()) {
        sMenu.status = ResultText(SOH3DS_RANDO_BUSY);
        return;
    }
    if (Soh3dsRandoSettings_Adjust(sMenu.group, sMenu.selected, direction))
        sMenu.status = "Saved. Applies to the next generated seed.";
    else
        sMenu.status = "Unavailable. A: details";
}

void ShowHelp() {
    Soh3dsRandoMenuRow row{};
    Soh3dsRandoSettings_Read(sMenu.group, sMenu.selected, &row);
    sMenu.help.clear();
    std::string text = std::string(row.label) + "\nCurrent: " + row.value + "\n" +
                       Soh3dsRandoSettings_Description(sMenu.group, sMenu.selected);
    // Keep all of long names/descriptions accessible on the small screen.
    while (!text.empty()) {
        // Font glyphs advance at most 9 pixels; 32 fit within 292 pixels.
        size_t end = std::min<size_t>(32, text.size());
        const size_t newline = text.find('\n');
        if (newline <= end) end = newline;
        else if (end < text.size()) {
            const size_t space = text.rfind(' ', end);
            if (space != std::string::npos && space > 0) end = space;
        }
        sMenu.help.push_back(text.substr(0, end));
        text.erase(0, end);
        if (!text.empty() && (text.front() == ' ' || text.front() == '\n')) text.erase(0, 1);
    }
    sMenu.setting = sMenu.selected;
    sMenu.page = PAGE_HELP;
    sMenu.selected = sMenu.filePage = 0;
    sMenu.status = "Up/down: scroll   B: back";
}

void ActivateSetupRow() {
    if (Randomizer_IsGenerating()) {
        sMenu.status = ResultText(SOH3DS_RANDO_BUSY);
        return;
    }
    switch (sMenu.selected) {
        case 0:
            if (Randomizer_ApplyPreset3DS(sMenu.preset)) {
                sMenu.status = std::string(kPresets[sMenu.preset]) + " preset applied";
            } else {
                sMenu.status = ResultText(SOH3DS_RANDO_FAILED);
            }
            break;
        case 1:
            EditSeed();
            break;
        case 2:
            RefreshFiles();
            break;
        case 3: {
            const int result = Soh3dsRandoMenu_TryStart(sMenu.seed, Randomizer_IsGenerating(), StartGeneration);
            sMenu.status = result == SOH3DS_RANDO_OK ? "Generation started" : ResultText(result);
            sMenu.generationPending = result == SOH3DS_RANDO_OK;
            break;
        }
        case 4:
            Soh3dsRandoSettings_Refresh();
            sMenu.page = PAGE_GROUPS;
            sMenu.selected = sMenu.filePage = 0;
            sMenu.status = "A: open   B: back";
            break;
        case 5:
            sMenu.status = Soh3dsRandoSettings_Randomize() ? "Settings randomized and saved" : ResultText(SOH3DS_RANDO_BUSY);
            break;
        case 6:
            if (Soh3dsRandoSettings_Reset()) {
                sMenu.preset = 3;
                sMenu.status = "Default settings restored and saved";
            } else sMenu.status = ResultText(SOH3DS_RANDO_FAILED);
            break;
    }
}

void ActivateFileRow() {
    if (sMenu.files.empty()) {
        sMenu.page = PAGE_SETUP;
        sMenu.selected = 2;
        return;
    }
    const int result = Soh3dsRandoMenu_TryImport(sMenu.files[sMenu.selected].path, (const char*)gBuildVersion,
                                                  Randomizer_IsGenerating(), ImportSeed);
    sMenu.status = result == SOH3DS_RANDO_OK ? "Seed file loaded" : ResultText(result);
    if (result == SOH3DS_RANDO_OK) {
        sMenu.page = PAGE_SETUP;
        sMenu.selected = 3;
    }
}

void HandleTouch(bool& accept) {
    int x = 0;
    int y = 0;
    const bool held = Soh3dsTouchRead != nullptr && Soh3dsTouchRead(&x, &y);
    if (held) {
        if (++sMenu.touchFrames < 2) {
            return;
        }
        sMenu.touchDown = true;
        sMenu.touchX = x;
        sMenu.touchY = y;
        return;
    }
    sMenu.touchFrames = 0;
    if (!sMenu.touchDown) {
        return;
    }
    sMenu.touchDown = false;
    if (sMenu.touchY < 28) {
        if (sMenu.touchX < 80) {
            Back();
        } else if (sMenu.page != PAGE_SETUP && sMenu.touchX >= 240 && RowCount() > 0) {
            const int pageCount = (RowCount() + SOH3DS_RANDO_VISIBLE_ROWS - 1) /
                                  SOH3DS_RANDO_VISIBLE_ROWS;
            if (sMenu.touchX < 280) {
                sMenu.filePage = std::max(0, sMenu.filePage - 1);
            } else {
                sMenu.filePage = std::min(pageCount - 1, sMenu.filePage + 1);
            }
            sMenu.selected = sMenu.filePage * SOH3DS_RANDO_VISIBLE_ROWS;
        }
        return;
    }
    constexpr int kRowsY = 30;
    constexpr int kRowHeight = 26;
    const int visible = (sMenu.touchY - kRowsY) / kRowHeight;
    if (sMenu.touchY < kRowsY || visible < 0 || visible >= SOH3DS_RANDO_VISIBLE_ROWS) {
        return;
    }
    if (sMenu.page == PAGE_SETUP) {
        if (visible < 7) {
            sMenu.selected = visible;
            if (visible == 0 && sMenu.touchX >= 166) {
                if (Randomizer_IsGenerating()) {
                    sMenu.status = ResultText(SOH3DS_RANDO_BUSY);
                    return;
                }
                const int direction = sMenu.touchX < 236 ? static_cast<int>(kPresets.size()) - 1 : 1;
                sMenu.preset = (sMenu.preset + direction) % kPresets.size();
                sMenu.status = std::string("Selected ") + kPresets[sMenu.preset];
                return;
            }
            accept = true;
        }
        return;
    }
    const int picked = sMenu.filePage * SOH3DS_RANDO_VISIBLE_ROWS + visible;
    if (picked < RowCount()) {
        sMenu.selected = picked;
        if (sMenu.page == PAGE_SETTINGS && sMenu.touchX >= 166 &&
            (sMenu.touchY - kRowsY) % kRowHeight >= 12)
            AdjustSetting(sMenu.touchX < 236 ? -1 : 1);
        else accept = true;
    }
}

#endif

} // namespace

extern "C" int Soh3dsRandoMenu_ListSeedFiles(const char* directory, Soh3dsRandoSeedFile* out, int capacity) {
    if (directory == nullptr || out == nullptr || capacity <= 0) {
        return 0;
    }
    capacity = std::min(capacity, SOH3DS_RANDO_MAX_FILES);
    std::vector<std::filesystem::path> paths;
    paths.reserve(capacity);
    try {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || !HasJsonExtension(entry.path())) {
                continue;
            }
            if (entry.file_size() > kMaximumSeedFileBytes) {
                continue;
            }
            const auto position = std::lower_bound(paths.begin(), paths.end(), entry.path(), [](const auto& left, const auto& right) {
                return left.filename().string() < right.filename().string();
            });
            if (static_cast<int>(paths.size()) < capacity) {
                paths.insert(position, entry.path());
            } else if (position != paths.end()) {
                paths.insert(position, entry.path());
                paths.pop_back();
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        return 0;
    }
    const int count = std::min(capacity, static_cast<int>(paths.size()));
    for (int i = 0; i < count; ++i) {
        Copy(out[i].path, sizeof(out[i].path), paths[i].string());
        Copy(out[i].name, sizeof(out[i].name), paths[i].filename().string());
    }
    return count;
}

extern "C" int Soh3dsRandoMenu_ValidateSeedFile(const char* path, const char* expectedVersion) {
    if (path == nullptr || path[0] == '\0' || expectedVersion == nullptr) {
        return SOH3DS_RANDO_INVALID;
    }
    try {
        const std::filesystem::path file(path);
        if (!std::filesystem::is_regular_file(file)) {
            return SOH3DS_RANDO_INVALID;
        }
        if (std::filesystem::file_size(file) > kMaximumSeedFileBytes) {
            return SOH3DS_RANDO_TOO_LARGE;
        }
        std::ifstream stream(file);
        nlohmann::json json;
        stream >> json;
        if (!json.is_object() || !json.contains("version") || !json["version"].is_string() ||
            !json.contains("seed") || !json["seed"].is_string() || json["seed"].get_ref<const std::string&>().size() > 1024 ||
            !json.contains("finalSeed") || !json["finalSeed"].is_number_unsigned() || !json.contains("file_hash") ||
            !json["file_hash"].is_array() || json["file_hash"].size() != 5 || !json.contains("settings") ||
            !json["settings"].is_object() || !json.contains("locations") || !json["locations"].is_object()) {
            return SOH3DS_RANDO_INVALID;
        }
        for (const auto& icon : json["file_hash"]) {
            if (!icon.is_number_unsigned() || icon.get<unsigned>() >= 100) {
                return SOH3DS_RANDO_INVALID;
            }
        }
        if (json["version"].get_ref<const std::string&>() != expectedVersion) {
            return SOH3DS_RANDO_WRONG_VERSION;
        }
        if (json.contains("fileType") && (!json["fileType"].is_number_integer() || json["fileType"].get<int>() != 3)) {
            return SOH3DS_RANDO_INVALID;
        }
        return SOH3DS_RANDO_OK;
    } catch (const std::exception&) {
        return SOH3DS_RANDO_INVALID;
    }
}

extern "C" int Soh3dsRandoMenu_TryImport(const char* path, const char* expectedVersion, int busy,
                                           Soh3dsRandoAction importer) {
    if (busy) {
        return SOH3DS_RANDO_BUSY;
    }
    const int validation = Soh3dsRandoMenu_ValidateSeedFile(path, expectedVersion);
    if (validation != SOH3DS_RANDO_OK) {
        return validation;
    }
    return importer != nullptr && importer(path) ? SOH3DS_RANDO_OK : SOH3DS_RANDO_FAILED;
}

extern "C" int Soh3dsRandoMenu_TryStart(const char* seed, int busy, Soh3dsRandoAction starter) {
    if (busy) {
        return SOH3DS_RANDO_BUSY;
    }
    if (!IsSeedTextValid(seed)) {
        return SOH3DS_RANDO_INVALID;
    }
    return starter != nullptr && starter(seed) ? SOH3DS_RANDO_OK : SOH3DS_RANDO_FAILED;
}

extern "C" void Soh3dsRandoProgress_Begin(void) {
    sStartedNanoseconds.store(MonotonicNanoseconds(), std::memory_order_relaxed);
    sStage.store(SOH3DS_RANDO_STAGE_QUEUED, std::memory_order_release);
}

extern "C" void Soh3dsRandoProgress_SetStage(int stage) {
    if (stage >= SOH3DS_RANDO_STAGE_IDLE && stage <= SOH3DS_RANDO_STAGE_FAILED) {
        sStage.store(stage, std::memory_order_release);
    }
}

extern "C" void Soh3dsRandoProgress_Finish(int success) {
    Soh3dsRandoProgress_SetStage(success ? SOH3DS_RANDO_STAGE_COMPLETE : SOH3DS_RANDO_STAGE_FAILED);
}

extern "C" int Soh3dsRandoProgress_Stage(void) {
    return sStage.load(std::memory_order_acquire);
}

extern "C" const char* Soh3dsRandoProgress_StageName(void) {
    const int stage = Soh3dsRandoProgress_Stage();
    return kStageNames[std::clamp(stage, 0, static_cast<int>(kStageNames.size()) - 1)];
}

extern "C" uint32_t Soh3dsRandoProgress_ElapsedSeconds(void) {
    const int64_t start = sStartedNanoseconds.load(std::memory_order_relaxed);
    if (start <= 0 || Soh3dsRandoProgress_Stage() == SOH3DS_RANDO_STAGE_IDLE) {
        return 0;
    }
    const int64_t elapsed = std::max<int64_t>(0, MonotonicNanoseconds() - start);
    return static_cast<uint32_t>(elapsed / 1000000000LL);
}

#if defined(__3DS__) || defined(SOH3DS_RANDO_MENU_TEST)
extern "C" void Soh3dsRandoMenu_Open(void) {
    sMenu.open = true;
    sMenu.page = PAGE_SETUP;
    sMenu.selected = 0;
    sMenu.status = "A: select   B: close";
}

extern "C" void Soh3dsRandoMenu_Close(void) {
    sMenu.open = false;
    sMenu.touchDown = false;
    sMenu.touchFrames = 0;
}

extern "C" int Soh3dsRandoMenu_IsOpen(void) {
    return sMenu.open;
}

extern "C" void Soh3dsRandoMenu_Update(int accept, int cancel, int up, int down, int left, int right) {
    if (!sMenu.open) {
        return;
    }
    bool activate = accept != 0;
    HandleTouch(activate);
    if (cancel) { Back(); return; }
    const int count = std::max(1, RowCount());
    if (up != down) {
        sMenu.selected = (sMenu.selected + (down ? 1 : count - 1)) % count;
        if (sMenu.page != PAGE_SETUP) sMenu.filePage = sMenu.selected / SOH3DS_RANDO_VISIBLE_ROWS;
    }
    if (sMenu.page == PAGE_SETUP && sMenu.selected == 0 && left != right && !Randomizer_IsGenerating()) {
        sMenu.preset = (sMenu.preset + (right ? 1 : static_cast<int>(kPresets.size()) - 1)) % kPresets.size();
    } else if (sMenu.page == PAGE_SETUP && sMenu.selected == 0 && left != right) {
        sMenu.status = ResultText(SOH3DS_RANDO_BUSY);
    }
    if (sMenu.page == PAGE_SETTINGS && left != right) AdjustSetting(right ? 1 : -1);
    if (activate) {
        switch (sMenu.page) {
            case PAGE_SETUP: ActivateSetupRow(); break;
            case PAGE_FILES: ActivateFileRow(); break;
            case PAGE_GROUPS:
                if (RowCount() > 0) {
                    sMenu.group = sMenu.selected;
                    sMenu.page = PAGE_SETTINGS;
                    sMenu.selected = sMenu.filePage = 0;
                    sMenu.status = "Left/right: edit   A: details   B: back";
                }
                break;
            case PAGE_SETTINGS: ShowHelp(); break;
            case PAGE_HELP: Back(); break;
        }
    }
}

extern "C" void Soh3dsRandoMenu_GetView(Soh3dsRandoMenuView* out) {
    if (out == nullptr) {
        return;
    }
    std::memset(out, 0, sizeof(*out));
    out->busy = Randomizer_IsGenerating();
    out->picker = sMenu.page != PAGE_SETUP;
    out->settings = sMenu.page == PAGE_SETTINGS;
    out->help = sMenu.page == PAGE_HELP;
    Copy(out->title, sizeof(out->title), sMenu.page == PAGE_SETUP ? "RANDOMIZER SETTINGS" : "LOAD SEED JSON");
    if (sMenu.page == PAGE_GROUPS) Copy(out->title, sizeof(out->title), "ALL SETTINGS");
    if (sMenu.page == PAGE_SETTINGS) Copy(out->title, sizeof(out->title), Soh3dsRandoSettings_GroupName(sMenu.group));
    if (sMenu.page == PAGE_HELP) Copy(out->title, sizeof(out->title), "SETTING DETAILS");
    if (out->busy) {
        Copy(out->status, sizeof(out->status), std::string(Soh3dsRandoProgress_StageName()) + "  " +
                                                   std::to_string(Soh3dsRandoProgress_ElapsedSeconds()) + "s");
    } else {
        if (sMenu.generationPending) {
            const int stage = Soh3dsRandoProgress_Stage();
            if (stage == SOH3DS_RANDO_STAGE_COMPLETE || stage == SOH3DS_RANDO_STAGE_FAILED) {
                sMenu.status = std::string(Soh3dsRandoProgress_StageName()) + "  " +
                               std::to_string(Soh3dsRandoProgress_ElapsedSeconds()) + "s";
                sMenu.generationPending = false;
            }
        }
        Copy(out->status, sizeof(out->status), sMenu.status);
    }
    if (sMenu.page == PAGE_SETUP) {
        static const char* labels[7] = { "Preset", "Seed", "Seed file", "Generate", "All settings", "Randomize settings", "Reset to defaults" };
        out->rowCount = 7;
        out->selectedRow = sMenu.selected;
        for (int i = 0; i < out->rowCount; ++i) {
            Copy(out->rows[i].label, sizeof(out->rows[i].label), labels[i]);
            out->rows[i].disabled = out->busy;
        }
        Copy(out->rows[0].value, sizeof(out->rows[0].value), std::string("< ") + kPresets[sMenu.preset] + " >");
        Copy(out->rows[1].value, sizeof(out->rows[1].value), sMenu.seed[0] == '\0' ? "Random" : sMenu.seed);
        Copy(out->rows[2].value, sizeof(out->rows[2].value), "Choose...");
        return;
    }
    if (sMenu.page != PAGE_FILES) {
        const int count = RowCount();
        out->pageCount = std::max(1, (count + SOH3DS_RANDO_VISIBLE_ROWS - 1) / SOH3DS_RANDO_VISIBLE_ROWS);
        out->pageIndex = sMenu.filePage;
        const int first = sMenu.filePage * SOH3DS_RANDO_VISIBLE_ROWS;
        out->rowCount = std::min(SOH3DS_RANDO_VISIBLE_ROWS, count - first);
        out->selectedRow = sMenu.selected - first;
        for (int i = 0; i < out->rowCount; ++i) {
            if (sMenu.page == PAGE_GROUPS)
                Copy(out->rows[i].label, sizeof(out->rows[i].label), Soh3dsRandoSettings_GroupName(first + i));
            else if (sMenu.page == PAGE_SETTINGS) {
                Soh3dsRandoSettings_Read(sMenu.group, first + i, &out->rows[i]);
                out->rows[i].disabled |= out->busy;
            } else Copy(out->rows[i].label, sizeof(out->rows[i].label), sMenu.help[first + i]);
        }
        return;
    }
    if (sMenu.files.empty()) {
        out->rowCount = 1;
        out->selectedRow = 0;
        Copy(out->rows[0].label, sizeof(out->rows[0].label), "No seed files found");
        return;
    }
    out->pageCount = (static_cast<int>(sMenu.files.size()) + SOH3DS_RANDO_VISIBLE_ROWS - 1) /
                     SOH3DS_RANDO_VISIBLE_ROWS;
    out->pageIndex = sMenu.filePage;
    const int first = sMenu.filePage * SOH3DS_RANDO_VISIBLE_ROWS;
    out->rowCount = std::min(SOH3DS_RANDO_VISIBLE_ROWS, static_cast<int>(sMenu.files.size()) - first);
    out->selectedRow = sMenu.selected - first;
    for (int i = 0; i < out->rowCount; ++i) {
        Copy(out->rows[i].label, sizeof(out->rows[i].label), sMenu.files[first + i].name);
        out->rows[i].disabled = out->busy;
    }
}
#else
extern "C" void Soh3dsRandoMenu_Open(void) {}
extern "C" void Soh3dsRandoMenu_Close(void) {}
extern "C" int Soh3dsRandoMenu_IsOpen(void) { return 0; }
extern "C" void Soh3dsRandoMenu_Update(int, int, int, int, int, int) {}
extern "C" void Soh3dsRandoMenu_GetView(Soh3dsRandoMenuView* out) {
    if (out != nullptr) std::memset(out, 0, sizeof(*out));
}
#endif
