#!/usr/bin/env python3
"""Exercise the production 3DS randomizer menu policy on the host."""

import json
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
MENU = ROOT / "third_party/shipwright/soh/soh/Enhancements/dualscreen3ds"
GLOBALS = ROOT / "third_party/shipwright/soh/soh/OTRGlobals.cpp"
ENTRANCES = ROOT / "third_party/shipwright/soh/soh/Enhancements/randomizer/entrance.cpp"
PRESETS = ROOT / "third_party/shipwright/soh/assets/custom/presets"


def function_body(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

HARNESS = r'''
#include "RandomizerMenu3DS.h"
#include "RandomizerSettings3DS.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

static std::string loadedSeed = "existing-seed";
static int generationStarts = 0;
static std::string importedPath;
static bool menuBusy = false;
static int appliedPreset = -1;
static int touchHeld = 0;
static int touchX = 0;
static int touchY = 0;
extern "C" const char gBuildVersion[] = "9.9.9-test";

static int hearts = 3;
static int randomizations = 0;
static int resets = 0;
extern "C" int Soh3dsRandoSettings_Reset() { ++resets; return 1; }
extern "C" int Soh3dsRandoSettings_Randomize() { ++randomizations; return 1; }
extern "C" void Soh3dsRandoSettings_Refresh() {}
extern "C" int Soh3dsRandoSettings_GroupCount() { return 1; }
extern "C" const char* Soh3dsRandoSettings_GroupName(int) { return "Starting inventory"; }
extern "C" int Soh3dsRandoSettings_Count(int) { return 9; }
extern "C" void Soh3dsRandoSettings_Read(int, int, Soh3dsRandoMenuRow* out) {
    *out = {};
    std::strcpy(out->label, "Starting Hearts");
    std::snprintf(out->value, sizeof(out->value), "%d", hearts);
}
extern "C" const char* Soh3dsRandoSettings_Description(int, int) { return "Starting heart containers. WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW"; }
extern "C" int Soh3dsRandoSettings_Adjust(int, int, int direction) { hearts += direction; return 1; }

static int ImportSeed(const char* path);

extern "C" bool Randomizer_IsGenerating() { return menuBusy; }
extern "C" uint8_t Randomizer_GenerateFromSeed(const char* seed) {
    ++generationStarts;
    loadedSeed = seed;
    return 1;
}
extern "C" uint8_t Randomizer_ImportSpoilerFile(const char* path) { return ImportSeed(path); }
extern "C" uint8_t Randomizer_ApplyPreset3DS(uint8_t preset) {
    appliedPreset = preset;
    return 1;
}
extern "C" int Soh3dsTouchRead(int* x, int* y) {
    *x = touchX;
    *y = touchY;
    return touchHeld;
}

static int ImportSeed(const char* path) {
    importedPath = path;
    loadedSeed = "replacement-seed";
    return 1;
}

static int StartSeed(const char* seed) {
    ++generationStarts;
    loadedSeed = seed;
    return 1;
}

static void Tap(int x, int y) {
    touchX = x;
    touchY = y;
    touchHeld = 1;
    Soh3dsRandoMenu_Update(0, 0, 0, 0, 0, 0);
    Soh3dsRandoMenu_Update(0, 0, 0, 0, 0, 0);
    touchHeld = 0;
    Soh3dsRandoMenu_Update(0, 0, 0, 0, 0, 0);
}

int main(int argc, char** argv) {
    assert(argc == 7);
    const char* directory = argv[1];
    const char* good = argv[2];
    const char* malformed = argv[3];
    const char* wrongVersion = argv[4];
    const char* oversized = argv[5];
    const char* expectedVersion = argv[6];

    Soh3dsRandoSeedFile files[SOH3DS_RANDO_MAX_FILES];
    const int count = Soh3dsRandoMenu_ListSeedFiles(directory, files, SOH3DS_RANDO_MAX_FILES);
    assert(count == SOH3DS_RANDO_MAX_FILES);
    assert(std::strcmp(files[0].name, "seed-000.json") == 0);
    assert(std::strcmp(files[count - 1].name, "seed-063.json") == 0);
    for (int i = 1; i < count; ++i) {
        assert(std::strcmp(files[i - 1].name, files[i].name) < 0);
    }
    for (int i = 0; i < count; ++i) {
        assert(std::strstr(files[i].name, ".json") != nullptr);
        assert(std::strstr(files[i].name, "nested") == nullptr);
    }

    assert(Soh3dsRandoMenu_ValidateSeedFile(good, expectedVersion) == SOH3DS_RANDO_OK);
    assert(Soh3dsRandoMenu_ValidateSeedFile(malformed, expectedVersion) == SOH3DS_RANDO_INVALID);
    assert(Soh3dsRandoMenu_ValidateSeedFile(wrongVersion, expectedVersion) == SOH3DS_RANDO_WRONG_VERSION);
    assert(Soh3dsRandoMenu_ValidateSeedFile(oversized, expectedVersion) == SOH3DS_RANDO_TOO_LARGE);

    // Validation and the busy gate happen before the engine import callback,
    // so neither a bad file nor a duplicate action can replace loaded state.
    assert(Soh3dsRandoMenu_TryImport(malformed, expectedVersion, 0, ImportSeed) == SOH3DS_RANDO_INVALID);
    assert(loadedSeed == "existing-seed");
    assert(Soh3dsRandoMenu_TryImport(good, expectedVersion, 1, ImportSeed) == SOH3DS_RANDO_BUSY);
    assert(loadedSeed == "existing-seed");
    assert(Soh3dsRandoMenu_TryImport(good, expectedVersion, 0, ImportSeed) == SOH3DS_RANDO_OK);
    assert(loadedSeed == "replacement-seed");

    generationStarts = 0;
    assert(Soh3dsRandoMenu_TryStart("seed-123", 1, StartSeed) == SOH3DS_RANDO_BUSY);
    assert(generationStarts == 0);
    assert(Soh3dsRandoMenu_TryStart("bad\nseed", 0, StartSeed) == SOH3DS_RANDO_INVALID);
    assert(generationStarts == 0);
    assert(Soh3dsRandoMenu_TryStart("seed-123", 0, StartSeed) == SOH3DS_RANDO_OK);
    assert(generationStarts == 1 && loadedSeed == "seed-123");
    assert(Soh3dsRandoMenu_TryStart("", 0, StartSeed) == SOH3DS_RANDO_OK); // blank means random
    assert(generationStarts == 2);

    Soh3dsRandoProgress_Begin();
    assert(Soh3dsRandoProgress_Stage() == SOH3DS_RANDO_STAGE_QUEUED);
    const unsigned before = Soh3dsRandoProgress_ElapsedSeconds();
    Soh3dsRandoProgress_SetStage(SOH3DS_RANDO_STAGE_PLACING_ITEMS);
    assert(Soh3dsRandoProgress_Stage() == SOH3DS_RANDO_STAGE_PLACING_ITEMS);
    assert(std::strcmp(Soh3dsRandoProgress_StageName(), "Placing items") == 0);
    assert(Soh3dsRandoProgress_ElapsedSeconds() >= before);
    Soh3dsRandoProgress_Finish(0);
    assert(Soh3dsRandoProgress_Stage() == SOH3DS_RANDO_STAGE_FAILED);
    Soh3dsRandoProgress_Begin();
    Soh3dsRandoProgress_Finish(1);
    assert(Soh3dsRandoProgress_Stage() == SOH3DS_RANDO_STAGE_COMPLETE);

    // Execute the production touch state machine: preset arrows obey busy,
    // all ten bounded picker pages are reachable, and touch Back cancels.
    Soh3dsRandoMenuView view{};
    Soh3dsRandoMenu_Open();
    Soh3dsRandoMenu_GetView(&view);
    assert(!view.picker && std::strstr(view.rows[0].value, "Standard"));
    Tap(280, 38);
    Soh3dsRandoMenu_GetView(&view);
    assert(std::strstr(view.rows[0].value, "Advanced"));
    menuBusy = true;
    Tap(280, 38);
    Soh3dsRandoMenu_Update(0, 0, 0, 0, 0, 1);
    Soh3dsRandoMenu_GetView(&view);
    assert(std::strstr(view.rows[0].value, "Advanced"));
    menuBusy = false;
    Tap(100, 90); // Seed file row.
    Soh3dsRandoMenu_GetView(&view);
    assert(view.picker && view.pageIndex == 0 && view.pageCount == 10 && view.rowCount == 7);
    for (int i = 0; i < 9; ++i) Tap(300, 10);
    Soh3dsRandoMenu_GetView(&view);
    assert(view.pageIndex == 9 && view.rowCount == 1);
    Tap(300, 10);
    Soh3dsRandoMenu_GetView(&view);
    assert(view.pageIndex == 9);
    Tap(250, 10);
    Soh3dsRandoMenu_GetView(&view);
    assert(view.pageIndex == 8);
    // Activate a paged row through the actual touch/update path.
    importedPath.clear();
    Tap(100, 42);
    Soh3dsRandoMenu_GetView(&view);
    assert(!view.picker && view.selectedRow == 3);
    assert(std::strstr(view.status, "Seed file loaded"));
    assert(importedPath.find("seed-056.json") != std::string::npos);
    Tap(100, 90);
    // Keyboard accept must load the next selected file as well.
    Soh3dsRandoMenu_Update(0, 0, 0, 1, 0, 0);
    Soh3dsRandoMenu_Update(1, 0, 0, 0, 0, 0);
    Soh3dsRandoMenu_GetView(&view);
    assert(!view.picker && importedPath.find("seed-001.json") != std::string::npos);
    Tap(100, 90);
    Tap(30, 10);
    Soh3dsRandoMenu_GetView(&view);
    assert(!view.picker && view.selectedRow == 2);
    const int startsBeforeTouch = generationStarts;
    Tap(100, 116); // Generate row.
    assert(generationStarts == startsBeforeTouch + 1);
    Soh3dsRandoProgress_Begin();
    Soh3dsRandoProgress_Finish(1);
    Soh3dsRandoMenu_GetView(&view);
    assert(std::strstr(view.status, "Complete") != nullptr);
    Tap(100, 116);
    Soh3dsRandoProgress_Begin();
    Soh3dsRandoProgress_Finish(0);
    Soh3dsRandoMenu_GetView(&view);
    assert(std::strstr(view.status, "Failed") != nullptr);
    // Full settings entry, category paging, edits, busy gating, and Back.
    Soh3dsRandoMenu_Open();
    Soh3dsRandoMenu_GetView(&view);
    assert(view.rowCount == 7);
    assert(std::string(view.rows[4].label) == "All settings");
    Tap(100, 142);
    Soh3dsRandoMenu_GetView(&view);
    assert(view.picker && std::string(view.rows[0].label) == "Starting inventory");
    Tap(100, 38);
    Soh3dsRandoMenu_GetView(&view);
    assert(std::string(view.rows[0].label) == "Starting Hearts");
    assert(std::string(view.rows[0].value) == "3");
    Soh3dsRandoMenu_Update(0, 0, 0, 0, 1, 0);
    Soh3dsRandoMenu_GetView(&view);
    assert(std::string(view.rows[0].value) == "2");
    menuBusy = true;
    Soh3dsRandoMenu_Update(0, 0, 0, 0, 1, 0);
    Soh3dsRandoMenu_GetView(&view);
    assert(view.rows[0].disabled && std::string(view.rows[0].value) == "2");
    menuBusy = false;
    Tap(280, 48); // Value arrow increments.
    Soh3dsRandoMenu_GetView(&view);
    assert(std::string(view.rows[0].value) == "3");
    Tap(180, 48); // Value arrow decrements.
    Soh3dsRandoMenu_GetView(&view);
    assert(std::string(view.rows[0].value) == "2");
    Tap(280, 36); // The entire upper name line opens details, never changes value.
    Soh3dsRandoMenu_GetView(&view);
    assert(hearts == 2);
    assert(view.help && std::string(view.rows[0].label) == "Starting Hearts");
    assert(view.pageCount > 1);
    for (int i = 0; i < view.rowCount; ++i) assert(std::strlen(view.rows[i].label) <= 32);
    Tap(300, 10);
    Soh3dsRandoMenu_GetView(&view);
    assert(view.help && view.pageIndex == 1);
    Soh3dsRandoMenu_Update(0, 1, 0, 0, 0, 0);
    Soh3dsRandoMenu_GetView(&view);
    assert(view.settings && view.selectedRow == 0);
    // D-pad can reach every row, including crossing a page boundary.
    for (int i = 0; i < 7; ++i) Soh3dsRandoMenu_Update(0, 0, 0, 1, 0, 0);
    Soh3dsRandoMenu_GetView(&view);
    assert(view.pageIndex == 1 && view.selectedRow == 0);
    Tap(250, 10);
    Tap(300, 10);
    Soh3dsRandoMenu_GetView(&view);
    assert(view.pageIndex == 1 && view.rowCount == 2);
    Tap(30, 10);
    Soh3dsRandoMenu_GetView(&view);
    assert(std::string(view.rows[0].label) == "Starting inventory");
    Soh3dsRandoMenu_Update(0, 1, 0, 0, 0, 0);
    Soh3dsRandoMenu_GetView(&view);
    assert(!view.picker && view.selectedRow == 4);
    Tap(100, 168); // Randomize settings action.
    assert(randomizations == 1);
    menuBusy = true;
    Tap(100, 168);
    assert(randomizations == 1);
    Tap(100, 194); // Reset is blocked during generation.
    assert(resets == 0 && Soh3dsRandoMenu_IsOpen());
    menuBusy = false;
    Soh3dsRandoMenu_GetView(&view);
    assert(std::string(view.rows[6].label) == "Reset to defaults");
    Tap(100, 194);
    assert(resets == 1 && Soh3dsRandoMenu_IsOpen());
    menuBusy = true;
    Tap(20, 10); // Header Back remains available during generation.
    assert(!Soh3dsRandoMenu_IsOpen());
    menuBusy = false;
    Soh3dsRandoMenu_Close();

    std::cout << "3DS randomizer menu: bounded picker, validation, busy gate and progress pass\n";
}
'''


def spoiler(version: str) -> dict:
    return {
        "version": version,
        "fileType": 3,
        "seed": "fixture-seed",
        "finalSeed": 123456789,
        "file_hash": [1, 2, 3, 4, 5],
        "settings": {},
        "locations": {},
    }


with tempfile.TemporaryDirectory(prefix="soh-rando-menu-") as temp:
    temp = Path(temp)
    picker = temp / "Randomizer"
    picker.mkdir()
    version = "9.9.9-test"
    for preset in ("Beginner", "Standard", "Advanced", "Reset to Default"):
        bundled = json.loads((PRESETS / f"Rando Seed Settings - {preset}.json").read_text())
        assert bundled["presetName"] == f"Rando Seed Settings - {preset}"
        assert "rando" in bundled["blocks"]
    for index in range(80):
        (picker / f"seed-{index:03}.json").write_text(json.dumps(spoiler(version)))
    (picker / "ignore.txt").write_text("not a seed")
    (picker / "UPPER.JSON").write_text(json.dumps(spoiler(version)))
    nested = picker / "nested"
    nested.mkdir()
    (nested / "nested.json").write_text(json.dumps(spoiler(version)))

    good = temp / "good.json"
    good.write_text(json.dumps(spoiler(version)))
    malformed = temp / "malformed.json"
    malformed.write_text('{"version":')
    wrong_version = temp / "wrong-version.json"
    wrong_version.write_text(json.dumps(spoiler("0.0.0")))
    oversized = temp / "oversized.json"
    oversized.write_bytes(b" " * (SOH3DS_MAX := 4 * 1024 * 1024 + 1))

    harness = temp / "test.cpp"
    harness.write_text(HARNESS)
    binary = temp / "test"
    subprocess.run(
        [
            os.environ.get("CXX", "g++"),
            "-std=c++20",
            "-O1",
            "-Wall",
            "-Wextra",
            "-DSOH3DS_RANDO_MENU_TEST",
            "-I",
            str(MENU),
            str(MENU / "RandomizerMenu3DS.cpp"),
            str(harness),
            "-o",
            str(binary),
        ],
        check=True,
    )

    # Compile and execute the production import wrapper with a Context double
    # that reproduces the real parser's dangerous failure order: mutate staged
    # state and the global entrance graph, catch internally, then report false.
    # The wrapper must republish the prior Context and entrance overrides.
    transaction = function_body(GLOBALS.read_text(), 'extern "C" uint8_t Randomizer_ImportSpoilerFile')
    transaction_harness = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <iostream>

#define SPDLOG_ERROR(...) ((void)0)
#define CVAR_GENERAL(x) x

static bool generating = false;
static bool parseSucceeds = false;
static bool restoreThrows = false;
static bool regionInitThrows = false;
static int entranceGraph = 7;
static int areaUpdates = 0;
static std::string savedPath = "old.json";
static int saves = 0;
bool IsRandoGenerating() { return generating; }
void CVarSetString(const char*, const char* value) { savedPath = value; }
void CVarSave() { ++saves; }

namespace Rando {
struct Context;
}
static Rando::Context* regionContext = nullptr;

namespace Rando {
struct EntranceShuffler {
    int desired = 0;
    void ApplyEntranceOverrides() {
        if (restoreThrows) throw std::runtime_error("restore failed");
        entranceGraph = desired;
    }
};
struct Logic {
    std::shared_ptr<Context> context;
    void SetContext(const std::shared_ptr<Context>& value) { context = value; }
};
struct Context : std::enable_shared_from_this<Context> {
    static std::shared_ptr<Context> current;
    static std::weak_ptr<Context> lastCreated;
    std::string seed = "empty";
    bool loaded = false;
    int locationInitializations = 0;
    std::shared_ptr<EntranceShuffler> entrances = std::make_shared<EntranceShuffler>();
    std::shared_ptr<Logic> logic = std::make_shared<Logic>();
    static std::shared_ptr<Context> CreateDetachedInstance() {
        auto candidate = std::make_shared<Context>();
        candidate->logic->SetContext(candidate);
        lastCreated = candidate;
        return candidate;
    }
    static void SetInstance(const std::shared_ptr<Context>& value) { current = value; }
    void AddExcludedOptions() { ++locationInitializations; }
    void ParseSpoiler(const char*) {
        seed = "partially-mutated";
        entranceGraph = -1;
        regionContext = this;
        if (parseSucceeds) {
            loaded = true;
            entrances->desired = 42;
            entranceGraph = 42;
        }
        // Failure mirrors Context::ParseSpoiler's internal catch: no throw,
        // IsSpoilerLoaded remains false after earlier mutations.
    }
    bool IsSpoilerLoaded() const { return loaded; }
    std::shared_ptr<EntranceShuffler> GetEntranceShuffler() { return entrances; }
    std::shared_ptr<Logic> GetLogic() { return logic; }
};
std::shared_ptr<Context> Context::current;
std::weak_ptr<Context> Context::lastCreated;
struct Settings {
    static Settings* GetInstance() { static Settings value; return &value; }
    std::shared_ptr<Context> assigned;
    void AssignContext(const std::shared_ptr<Context>& value) { assigned = value; }
};
}

void RegionTable_Init() {
    regionContext = Rando::Context::current.get();
    entranceGraph = 0;
    if (regionInitThrows) throw std::runtime_error("region init failed");
}
void SetAreas() { ++areaUpdates; }

struct Randomizer { bool SpoilerFileExists(const char*) { return true; } };
struct OTRGlobals {
    static OTRGlobals* Instance;
    std::shared_ptr<Randomizer> gRandomizer = std::make_shared<Randomizer>();
    std::shared_ptr<Rando::Context> gRandoContext;
};
OTRGlobals globals;
OTRGlobals* OTRGlobals::Instance = &globals;
''' + transaction + r'''

int main() {
    auto previous = std::make_shared<Rando::Context>();
    std::weak_ptr<Rando::Context> retiredPrevious = previous;
    previous->logic->SetContext(previous);
    previous->seed = "existing-seed";
    previous->loaded = true;
    previous->entrances->desired = 7;
    globals.gRandoContext = previous;
    Rando::Context::SetInstance(previous);
    Rando::Settings::GetInstance()->AssignContext(previous);

    // Settings must be restored even when the region-table rebuild itself
    // throws. The rejected Context must still be collectible afterward.
    regionInitThrows = true;
    parseSucceeds = false;
    assert(Randomizer_ImportSpoilerFile("region-init-fails.json") == 0);
    assert(regionContext == previous.get());
    assert(Rando::Settings::GetInstance()->assigned == previous);
    assert(Rando::Context::lastCreated.expired());
    regionInitThrows = false;

    // Even if graph restoration throws, the raw region binding is changed
    // back before the rejected candidate's ownership cycle is broken.
    restoreThrows = true;
    parseSucceeds = false;
    assert(Randomizer_ImportSpoilerFile("rollback-fails.json") == 0);
    assert(regionContext == previous.get());
    assert(Rando::Context::lastCreated.expired());
    restoreThrows = false;

    parseSucceeds = false;
    assert(Randomizer_ImportSpoilerFile("broken-nested.json") == 0);
    assert(globals.gRandoContext == previous);
    assert(Rando::Context::current == previous);
    assert(Rando::Settings::GetInstance()->assigned == previous);
    assert(previous->seed == "existing-seed" && previous->loaded);
    assert(entranceGraph == 7);
    assert(regionContext == previous.get() && areaUpdates == 1);
    assert(Rando::Context::lastCreated.expired());
    assert(savedPath == "old.json" && saves == 0);

    parseSucceeds = true;
    assert(Randomizer_ImportSpoilerFile("good.json") == 1);
    assert(globals.gRandoContext != previous);
    assert(Rando::Context::current == globals.gRandoContext);
    assert(Rando::Settings::GetInstance()->assigned == globals.gRandoContext);
    assert(globals.gRandoContext->seed == "partially-mutated");
    assert(globals.gRandoContext->locationInitializations == 1);
    assert(entranceGraph == 42);
    assert(savedPath == "good.json" && saves == 1);
    previous.reset();
    assert(retiredPrevious.expired());
    std::cout << "3DS randomizer import transaction rollback passes\n";
}
'''
    transaction_source = temp / "transaction.cpp"
    transaction_source.write_text(transaction_harness)
    transaction_binary = temp / "transaction"
    subprocess.run(
        [os.environ.get("CXX", "g++"), "-std=c++20", "-O1", "-Wall", "-Wextra", str(transaction_source),
         "-o", str(transaction_binary)],
        check=True,
        cwd=temp,
    )
    subprocess.run([str(transaction_binary)], check=True)

    # Compile the production spoiler entrance parser and graph adapter bodies
    # with a small graph double. An unknown nested entrance must throw before
    # any valid entry in the same file disconnects from the live graph.
    parse_entrances = function_body(
        ENTRANCES.read_text(), "void EntranceShuffler::ParseJson"
    )
    apply_entrances = function_body(
        ENTRANCES.read_text(), "void EntranceShuffler::ApplyEntranceOverrides"
    )
    entrance_harness = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace Rando {
struct EntranceOverride {
    uint8_t type = 0;
    int16_t index = 0;
    int16_t destination = 0;
    int16_t override = 0;
    int16_t overrideDestination = 0;
};
struct Entrance {
    int originalRegion = 0;
    int connectedRegion = 0;
    int disconnects = 0;
    bool shuffled = false;
    void Disconnect() { ++disconnects; connectedRegion = -1; }
    void Connect(int region) { connectedRegion = region; }
    int GetOriginalConnectedRegionKey() const { return originalRegion; }
    void SetAsShuffled() { shuffled = true; }
};
std::unordered_map<int16_t, Entrance*> entranceMap;
void SetAllEntrancesData() {}
struct EntranceShuffler {
    std::array<EntranceOverride, 4> entranceOverrides{};
    void UnshuffleAllEntrances() { entranceOverrides.fill({ 0, 0, 0, 0, 0 }); }
    void ParseJson(const nlohmann::json& spoilerFileJson);
    void ApplyEntranceOverrides();
};
}

static int regionInitializations = 0;
static int areaUpdates = 0;
void RegionTable_Init() { ++regionInitializations; }
void SetAreas() { ++areaUpdates; }

namespace Rando {
''' + parse_entrances + "\n" + apply_entrances + r'''
}

int main() {
    using nlohmann::json;
    Rando::Entrance original{ .originalRegion = 10, .connectedRegion = 10 };
    Rando::Entrance replacement{ .originalRegion = 20, .connectedRegion = 20 };
    Rando::entranceMap.emplace(1, &original);
    Rando::entranceMap.emplace(2, &replacement);
    Rando::EntranceShuffler shuffler;

    const json valid = { { "entrances", json::array({ {
        { "index", 1 }, { "destination", -1 }, { "override", 2 }, { "overrideDestination", -1 }
    } }) } };
    shuffler.ParseJson(valid);
    assert(original.disconnects == 1 && original.connectedRegion == 20 && original.shuffled);
    assert(regionInitializations == 1 && areaUpdates == 1);

    original.connectedRegion = 10;
    original.disconnects = 0;
    original.shuffled = false;
    const json invalid = { { "entrances", json::array({
        { { "index", 1 }, { "destination", -1 }, { "override", 2 }, { "overrideDestination", -1 } },
        { { "index", 32767 }, { "destination", -1 }, { "override", 32767 },
          { "overrideDestination", -1 } }
    }) } };
    try {
        shuffler.ParseJson(invalid);
        assert(false && "unknown entrance accepted");
    } catch (const std::invalid_argument&) {
    }
    assert(original.disconnects == 0 && original.connectedRegion == 10 && !original.shuffled);
    assert(regionInitializations == 2 && areaUpdates == 1);
    std::cout << "3DS spoiler entrance validation is atomic\n";
}
'''
    entrance_source = temp / "entrance.cpp"
    entrance_source.write_text(entrance_harness)
    entrance_binary = temp / "entrance"
    subprocess.run(
        [os.environ.get("CXX", "g++"), "-std=c++20", "-O1", "-Wall", "-Wextra", str(entrance_source),
         "-o", str(entrance_binary)],
        check=True,
        cwd=temp,
    )
    subprocess.run([str(entrance_binary)], check=True)
    subprocess.run(
        [
            str(binary),
            str(picker),
            str(good),
            str(malformed),
            str(wrong_version),
            str(oversized),
            version,
        ],
        check=True,
        cwd=temp,
    )
