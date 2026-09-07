#!/usr/bin/env python3
"""Run the real Infinite Ammo module, ShipInit registry and COND_HOOK on host.

Only game/runtime boundaries are stubbed. This checks CVar-driven registration,
save gating, equipment capacities and vanilla/randomizer bombchu behavior.
Run with --sanitize for the ASan/UBSan variant.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOH = ROOT / "third_party/shipwright/soh"
MODULE = SOH / "soh/Enhancements/Cheats/Infinite/Ammo.cpp"
INTERACTOR = SOH / "soh/Enhancements/game-interactor/GameInteractor.h"

module = re.sub(r'^#include[^\n]*\n', '', MODULE.read_text(), flags=re.MULTILINE)
header = INTERACTOR.read_text()
conditional_hook = header[header.index('#define COND_HOOK('):header.index('#define COND_ID_HOOK(')]

HARNESS = r'''
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

#define CVAR_PREFIX_CHEAT "gCheats"
using s8 = int8_t;
enum { ITEM_STICK, ITEM_NUT, ITEM_BOMB, ITEM_BOW, ITEM_SLINGSHOT, ITEM_BOMBCHU,
       ITEM_COUNT, ITEM_NONE = 0xff };
enum { UPG_STICKS, UPG_NUTS, UPG_BOMB_BAG, UPG_QUIVER, UPG_BULLET_BAG, UPG_COUNT };
struct SaveContext {
    std::array<s8, ITEM_COUNT> ammo{};
    std::array<int, ITEM_COUNT> items{};
    std::array<int, UPG_COUNT> capacity{};
};
extern "C" { SaveContext gSaveContext; }
#define AMMO(item) gSaveContext.ammo[item]
#define INV_CONTENT(item) gSaveContext.items[item]
#define CUR_CAPACITY(upgrade) gSaveContext.capacity[upgrade]
static std::map<std::string, int> cvars;
int CVarGetInteger(const char* name, int fallback) {
    const auto found = cvars.find(name);
    return found == cvars.end() ? fallback : found->second;
}
void CVarSetInteger(const char* name, int value) { cvars[name] = value; }

static bool randomizer = false;
static bool progressiveBombchus = false;
static int optionReads = 0;
static int capacityReads = 0;
enum { RSK_BOMBCHU_BAG, RO_BOMBCHU_BAG_PROGRESSIVE };
struct Option {
    bool Is(int value) const {
        return value == RO_BOMBCHU_BAG_PROGRESSIVE && progressiveBombchus;
    }
};
Option GetOption(int key) {
    if (key != RSK_BOMBCHU_BAG) std::abort();
    ++optionReads;
    return {};
}
#define IS_RANDO randomizer
#define RAND_GET_OPTION(key) GetOption(key)
struct RandoContext {
    int bombchuCapacity = 0;
    int GetBombchuCapacity() { ++capacityReads; return bombchuCapacity; }
};
struct OTRGlobals {
    static OTRGlobals* Instance;
    RandoContext* gRandoContext;
};
static RandoContext randoContext;
static OTRGlobals globals{&randoContext};
OTRGlobals* OTRGlobals::Instance = &globals;

using HOOK_ID = uint32_t;
static bool saveLoaded = false;
static bool debugSaveLoaded = false;
static int saveChecks = 0;
struct GameInteractor {
    struct OnGameFrameUpdate {};
    static GameInteractor* Instance;
    std::map<HOOK_ID, std::function<void()>> frameHooks;
    HOOK_ID nextId = 1;
    static bool IsSaveLoaded(bool allowDebug = false) {
        ++saveChecks;
        return saveLoaded || (allowDebug && debugSaveLoaded);
    }
    template <typename Hook> HOOK_ID RegisterGameHook(std::function<void()> callback) {
        const auto id = nextId++;
        frameHooks[id] = callback;
        return id;
    }
    template <typename Hook> void UnregisterGameHook(HOOK_ID id) { frameHooks.erase(id); }
    void Frame() { for (const auto& [id, callback] : frameHooks) callback(); }
};
static GameInteractor interactor;
GameInteractor* GameInteractor::Instance = &interactor;
'''

CASES = r'''
static void Require(bool condition, const char* description) {
    if (!condition) {
        std::cerr << description << '\n';
        std::exit(1);
    }
}
static void Select(bool enabled) {
    // Use the menu's actual path rather than calling RegisterInfiniteAmmo directly.
    CVarSetInteger("gCheats.InfiniteAmmo", enabled);
    ShipInit::Init("gCheats.InfiniteAmmo");
}
static void ExpectRegularCapacity() {
    const int items[] = { ITEM_STICK, ITEM_NUT, ITEM_BOMB, ITEM_BOW, ITEM_SLINGSHOT };
    const int upgrades[] = { UPG_STICKS, UPG_NUTS, UPG_BOMB_BAG, UPG_QUIVER, UPG_BULLET_BAG };
    for (int i = 0; i < 5; ++i)
        Require(AMMO(items[i]) == CUR_CAPACITY(upgrades[i]), "ammo did not match its current equipment capacity");
}
int main(int argc, char** argv) {
    Require(argc == 2, "missing scenario");
    const std::string scenario = argv[1];
    gSaveContext.items.fill(ITEM_NONE);
    gSaveContext.capacity = {10, 20, 30, 40, 50};
    if (scenario == "registration") {
        ShipInit::InitAll();
        Require(interactor.frameHooks.empty(), "default Off registered an ammo callback");
        saveLoaded = true;
        gSaveContext.ammo.fill(1);
        interactor.Frame();
        Require(AMMO(ITEM_STICK) == 1, "Off modified ammo");
        for (int i = 0; i < 3; ++i) {
            Select(true);
            Select(true);
            Require(interactor.frameHooks.size() == 1, "enabling did not retain exactly one callback");
            const int checksBefore = saveChecks;
            interactor.Frame();
            Require(saveChecks == checksBefore + 1, "enabled callback did not run exactly once");
            ExpectRegularCapacity();
            Select(false);
            Require(interactor.frameHooks.empty(), "Off retained the ammo callback");
            gSaveContext.ammo.fill(1);
            interactor.Frame();
            for (s8 ammo : gSaveContext.ammo) Require(ammo == 1, "disabled cheat replenished ammo");
        }
    } else if (scenario == "persisted_on_and_save_gating") {
        CVarSetInteger("gCheats.InfiniteAmmo", 1);
        ShipInit::InitAll();
        Require(interactor.frameHooks.size() == 1, "persisted On was not registered at startup");
        gSaveContext.ammo.fill(3);
        randomizer = progressiveBombchus = true;
        INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
        interactor.Frame();
        for (s8 ammo : gSaveContext.ammo) Require(ammo == 3, "no-save frame changed inventory");
        Require(optionReads == 0 && capacityReads == 0, "no-save frame accessed randomizer state");
        debugSaveLoaded = true;
        interactor.Frame();
        ExpectRegularCapacity();
        Require(capacityReads == 1, "loaded debug save did not replenish ammo");
        debugSaveLoaded = false;
        gSaveContext.ammo.fill(2);
        interactor.Frame();
        for (s8 ammo : gSaveContext.ammo) Require(ammo == 2, "returning to no-save state changed inventory");
    } else if (scenario == "capacities") {
        Select(true);
        saveLoaded = true;
        // Mixed upgrade levels catch swaps; zero capacities must stay zero.
        for (const auto& capacities : {std::array<int, UPG_COUNT>{10, 30, 40, 50, 30},
                                      std::array<int, UPG_COUNT>{30, 40, 20, 30, 50},
                                      std::array<int, UPG_COUNT>{0, 0, 0, 0, 0}}) {
            gSaveContext.capacity = capacities;
            gSaveContext.ammo.fill(1);
            interactor.Frame();
            ExpectRegularCapacity();
            Require(AMMO(ITEM_BOMBCHU) == 1, "cheat granted unowned bombchus");
        }
    } else if (scenario == "bombchus") {
        Select(true);
        saveLoaded = true;
        INV_CONTENT(ITEM_BOMBCHU) = ITEM_BOMBCHU;
        progressiveBombchus = true;
        interactor.Frame();
        Require(AMMO(ITEM_BOMBCHU) == 50, "vanilla bombchus did not refill to 50");
        Require(optionReads == 0 && capacityReads == 0, "vanilla queried randomizer state");
        randomizer = true;
        progressiveBombchus = false;
        interactor.Frame();
        Require(AMMO(ITEM_BOMBCHU) == 50 && capacityReads == 0,
                "non-progressive randomizer bombchus did not use vanilla capacity");
        progressiveBombchus = true;
        for (int capacity : {0, 10, 20, 50}) {
            randoContext.bombchuCapacity = capacity;
            AMMO(ITEM_BOMBCHU) = 1;
            interactor.Frame();
            Require(AMMO(ITEM_BOMBCHU) == capacity, "progressive bombchu bag ignored current capacity");
        }
        Require(capacityReads == 4, "progressive capacity was not read on every frame");
        INV_CONTENT(ITEM_BOMBCHU) = ITEM_NONE;
        AMMO(ITEM_BOMBCHU) = 7;
        const int readsBefore = optionReads;
        interactor.Frame();
        Require(AMMO(ITEM_BOMBCHU) == 7 && optionReads == readsBefore && capacityReads == 4,
                "unowned bombchus were changed or queried randomizer options");
    } else {
        Require(false, "unknown scenario");
    }
    std::cout << scenario << " passed\n";
}
'''

with tempfile.TemporaryDirectory(prefix='soh-infinite-ammo-test-') as directory:
    directory = Path(directory)
    source = directory / 'test.cpp'
    source.write_text(HARNESS + conditional_hook + module + CASES)
    executable = directory / 'test'
    flags = ['-std=c++17', '-O1', '-Wall', '-Wextra']
    if '--sanitize' in sys.argv:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
    subprocess.run([os.environ.get('CXX', 'g++'), *flags, '-I', str(SOH), str(source),
                    '-o', str(executable)], check=True)
    failures = []
    for scenario in ['registration', 'persisted_on_and_save_gating', 'capacities', 'bombchus']:
        if subprocess.run([str(executable), scenario]).returncode:
            failures.append(scenario)
    if failures:
        sys.exit('Infinite Ammo regression failed: ' + ', '.join(failures))
