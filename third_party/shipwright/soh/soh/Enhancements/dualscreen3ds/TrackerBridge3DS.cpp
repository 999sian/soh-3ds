// SoH-3DS: compact, read-only tracker rows for the native bottom screen.
// Inventory rows use only the loaded SaveContext. Region rows consume the
// check tracker's visibility and completion facts and never request a placed
// item, hint, or item name from the randomizer context.
#if defined(__3DS__) || defined(SOH3DS_TRACKER_TEST)

#include "TrackerBridge3DS.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#ifdef SOH3DS_TRACKER_TEST
#include "TrackerBridge3DSTestDeps.h"
#else
#include "variables.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/randomizer/randomizer_check_objects.h"
#include "soh/Enhancements/randomizer/randomizer_check_tracker.h"
#include "soh/OTRGlobals.h"
#include "soh/ShipInit.hpp"

namespace Soh3dsTrackerPlatform {

bool SaveLoaded() {
    return GameInteractor::IsSaveLoaded();
}

bool Generating() {
    return IsRandoGenerating();
}

int SaveIdentity() {
    return (gSaveContext.fileNum & 0xFF) | (static_cast<int>(gSaveContext.ship.quest.id) << 8);
}

uint64_t NowMilliseconds() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

uint8_t InventoryItem(int slot) {
    return gSaveContext.inventory.items[slot];
}

uint32_t QuestItems() {
    return gSaveContext.inventory.questItems;
}

int SkullTokens() {
    return gSaveContext.inventory.gsTokens;
}

uint8_t DungeonItems(int dungeon) {
    return gSaveContext.inventory.dungeonItems[dungeon];
}

int DungeonKeys(int dungeon) {
    return gSaveContext.inventory.dungeonKeys[dungeon];
}

static const std::map<RandomizerCheckArea, std::vector<RandomizerCheck>>& ChecksByArea() {
    // RandomizerCheckObjects memoizes its table internally but returns it by
    // value. Keep that immutable copy here rather than copying it every draw.
    static const auto checks = RandomizerCheckObjects::GetAllRCObjectsByArea();
    return checks;
}

int RegionCount() {
    return RCAREA_INVALID;
}

const char* RegionName(int area) {
    static std::string name;
    name = RandomizerCheckObjects::GetRCAreaName(static_cast<RandomizerCheckArea>(area));
    return name.c_str();
}

bool RegionVisible(int area) {
    return CheckTracker::IsAreaSpoiled(static_cast<RandomizerCheckArea>(area));
}

int RegionCheckCount(int area) {
    const auto& checks = ChecksByArea();
    const auto found = checks.find(static_cast<RandomizerCheckArea>(area));
    return found == checks.end() ? 0 : static_cast<int>(found->second.size());
}

int RegionCheckId(int area, int check) {
    const auto& checks = ChecksByArea();
    const auto found = checks.find(static_cast<RandomizerCheckArea>(area));
    if (found == checks.end() || check < 0 || check >= static_cast<int>(found->second.size())) {
        return RC_UNKNOWN_CHECK;
    }
    return found->second[check];
}

bool IsRandomizer() {
    return IS_RANDO;
}

bool CheckIsShop(int check) {
    return Rando::StaticData::GetLocation(static_cast<RandomizerCheck>(check))->GetRCType() == RCTYPE_SHOP;
}

bool CheckExcluded(int check) {
    return OTRGlobals::Instance->gRandoContext->GetItemLocation(check)->IsExcluded();
}

bool CheckQuestActive(int check) {
    return OTRGlobals::Instance->gRandoContext->IsQuestOfLocationActive(static_cast<RandomizerCheck>(check));
}

bool CheckKnown(int check) {
    return OTRGlobals::Instance->gRandoContext->GetItemLocation(check)->GetCheckStatus() != RCSHOW_UNCHECKED;
}

bool CheckHasCustomPrice(int check) {
    return OTRGlobals::Instance->gRandoContext->GetItemLocation(check)->HasCustomPrice();
}

bool NonShopTrackerVisible(int check) {
    const RandomizerCheck rc = static_cast<RandomizerCheck>(check);
    // IsVisibleInCheckTracker identifies a shop through its placed item and
    // price. Keep a second guard here so even a caller error cannot enter that
    // path for a shop.
    if (Rando::StaticData::GetLocation(rc)->GetRCType() == RCTYPE_SHOP) {
        return false;
    }
    return CheckTracker::IsVisibleInCheckTracker(rc);
}

bool VanillaCheckVisible(int check) {
    const auto* location = Rando::StaticData::GetLocation(static_cast<RandomizerCheck>(check));
    return location->IsVanillaCompletion() &&
           (!location->IsDungeon() || location->GetQuest() == gSaveContext.ship.quest.id);
}

bool CheckObtained(int check) {
    return OTRGlobals::Instance->gRandoContext->GetItemLocation(check)->HasObtained();
}

bool CheckSkipped(int check) {
    return OTRGlobals::Instance->gRandoContext->GetItemLocation(check)->GetIsSkipped();
}

} // namespace Soh3dsTrackerPlatform
#endif

