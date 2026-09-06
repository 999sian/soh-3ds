#!/usr/bin/env python3
"""Exercise the production 3DS tracker bridge with controlled game state."""

import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "third_party/shipwright/soh/soh/Enhancements/dualscreen3ds"

DEPS = r'''
#pragma once
#include <cstdint>
namespace Soh3dsTrackerPlatform {
bool SaveLoaded();
bool Generating();
int SaveIdentity();
uint64_t NowMilliseconds();
uint8_t InventoryItem(int slot);
uint32_t QuestItems();
int SkullTokens();
uint8_t DungeonItems(int dungeon);
int DungeonKeys(int dungeon);
int RegionCount();
const char* RegionName(int area);
bool RegionVisible(int area);
int RegionCheckCount(int area);
int RegionCheckId(int area, int check);
bool IsRandomizer();
bool CheckIsShop(int check);
bool CheckExcluded(int check);
bool CheckQuestActive(int check);
bool CheckKnown(int check);
bool CheckHasCustomPrice(int check);
bool NonShopTrackerVisible(int check);
bool VanillaCheckVisible(int check);
bool CheckObtained(int check);
bool CheckSkipped(int check);
}
'''

HARNESS = r'''
#include "TrackerBridge3DS.h"
#include "TrackerBridge3DSTestDeps.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
struct FakeCheck {
    bool trackerVisible;
    bool obtained;
    bool skipped;
    const char* placedItem;
    bool shop = false;
    bool excluded = false;
    bool questActive = true;
    bool known = true;
    bool customPrice = true;
    bool vanillaVisible = true;
};
struct FakeRegion { const char* name; bool visible; std::vector<FakeCheck> checks; };
bool saveLoaded = true;
bool generating = false;
int saveIdentity = 10;
uint64_t nowMs = 1000;
std::array<uint8_t, 24> items{};
uint32_t questItems = 0;
int skullTokens = 0;
std::array<uint8_t, 20> dungeonItems{};
std::array<int, 19> dungeonKeys{};
std::vector<FakeRegion> regions;
int checkReads = 0;
int placementReads = 0;
int inventoryReads = 0;
bool isRandomizer = true;

const FakeCheck& Check(int id) {
    return regions.at((id >> 16) & 0xFFFF).checks.at(id & 0xFFFF);
}
}

namespace Soh3dsTrackerPlatform {
bool SaveLoaded() { return saveLoaded; }
bool Generating() { return generating; }
int SaveIdentity() { return saveIdentity; }
uint64_t NowMilliseconds() { return nowMs; }
uint8_t InventoryItem(int slot) { ++inventoryReads; return items.at(slot); }
uint32_t QuestItems() { return questItems; }
int SkullTokens() { return skullTokens; }
uint8_t DungeonItems(int dungeon) { return dungeonItems.at(dungeon); }
int DungeonKeys(int dungeon) { return dungeonKeys.at(dungeon); }
int RegionCount() { return static_cast<int>(regions.size()); }
const char* RegionName(int area) { return regions.at(area).name; }
bool RegionVisible(int area) { return regions.at(area).visible; }
int RegionCheckCount(int area) { return static_cast<int>(regions.at(area).checks.size()); }
int RegionCheckId(int area, int check) {
    ++checkReads;
    return (area << 16) | check;
}
bool IsRandomizer() { return isRandomizer; }
bool CheckIsShop(int check) { return Check(check).shop; }
bool CheckExcluded(int check) { return Check(check).excluded; }
bool CheckQuestActive(int check) { return Check(check).questActive; }
bool CheckKnown(int check) { return Check(check).known; }
bool CheckHasCustomPrice(int check) { return Check(check).customPrice; }
bool NonShopTrackerVisible(int check) {
    const auto& value = Check(check);
    if (value.shop) {
        // Models the old generic tracker predicate's IdentifyShopItem path.
        // Calling it for a shop is itself the privacy failure.
        ++placementReads;
        (void)std::strlen(value.placedItem);
    }
    return value.trackerVisible;
}
bool VanillaCheckVisible(int check) { return Check(check).vanillaVisible; }
bool CheckObtained(int check) { return Check(check).obtained; }
bool CheckSkipped(int check) { return Check(check).skipped; }
}

extern "C" void Soh3dsTracker_TestNotifyLoad();

static std::vector<std::pair<std::string, std::string>> Rows(int page) {
    std::vector<std::pair<std::string, std::string>> rows;
    const int count = Soh3dsTracker_RowCount(page);
    for (int i = 0; i < count; ++i) {
        Soh3dsSettingsRow row{};
        assert(Soh3dsTracker_Row(page, i, &row));
        assert(row.kind == SOH3DS_ROW_TEXT && row.disabled == 1);
        rows.emplace_back(row.label, row.value);
    }
    return rows;
}

static std::string Value(const std::vector<std::pair<std::string, std::string>>& rows, const char* label) {
    for (const auto& row : rows) if (row.first == label) return row.second;
    std::cerr << "missing tracker row: " << label << '\n';
    std::abort();
}

static bool HasLabel(const std::vector<std::pair<std::string, std::string>>& rows, const char* label) {
    for (const auto& row : rows) if (row.first == label) return true;
    return false;
}

int main() {
    items.fill(0xFF);
    dungeonKeys.fill(-1);
    assert(Soh3dsTracker_PageCount() == 3);
    assert(std::strcmp(Soh3dsTracker_PageName(0), "Items") == 0);
    assert(std::strcmp(Soh3dsTracker_PageName(1), "Dungeons") == 0);
    assert(std::strcmp(Soh3dsTracker_PageName(2), "Regions") == 0);
    assert(std::strcmp(Soh3dsTracker_PageName(-1), "") == 0);
    Soh3dsSettingsRow invalid{};
    assert(!Soh3dsTracker_Row(0, -1, &invalid));
    assert(!Soh3dsTracker_Row(7, 0, &invalid));
    assert(!Soh3dsTracker_Row(0, 0, nullptr));

    // Missing production filtering here would expose Bombs or the Hookshot
    // instead of reporting only the actual items owned by this save.
    items[2] = 0xFF;
    items[3] = 0x03; // Bow
    items[9] = 0x0B; // Longshot
    questItems = (1u << 0) | (1u << 3) | (1u << 0x12) | (1u << 0x14) |
                 (1u << 6) | (1u << 7) | (1u << 8);
    skullTokens = 17;
    auto owned = Rows(0);
    assert(HasLabel(owned, "Bow"));
    assert(HasLabel(owned, "Longshot"));
    assert(!HasLabel(owned, "Bombs"));
    assert(!HasLabel(owned, "Hookshot"));
    assert(Value(owned, "Spiritual Stones") == "2/3");
    assert(Value(owned, "Medallions") == "2/6");
    assert(Value(owned, "Songs") == "3/12");
    assert(Value(owned, "Gold Skulltulas") == "17");
    Soh3dsTracker_RowCount(0);
    const int readsAfterRowCount = inventoryReads;
    (void)Soh3dsTracker_PageName(0); // Header draw must preserve row buffers.
    Soh3dsSettingsRow cachedRow{};
    assert(Soh3dsTracker_Row(0, 0, &cachedRow));
    assert(inventoryReads == readsAfterRowCount); // Row is a cache lookup, not a rebuild.

    dungeonKeys[3] = 4;
    dungeonItems[3] = (1u << 0) | (1u << 2); // boss key and map, no compass
    dungeonKeys[13] = 2; // Ganon's Castle small keys
    dungeonItems[10] = (1u << 0); // Ganon's boss key belongs to Ganon's Tower
    dungeonItems[13] = (1u << 2); // Castle map; no boss key in the Castle slot
    auto dungeons = Rows(1);
    assert(dungeons.size() == 12);
    assert(Value(dungeons, "Forest Temple") == "K:4 B:Y M:Y C:-");
    assert(Value(dungeons, "Deku Tree") == "K:- B:- M:- C:-");
    assert(Value(dungeons, "Ganon's Castle") == "K:2 B:Y M:Y C:-");

    // The secret placed-item strings model seed data present in memory. The
    // bridge receives only visibility/completion facts, so neither an unknown
    // nor an uncollected check can disclose a placement.
    regions = {
        {"Kokiri Forest", true, {
            {true, true, false, "Progressive Hookshot"},
            {true, false, false, "Light Arrows"},
            {false, true, false, "Triforce"},
            {true, false, true, "Boss Key"},
            {true, false, false, "Unrevealed shop placement", true, false, true, false, true},
        }},
        {"Hyrule Field", true, {{true, false, false, "Bow"}}},
        {"Unknown MQ dungeon", false, {{true, true, false, "Dungeon layout secret"}}},
    };
    auto regionRows = Rows(2);
    assert(Value(regionRows, "Kokiri Forest") == "2/3");
    assert(Value(regionRows, "Hyrule Field") == "0/1");
    assert(!HasLabel(regionRows, "Unknown MQ dungeon"));
    assert(placementReads == 0);
    for (const auto& row : regionRows) {
        for (const auto& region : regions) for (const auto& check : region.checks) {
            assert(row.first.find(check.placedItem) == std::string::npos);
            assert(row.second.find(check.placedItem) == std::string::npos);
        }
    }
    const int cachedReads = checkReads;
    regions[0].checks[1].obtained = true;
    assert(Value(Rows(2), "Kokiri Forest") == "2/3");
    assert(checkReads == cachedReads); // no traversal on every draw
    nowMs += 1000;
    assert(Value(Rows(2), "Kokiri Forest") == "3/3");
    assert(checkReads > cachedReads);

    // A newly loaded save invalidates immediately even inside the time window.
    const int priorReads = checkReads;
    ++saveIdentity;
    regions[1].checks[0].obtained = true;
    assert(Value(Rows(2), "Hyrule Field") == "1/1");
    assert(checkReads > priorReads);

    // Reloading the same slot and quest also invalidates immediately through
    // the actual load-hook target, without waiting for the timer.
    const int beforeSameSlotLoad = checkReads;
    regions[0].checks[0].obtained = false;
    Soh3dsTracker_TestNotifyLoad();
    assert(Value(Rows(2), "Kokiri Forest") == "2/3");
    assert(checkReads > beforeSameSlotLoad);

    // During seed generation the bridge retains an already safe snapshot and
    // never traverses mutable location state.
    generating = true;
    const int beforeGeneration = checkReads;
    nowMs += 1000;
    regions[0].checks.push_back({true, true, false, "Generated Secret"});
    assert(Value(Rows(2), "Kokiri Forest") == "2/3");
    assert(checkReads == beforeGeneration);
    generating = false;

    // Vanilla uses the same visible/completed policy and still provides useful
    // owned-item, dungeon and region summaries without randomizer placements.
    ++saveIdentity;
    nowMs += 1000;
    isRandomizer = false;
    regions = {{"Kakariko Village", true, {{true, true, false, "vanilla reward"},
                                            {true, false, false, "vanilla reward"}}}};
    assert(Value(Rows(2), "Kakariko Village") == "1/2");
    assert(HasLabel(Rows(0), "Bow"));

    saveLoaded = false;
    assert(Soh3dsTracker_RowCount(0) == 1);
    assert(Value(Rows(0), "No save loaded").empty());
    assert(Soh3dsTracker_RowCount(2) == 1);
    assert(Value(Rows(2), "No save loaded").empty());

    std::cout << "3DS tracker: owned-only inventory, dungeon state, spoiler-free cached region counts pass\n";
}
'''


with tempfile.TemporaryDirectory(prefix="soh-tracker-3ds-") as directory:
    directory = Path(directory)
    (directory / "TrackerBridge3DSTestDeps.h").write_text(DEPS)
    (directory / "test.cpp").write_text(HARNESS)
    binary = directory / "tracker-test"
    subprocess.run([
        os.environ.get("CXX", "g++"), "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
        "-DSOH3DS_TRACKER_TEST", "-I", str(directory), "-I", str(BRIDGE),
        str(BRIDGE / "TrackerBridge3DS.cpp"), str(directory / "test.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
