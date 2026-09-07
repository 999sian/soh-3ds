// Native settings adapter: share the desktop registry and persisted seed CVars.
#if defined(__3DS__) || defined(SOH3DS_RANDO_SETTINGS_TEST)
#include "RandomizerSettings3DS.h"
#ifdef SOH3DS_RANDO_SETTINGS_TEST
#include "RandomizerSettings3DSTestDeps.h"
#else
#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/OTRGlobals.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/randomizer/settings.h"
#include "soh/Enhancements/randomizer/randomizer_check_objects.h"
#endif
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace {
enum class Kind { Setting, Trick, Location };
struct Entry { const Rando::Option* option; Kind kind; std::string token; };
struct Group { std::string name; std::vector<Entry> entries; };
std::vector<Group> groups;

// These desktop Randomizer controls are enhancements, not seed Options.
struct Enhancement {
    const char* name;
    const char* cvar;
    const char* description;
    int defaultValue;
};
const Enhancement enhancements[] = {
    {"Rando-Relevant Navi Hints", CVAR_RANDOMIZER_ENHANCEMENT("RandoRelevantNavi"), "Replace Navi's overworld quest hints with randomizer hints.", 1},
    {"Random Rupee Names", CVAR_RANDOMIZER_ENHANCEMENT("RandomizeRupeeNames"), "Randomize the name shown when obtaining Rupees.", 1},
    {"Map & Compass Colors Match Dungeon", CVAR_RANDOMIZER_ENHANCEMENT("ColoredMapsAndCompasses"), "Match shuffled map and compass colors to their dungeon.", 1},
    {"Jabber Nut Colors Match Kind", CVAR_RANDOMIZER_ENHANCEMENT("GenericJabberNutModel"), "With Shuffle Speak, use generic jabber nut models and colors.", 1},
    {"Quest Item Fanfares", CVAR_RANDOMIZER_ENHANCEMENT("QuestItemFanfares"), "Play unique fanfares for medallions, stones and songs.", 0},
    {"Mysterious Shuffled Items", CVAR_RANDOMIZER_ENHANCEMENT("MysteriousShuffle"), "Hide shuffled items behind mystery models and names.", 0},
    {"Simpler Boss Soul Models", CVAR_RANDOMIZER_ENHANCEMENT("SimplerBossSoulModels"), "Use simpler boss soul models, which can improve performance.", 0},
    {"Skip Get Item Animations", CVAR_RANDOMIZER_ENHANCEMENT("TimeSavers.SkipGetItemAnimation"), "Skip pickup animations for junk items or all items.", 1},
    {"Item Scale", CVAR_RANDOMIZER_ENHANCEMENT("TimeSavers.SkipGetItemAnimationScale"), "Pickup item size from 5 to 15, while Skip Get Item Animations is enabled.", 10},
    {"Signs Hint Entrances", CVAR_RANDOMIZER_ENHANCEMENT("EntrancesOnSigns"), "Signs near loading zones tell you where they lead.", 0},
};
constexpr int kEnhancementCount = sizeof(enhancements) / sizeof(enhancements[0]);
bool IsEnhancement(int group, int row) {
    return group == static_cast<int>(groups.size()) && row >= 0 && row < kEnhancementCount;
}
bool EnhancementDisabled(int row) {
    return row == 8 && !CVarGetInteger(enhancements[7].cvar, enhancements[7].defaultValue);
}

const Entry* Find(int group, int row) {
    if (group < 0 || group >= static_cast<int>(groups.size()) || row < 0 ||
        row >= static_cast<int>(groups[group].entries.size())) return nullptr;
    return &groups[group].entries[row];
}
const char* ListCVar(Kind kind) {
    return kind == Kind::Trick ? CVAR_RANDOMIZER_SETTING("EnabledTricks") :
                                CVAR_RANDOMIZER_SETTING("ExcludedLocations");
}
std::set<std::string> Tokens(Kind kind) {
    std::set<std::string> values;
    std::istringstream input(CVarGetString(ListCVar(kind), ""));
    std::string token;
    while (std::getline(input, token, ',')) if (!token.empty()) values.insert(token);
    return values;
}
void Add(Group group) { if (!group.entries.empty()) groups.push_back(std::move(group)); }
}