namespace Soh3dsTrackerDeps {

using Soh3dsTrackerPlatform::DungeonItems;
using Soh3dsTrackerPlatform::DungeonKeys;
using Soh3dsTrackerPlatform::Generating;
using Soh3dsTrackerPlatform::InventoryItem;
using Soh3dsTrackerPlatform::NowMilliseconds;
using Soh3dsTrackerPlatform::QuestItems;
using Soh3dsTrackerPlatform::RegionCheckCount;
using Soh3dsTrackerPlatform::RegionCount;
using Soh3dsTrackerPlatform::RegionName;
using Soh3dsTrackerPlatform::RegionVisible;
using Soh3dsTrackerPlatform::SaveIdentity;
using Soh3dsTrackerPlatform::SaveLoaded;
using Soh3dsTrackerPlatform::SkullTokens;

struct CheckFacts {
    bool visible;
    bool obtained;
    bool skipped;
};

CheckFacts RegionCheck(int area, int index) {
    const int check = Soh3dsTrackerPlatform::RegionCheckId(area, index);
    bool visible;
    if (!Soh3dsTrackerPlatform::IsRandomizer()) {
        visible = Soh3dsTrackerPlatform::VanillaCheckVisible(check);
    } else if (Soh3dsTrackerPlatform::CheckExcluded(check) ||
               !Soh3dsTrackerPlatform::CheckQuestActive(check)) {
        visible = false;
    } else if (Soh3dsTrackerPlatform::CheckIsShop(check)) {
        // A custom price marks a replaced shop slot without reading the price
        // or item. Wait until the slot is seen/collected so random shopsanity
        // counts cannot disclose seed state before the player visits the shop.
        visible = Soh3dsTrackerPlatform::CheckKnown(check) &&
                  Soh3dsTrackerPlatform::CheckHasCustomPrice(check);
    } else {
        visible = Soh3dsTrackerPlatform::NonShopTrackerVisible(check);
    }
    return { visible, Soh3dsTrackerPlatform::CheckObtained(check), Soh3dsTrackerPlatform::CheckSkipped(check) };
}

} // namespace Soh3dsTrackerDeps

