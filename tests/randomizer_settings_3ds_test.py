#!/usr/bin/env python3
"""Execute the production native adapter with a small desktop option registry."""
from pathlib import Path
import subprocess
import re
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MENU = ROOT / "third_party/shipwright/soh/soh/Enhancements/dualscreen3ds"
DEPS = r'''
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define CVAR_RANDOMIZER_ENHANCEMENT(x) "gRandoEnhancements." x
inline std::map<std::string, float> floats;
inline float CVarGetFloat(const char* key, float fallback) { return floats.count(key) ? floats[key] : fallback; }
inline void CVarSetFloat(const char* key, float value) { floats[key] = value; }
#define CVAR_RANDOMIZER_SETTING(x) "gRando." x
inline std::map<std::string, int> ints;
inline std::map<std::string, std::string> strings;
inline bool busy = false;
inline int saves = 0;
inline std::string initialized;
namespace ShipInit { inline void Init(const char* cvar) { initialized = cvar; } }
inline bool Randomizer_IsGenerating() { return busy; }
inline long CVarGetInteger(const char* key, int fallback) { return ints.count(key) ? ints[key] : fallback; }
inline void CVarSetInteger(const char* key, int value) { ints[key] = value; }
inline const char* CVarGetString(const char* key, const char* fallback) { return strings.count(key) ? strings[key].c_str() : fallback; }
inline void CVarSetString(const char* key, const char* value) { strings[key] = value; }
inline void CVarSave() { ++saves; }
inline void CVarClear(const char* key) { ints.erase(key); strings.erase(key); floats.erase(key); }
extern "C" inline uint8_t Randomizer_ApplyPreset3DS(uint8_t preset) {
    if (preset != 3 || busy) return 0;
    std::erase_if(ints, [](const auto& p) { return p.first.starts_with("gRando."); });
    std::erase_if(strings, [](const auto& p) { return p.first.starts_with("gRando."); });
    return 1;
}
enum { RSK_SHOPSANITY_COUNT = 99, RO_LOGIC_GLITCHLESS = 0, RO_LOGIC_NO_LOGIC = 1 };
using RandomizerCheckArea = int;
namespace Rando {
struct Option {
    std::string name, cvar;
    int key = 0, initial = 0;
    bool disabled = false, hidden = false;
    std::vector<std::string> values{"Off", "On"};
    const std::string& GetName() const { return name; }
    const std::string& GetCVarName() const { return cvar; }
    const std::string& GetDescription() const { return name; }
    const std::string& GetDisabledText() const { return name; }
    size_t GetOptionCount() const { return values.size(); }
    int GetOptionIndex() const { return CVarGetInteger(cvar.c_str(), initial); }
    const std::string& GetOptionText(int index) const { return values.at(index); }
    int GetKey() const { return key; }
    bool IsDisabled() const { return disabled; }
    bool IsHidden() const { return hidden; }
};
struct Trick : Option { std::string GetNameTag() const { return "TestTrick"; } };
struct Group {
    std::string name;
    std::vector<Option*> options;
    const auto& GetName() const { return name; }
    const auto& GetOptions() const { return options; }
    const std::vector<Group*>& GetSubGroups() const { static const std::vector<Group*> empty; return empty; }
};
using OptionGroup = Group;
struct Tricks { static std::string GetAreaName(int) { return "Forest"; } };
struct Settings {
    std::array<Option, 4> options;
    Trick trick;
    Option location;
    std::vector<Group> groups;
    std::array<std::vector<Option*>, 1> locations;
    std::map<int, std::vector<int>> mTricksByArea{{0, {0}}};
    Settings() {
        options[0] = {"Starting Hearts", "gRando.StartingHearts", 0, 2, false, false, {"1", "2", "3", "4"}};
        options[1] = {"Dependent", "gRando.Dependent", 1};
        options[2] = {"Ungrouped", "gRando.Ungrouped", 2};
        options[3] = {"Internal", "", 3};
        trick.name = "Test trick";
        location = {"Forest chest", "", 42};
        locations[0].push_back(&location);
        groups = {{"Starting inventory", {&options[0], &options[1]}},
                  {"Duplicate", {&options[0]}}};
    }
    static auto GetInstance() { static auto instance = std::make_shared<Settings>(); return instance; }
    void RandomizeAllSettings() { ints["randomized"] = 1; }
    void UpdateAllOptions() { options[1].disabled = options[0].GetOptionIndex() == 0; }
    const auto& GetAllOptions() const { return options; }
    const auto& GetOptionGroups() const { return groups; }
    const auto& GetExcludeLocationsOptions() const { return locations; }
    const auto& GetTrickSetting(int) const { return trick; }
};
}
namespace RandomizerCheckObjects { inline std::string GetRCAreaName(int) { return "Forest"; } }
'''
HARNESS = r'''
#include "RandomizerSettings3DSTestDeps.h"
#include "RandomizerSettings3DS.h"
#include <cassert>
#include <cstring>
int main() {
    Soh3dsRandoSettings_Refresh();
    assert(Soh3dsRandoSettings_GroupCount() == 5); // Deduplicated + fallback + tricks + exclusions.
    assert(Soh3dsRandoSettings_Count(0) == 2);
    assert(std::string(Soh3dsRandoSettings_GroupName(4)) == "Randomizer enhancements");
    assert(Soh3dsRandoSettings_Count(4) == 10);
    Soh3dsRandoMenuRow row{};
    Soh3dsRandoSettings_Read(0, 0, &row);
    assert(std::strcmp(row.value, "3") == 0); // Missing CVar uses desktop default.
    assert(Soh3dsRandoSettings_Adjust(0, 0, -1));
    assert(ints["gRando.StartingHearts"] == 1 && saves == 1);
    Soh3dsRandoSettings_Adjust(0, 0, -1);
    Soh3dsRandoSettings_Read(0, 1, &row);
    assert(row.disabled && !Soh3dsRandoSettings_Adjust(0, 1, 1));
    Soh3dsRandoSettings_Adjust(0, 0, -1);
    assert(ints["gRando.StartingHearts"] == 0); // Lower bound, not unsigned wrap.
    for (int i = 0; i < 9; ++i) Soh3dsRandoSettings_Adjust(0, 0, 1);
    assert(ints["gRando.StartingHearts"] == 3);
    strings["gRando.EnabledTricks"] = "ExistingTrick,";
    Soh3dsRandoSettings_Adjust(2, 0, 1);
    assert(strings["gRando.EnabledTricks"] == "ExistingTrick,TestTrick");
    Soh3dsRandoSettings_Adjust(2, 0, -1);
    assert(strings["gRando.EnabledTricks"] == "ExistingTrick");
    strings["gRando.ExcludedLocations"] = "12";
    Soh3dsRandoSettings_Adjust(3, 0, 1);
    assert(strings["gRando.ExcludedLocations"] == "12,42");
    Soh3dsRandoSettings_Read(3, 0, &row);
    assert(std::strcmp(row.value, "Excluded") == 0);
    Soh3dsRandoSettings_Read(4, 0, &row);
    assert(std::strcmp(row.value, "On") == 0);
    Soh3dsRandoSettings_Adjust(4, 0, -1);
    assert(ints["gRandoEnhancements.RandoRelevantNavi"] == 0);
    assert(initialized == "gRandoEnhancements.RandoRelevantNavi");
    Soh3dsRandoSettings_Adjust(4, 8, 1);
    assert(floats["gRandoEnhancements.TimeSavers.SkipGetItemAnimationScale"] > 10.0f);
    Soh3dsRandoSettings_Adjust(4, 7, -1);
    assert(!Soh3dsRandoSettings_Adjust(4, 8, 1));
    ints["unrelated"] = 7;
    const int before = saves;
    busy = true;
    assert(!Soh3dsRandoSettings_Adjust(0, 0, -1));
    assert(!Soh3dsRandoSettings_Adjust(2, 0, 1));
    assert(!Soh3dsRandoSettings_Adjust(3, 0, 1));
    assert(!Soh3dsRandoSettings_Randomize());
    assert(!Soh3dsRandoSettings_Reset());
    assert(saves == before);
    busy = false;
    assert(Soh3dsRandoSettings_Randomize() && ints["randomized"] == 1);
    assert(!Soh3dsRandoSettings_Adjust(-1, 0, 1));
    assert(!Soh3dsRandoSettings_Adjust(0, 100, 1));
    assert(Soh3dsRandoSettings_Reset());
    Soh3dsRandoSettings_Read(0, 0, &row);
    assert(std::strcmp(row.value, "3") == 0);
    assert(strings["gRando.EnabledTricks"].empty() && strings["gRando.ExcludedLocations"].empty());
    assert(CVarGetInteger("gRandoEnhancements.RandoRelevantNavi", 1) == 1);
    assert(CVarGetFloat("gRandoEnhancements.TimeSavers.SkipGetItemAnimationScale", 10) == 10);
    assert(ints["unrelated"] == 7 && saves > before);
    // Preset/config changes are reflected without rebuilding the registry.
    ints["gRando.StartingHearts"] = 2;
    Soh3dsRandoSettings_Refresh();
    Soh3dsRandoSettings_Read(0, 0, &row);
    assert(std::strcmp(row.value, "3") == 0);
}
'''
with tempfile.TemporaryDirectory(prefix="soh-rando-settings-") as directory:
    temp = Path(directory)
    (temp / "RandomizerSettings3DSTestDeps.h").write_text(DEPS)
    (temp / "test.cpp").write_text(HARNESS)
    binary = temp / "test"
    subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-DSOH3DS_RANDO_SETTINGS_TEST",
                    "-I", str(temp), "-I", str(MENU), str(temp / "test.cpp"),
                    str(MENU / "RandomizerSettings3DS.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("3DS settings: defaults, coverage, dependencies, bounds, persistence and busy guards pass")

# Catch desktop controls added outside the seed registry, too.
desktop = (ROOT / "third_party/shipwright/soh/soh/SohGui/SohMenuRandomizer.cpp").read_text()
adapter = (MENU / "RandomizerSettings3DS.cpp").read_text()
pattern = r'CVAR_RANDOMIZER_ENHANCEMENT\("([^\"]+)"\)'
assert set(re.findall(pattern, desktop)) <= set(re.findall(pattern, adapter))