extern "C" uint8_t Randomizer_ApplyPreset3DS(uint8_t presetIndex);

extern "C" int Soh3dsRandoSettings_Reset(void) {
    if (Randomizer_IsGenerating() || !Randomizer_ApplyPreset3DS(3)) return 0;
    for (const auto& enhancement : enhancements) CVarClear(enhancement.cvar);
    for (const auto& enhancement : enhancements) ShipInit::Init(enhancement.cvar);
    Rando::Settings::GetInstance()->UpdateAllOptions();
    CVarSave();
    return 1;
}

extern "C" int Soh3dsRandoSettings_Randomize(void) {
    if (Randomizer_IsGenerating()) return 0;
    Rando::Settings::GetInstance()->RandomizeAllSettings();
    Rando::Settings::GetInstance()->UpdateAllOptions();
    CVarSave();
    return 1;
}

extern "C" void Soh3dsRandoSettings_Refresh(void) {
    if (Randomizer_IsGenerating()) return;
    auto settings = Rando::Settings::GetInstance();
    settings->UpdateAllOptions();
    if (!groups.empty()) return;
    std::set<const Rando::Option*> available, seen;
    for (const auto& option : settings->GetAllOptions()) {
        if (!option.GetCVarName().empty() && option.GetOptionCount() != 0) available.insert(&option);
    }
    // Qualify short leaf names such as "Other" with their desktop parent.
    std::map<const Rando::OptionGroup*, std::string> parents;
    for (const auto& parent : settings->GetOptionGroups()) {
        for (const auto* child : parent.GetSubGroups()) parents.emplace(child, parent.GetName());
    }
    // The fallback catches any registered setting without a desktop group.
    for (const auto& source : settings->GetOptionGroups()) {
        const auto parent = parents.find(&source);
        Group group{parent == parents.end() ? source.GetName() : parent->second + " / " + source.GetName(), {}};
        for (const auto* option : source.GetOptions()) {
            if (available.count(option) && seen.insert(option).second)
                group.entries.push_back({option, Kind::Setting, {}});
        }
        Add(std::move(group));
    }
    Group other{"Other settings", {}};
    for (const auto& option : settings->GetAllOptions()) {
        if (available.count(&option) && seen.insert(&option).second)
            other.entries.push_back({&option, Kind::Setting, {}});
    }
    Add(std::move(other));
    for (const auto& [area, tricks] : settings->mTricksByArea) {
        Group group{"Tricks: " + Rando::Tricks::GetAreaName(area), {}};
        for (auto key : tricks) {
            const auto& trick = settings->GetTrickSetting(key);
            if (!trick.GetNameTag().empty()) group.entries.push_back({&trick, Kind::Trick, trick.GetNameTag()});
        }
        Add(std::move(group));
    }
    const auto& areas = settings->GetExcludeLocationsOptions();
    for (size_t area = 0; area < areas.size(); ++area) {
        Group group{"Exclude: " + RandomizerCheckObjects::GetRCAreaName(static_cast<RandomizerCheckArea>(area)), {}};
        for (const auto* option : areas[area])
            group.entries.push_back({option, Kind::Location, std::to_string(static_cast<int>(option->GetKey()))});
        Add(std::move(group));
    }
}
extern "C" int Soh3dsRandoSettings_GroupCount(void) { return static_cast<int>(groups.size()) + 1; }
extern "C" const char* Soh3dsRandoSettings_GroupName(int group) {
    if (group == static_cast<int>(groups.size())) return "Randomizer enhancements";
    return group >= 0 && group < static_cast<int>(groups.size()) ? groups[group].name.c_str() : "";
}
extern "C" int Soh3dsRandoSettings_Count(int group) {
    if (group == static_cast<int>(groups.size())) return kEnhancementCount;
    return group >= 0 && group < static_cast<int>(groups.size()) ? static_cast<int>(groups[group].entries.size()) : 0;
}
extern "C" void Soh3dsRandoSettings_Read(int group, int row, Soh3dsRandoMenuRow* out) {
    if (!out) return;
    *out = {};
    if (IsEnhancement(group, row)) {
        const auto& e = enhancements[row];
        std::snprintf(out->label, sizeof(out->label), "%s", e.name);
        if (row == 8) std::snprintf(out->value, sizeof(out->value), "%.2f", CVarGetFloat(e.cvar, 10.0f));
        else {
            const char* animations[] = {"Disabled", "Junk Items", "All Items"};
            const int value = CVarGetInteger(e.cvar, e.defaultValue);
            std::snprintf(out->value, sizeof(out->value), "%s", row == 7 ? animations[std::clamp(value, 0, 2)] : value ? "On" : "Off");
        }
        out->disabled = Randomizer_IsGenerating() || EnhancementDisabled(row);
        return;
    }
    const auto* entry = Find(group, row);
    if (!entry) return;
    const auto& option = *entry->option;
    std::snprintf(out->label, sizeof(out->label), "%s", option.GetName().c_str());
    std::string value;
    if (entry->kind == Kind::Setting) value = option.GetOptionText(option.GetOptionIndex());
    else if (entry->kind == Kind::Trick) value = Tokens(entry->kind).count(entry->token) ? "On" : "Off";
    else value = Tokens(entry->kind).count(entry->token) ? "Excluded" : "Included";
    std::snprintf(out->value, sizeof(out->value), "%s", value.c_str());
    out->disabled = Randomizer_IsGenerating() || (entry->kind == Kind::Setting &&
                                                (option.IsDisabled() || option.IsHidden()));
}
extern "C" const char* Soh3dsRandoSettings_Description(int group, int row) {
    if (IsEnhancement(group, row)) return enhancements[row].description;
    const auto* entry = Find(group, row);
    if (!entry) return "";
    const auto& option = *entry->option;
    if (entry->kind == Kind::Setting && option.IsDisabled()) return option.GetDisabledText().c_str();
    if (entry->kind == Kind::Setting && option.IsHidden()) return "Unavailable with the current settings.";
    return option.GetDescription().c_str();
}
extern "C" int Soh3dsRandoSettings_Adjust(int group, int row, int direction) {
    if (!direction || Randomizer_IsGenerating()) return 0;
    if (IsEnhancement(group, row)) {
        if (EnhancementDisabled(row)) return 0;
        const auto& e = enhancements[row];
        if (row == 8) CVarSetFloat(e.cvar, std::clamp(CVarGetFloat(e.cvar, 10.0f) + (direction > 0 ? 0.1f : -0.1f), 5.0f, 15.0f));
        else CVarSetInteger(e.cvar, std::clamp(static_cast<int>(CVarGetInteger(e.cvar, e.defaultValue)) + (direction > 0 ? 1 : -1), 0, row == 7 ? 2 : 1));
        ShipInit::Init(e.cvar);
        CVarSave();
        return 1;
    }
    const auto* entry = Find(group, row);
    if (!entry || !direction || Randomizer_IsGenerating()) return 0;
    const auto& option = *entry->option;
    if (entry->kind == Kind::Setting) {
        if (option.IsDisabled() || option.IsHidden()) return 0;
        int maximum = static_cast<int>(option.GetOptionCount()) - 1;
        // Desktop's slider pre-function applies this additional logic bound.
        if (option.GetKey() == RSK_SHOPSANITY_COUNT &&
            CVarGetInteger(CVAR_RANDOMIZER_SETTING("LogicRules"), RO_LOGIC_GLITCHLESS) != RO_LOGIC_NO_LOGIC)
            maximum = std::min(maximum, 7);
        const int value = std::clamp(static_cast<int>(option.GetOptionIndex()) + (direction > 0 ? 1 : -1), 0, maximum);
        CVarSetInteger(option.GetCVarName().c_str(), value);
    } else {
        auto values = Tokens(entry->kind);
        if (!values.erase(entry->token)) values.insert(entry->token);
        std::string text;
        for (const auto& token : values) { if (!text.empty()) text += ','; text += token; }
        CVarSetString(ListCVar(entry->kind), text.c_str());
    }
    Rando::Settings::GetInstance()->UpdateAllOptions();
    CVarSave();
    return 1;
}
#endif