namespace {

constexpr int kPageCount = 3;
constexpr uint64_t kRegionRefreshMs = 750;
constexpr int kMaxRegions = 64;
constexpr int kMaxChecks = 8192;

struct RowStorage {
    std::string label;
    std::string value;
    std::string tooltip;
};

struct RegionStorage {
    std::string name;
    int completed;
    int total;
};

std::vector<RowStorage> sRows;
int sRowsPage = -1;
bool sRowsValid = false;
std::vector<RegionStorage> sRegions;
uint64_t sRegionRefreshAt = 0;
int sRegionSaveIdentity = -1;
bool sHaveRegionSnapshot = false;

void AddRow(std::string label, std::string value = {}, std::string tooltip = {}) {
    sRows.push_back({ std::move(label), std::move(value), std::move(tooltip) });
}

int CountBits(uint32_t bits, int first, int count) {
    int result = 0;
    for (int bit = first; bit < first + count; ++bit) {
        result += (bits >> bit) & 1U;
    }
    return result;
}

const char* InventoryItemName(uint8_t item) {
    static constexpr std::array<const char*, 20> names = {
        "Deku Stick", "Deku Nuts", "Bombs",       "Bow",          "Fire Arrow",
        "Din's Fire", "Slingshot", "Fairy Ocarina", "Ocarina of Time", "Bombchus",
        "Hookshot",   "Longshot",  "Ice Arrow",   "Farore's Wind", "Boomerang",
        "Lens of Truth", "Magic Beans", "Megaton Hammer", "Light Arrow", "Nayru's Love",
    };
    if (item < names.size()) {
        return names[item];
    }
    switch (item) {
        case 0x21: return "Weird Egg";
        case 0x22: return "Chicken";
        case 0x23: return "Zelda's Letter";
        case 0x24: return "Keaton Mask";
        case 0x25: return "Skull Mask";
        case 0x26: return "Spooky Mask";
        case 0x27: return "Bunny Hood";
        case 0x28: return "Goron Mask";
        case 0x29: return "Zora Mask";
        case 0x2A: return "Gerudo Mask";
        case 0x2B: return "Mask of Truth";
        case 0x2D: return "Pocket Egg";
        case 0x2E: return "Pocket Cucco";
        case 0x2F: return "Cojiro";
        case 0x30: return "Odd Mushroom";
        case 0x31: return "Odd Potion";
        case 0x32: return "Poacher's Saw";
        case 0x33: return "Broken Goron's Sword";
        case 0x34: return "Prescription";
        case 0x35: return "Eyeball Frog";
        case 0x36: return "Eye Drops";
        case 0x37: return "Claim Check";
        default: return "Inventory item";
    }
}

void BuildItems() {
    // Slots 0..17 are distinct tools/spells. Bottle contents are deliberately
    // summarized as capacity, and the two trade slots show only current owned
    // progression from the save.
    for (int slot = 0; slot < 18; ++slot) {
        const uint8_t item = Soh3dsTrackerDeps::InventoryItem(slot);
        if (item != 0xFF) {
            AddRow(InventoryItemName(item), "Owned", "Read from the loaded save inventory.");
        }
    }
    int bottles = 0;
    for (int slot = 18; slot <= 21; ++slot) {
        bottles += Soh3dsTrackerDeps::InventoryItem(slot) != 0xFF;
    }
    if (bottles != 0) {
        AddRow("Bottles", std::to_string(bottles), "Owned bottle slots; contents are not tracked here.");
    }
    for (int slot = 22; slot <= 23; ++slot) {
        const uint8_t item = Soh3dsTrackerDeps::InventoryItem(slot);
        if (item != 0xFF) {
            AddRow(InventoryItemName(item), "Owned", "Current trade quest item in the loaded save.");
        }
    }

    const uint32_t quest = Soh3dsTrackerDeps::QuestItems();
    AddRow("Spiritual Stones", std::to_string(CountBits(quest, 0x12, 3)) + "/3");
    AddRow("Medallions", std::to_string(CountBits(quest, 0, 6)) + "/6");
    AddRow("Songs", std::to_string(CountBits(quest, 6, 12)) + "/12");
    AddRow("Gold Skulltulas", std::to_string(std::max(0, Soh3dsTrackerDeps::SkullTokens())));
}

void BuildDungeons() {
    struct Dungeon {
        const char* name;
        int index;
        int bossKeyIndex;
    };
    static constexpr Dungeon dungeons[] = {
        { "Deku Tree", 0, 0 },          { "Dodongo's Cavern", 1, 1 }, { "Jabu Jabu's Belly", 2, 2 },
        { "Forest Temple", 3, 3 },      { "Fire Temple", 4, 4 },      { "Water Temple", 5, 5 },
        { "Spirit Temple", 6, 6 },      { "Shadow Temple", 7, 7 },    { "Bottom of the Well", 8, 8 },
        { "Ice Cavern", 9, 9 },         { "Gerudo Training", 11, 11 }, { "Ganon's Castle", 13, 10 },
    };
    for (const auto& dungeon : dungeons) {
        const int keys = Soh3dsTrackerDeps::DungeonKeys(dungeon.index);
        const uint8_t items = Soh3dsTrackerDeps::DungeonItems(dungeon.index);
        const uint8_t bossKeyItems = Soh3dsTrackerDeps::DungeonItems(dungeon.bossKeyIndex);
        const std::string keyText = keys < 0 ? "-" : std::to_string(keys);
        char value[64];
        std::snprintf(value, sizeof(value), "K:%s B:%s M:%s C:%s", keyText.c_str(),
                      (bossKeyItems & 1U) ? "Y" : "-",
                      (items & 4U) ? "Y" : "-", (items & 2U) ? "Y" : "-");
        AddRow(dungeon.name, value, "Current small keys and owned dungeon items.");
    }
}

void ClearRegionSnapshot() {
    sRegions.clear();
    sRegionRefreshAt = 0;
    sRegionSaveIdentity = -1;
    sHaveRegionSnapshot = false;
}

void InvalidateForLoad() {
    ClearRegionSnapshot();
    sRowsValid = false;
}

void RefreshRegionsIfNeeded() {
    const uint64_t now = Soh3dsTrackerDeps::NowMilliseconds();
    const int identity = Soh3dsTrackerDeps::SaveIdentity();
    const bool expired = !sHaveRegionSnapshot || now >= sRegionRefreshAt;
    if (sHaveRegionSnapshot && identity == sRegionSaveIdentity && !expired) {
        return;
    }
    // Generation mutates the randomizer location table on a worker thread.
    // Keep the last complete snapshot and never enter it while that happens.
    if (Soh3dsTrackerDeps::Generating()) {
        return;
    }

    std::vector<RegionStorage> next;
    const int regionCount = std::clamp(Soh3dsTrackerDeps::RegionCount(), 0, kMaxRegions);
    int visitedChecks = 0;
    for (int area = 0; area < regionCount && visitedChecks < kMaxChecks; ++area) {
        // The main tracker withholds random-layout dungeons until their area is
        // spoiled. Even a bare total can reveal Vanilla vs MQ, so apply that
        // gate before asking for any check facts.
        if (!Soh3dsTrackerDeps::RegionVisible(area)) {
            continue;
        }
        const int available = std::max(0, Soh3dsTrackerDeps::RegionCheckCount(area));
        const int count = std::min(available, kMaxChecks - visitedChecks);
        int completed = 0;
        int total = 0;
        for (int check = 0; check < count; ++check) {
            const auto facts = Soh3dsTrackerDeps::RegionCheck(area, check);
            if (!facts.visible) {
                continue;
            }
            ++total;
            completed += facts.obtained || facts.skipped;
        }
        visitedChecks += count;
        if (total != 0) {
            const char* name = Soh3dsTrackerDeps::RegionName(area);
            next.push_back({ name == nullptr ? "Unknown region" : name, completed, total });
        }
    }
    sRegions = std::move(next);
    sRegionSaveIdentity = identity;
    sRegionRefreshAt = now + kRegionRefreshMs;
    sHaveRegionSnapshot = true;
}

void BuildRegions() {
    RefreshRegionsIfNeeded();
    if (!sHaveRegionSnapshot) {
        AddRow(Soh3dsTrackerDeps::Generating() ? "Tracker paused during seed generation" : "Tracker data unavailable");
        return;
    }
    if (sRegions.empty()) {
        AddRow("No tracked checks");
        return;
    }
    for (const auto& region : sRegions) {
        AddRow(region.name, std::to_string(region.completed) + "/" + std::to_string(region.total),
               "Completed checks / visible checks. Skipped checks follow the main check tracker's completed policy.");
    }
}

void RebuildRows(int page) {
    sRows.clear();
    sRowsPage = page;
    sRowsValid = true;
    if (page < 0 || page >= kPageCount) {
        return;
    }
    if (!Soh3dsTrackerDeps::SaveLoaded()) {
        ClearRegionSnapshot();
        AddRow("No save loaded");
        return;
    }
    switch (page) {
        case 0: BuildItems(); break;
        case 1: BuildDungeons(); break;
        case 2: BuildRegions(); break;
    }
}

void EnsureRows(int page) {
    if (!sRowsValid || sRowsPage != page) {
        RebuildRows(page);
    }
}

} // namespace

extern "C" int Soh3dsTracker_PageCount(void) {
    return kPageCount;
}

extern "C" const char* Soh3dsTracker_PageName(int page) {
    static constexpr const char* names[kPageCount] = { "Items", "Dungeons", "Regions" };
    return page >= 0 && page < kPageCount ? names[page] : "";
}

extern "C" int Soh3dsTracker_RowCount(int page) {
    RebuildRows(page);
    return static_cast<int>(sRows.size());
}

extern "C" int Soh3dsTracker_Row(int page, int row, Soh3dsSettingsRow* out) {
    if (out == nullptr || row < 0) {
        return 0;
    }
    EnsureRows(page);
    if (row >= static_cast<int>(sRows.size())) {
        return 0;
    }
    const auto& source = sRows[row];
    out->label = source.label.c_str();
    out->value = source.value.c_str();
    out->tooltip = source.tooltip.c_str();
    out->kind = SOH3DS_ROW_TEXT;
    out->on = 0;
    out->disabled = 1;
    return 1;
}

#ifdef SOH3DS_TRACKER_TEST
extern "C" void Soh3dsTracker_TestNotifyLoad() {
    InvalidateForLoad();
}
#else
void RegisterSoh3dsTrackerLoadHook() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>(
        [](int32_t) { InvalidateForLoad(); });
}

static RegisterShipInitFunc registerSoh3dsTrackerLoadHook(RegisterSoh3dsTrackerLoadHook);
#endif

#endif
