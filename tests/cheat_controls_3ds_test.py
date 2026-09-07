#!/usr/bin/env python3
"""Run actual MoonJump/NoClip registrations and native controls on host.

Catches a missing/wrong CVar registration, player filtering mistakes, and a
physical-to-logical input mismatch. ShipInit, conditional registration macros,
cheat translation units and ControlsBridge are production code. Only the game
state, CVar storage and GameInteractor dispatch boundary are host fixtures.
Final 3DS archive inclusion is checked separately by the link regression.
"""

import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOH = ROOT / "third_party/shipwright/soh"
CHEATS = SOH / "soh/Enhancements/Cheats"
BRIDGE = SOH / "soh/Enhancements/dualscreen3ds"
ULTRA = ROOT / "third_party/libultraship/include"

game_interactor = (SOH / "soh/Enhancements/game-interactor/GameInteractor.h").read_text()
conditional_macros = game_interactor[
    game_interactor.index("#define REGISTER_VB_SHOULD("):
    game_interactor.index("class GameInteractor {")
]

dispatch = r'''
#pragma once
#include <cstdarg>
#include <cstdint>
#include <functional>
#include <map>
#include <type_traits>
#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/vanilla-behavior/GIVanillaBehavior.h"
using HOOK_ID = uint32_t;
class GameInteractor {
public:
    struct OnPlayerUpdate {};
    struct OnVanillaBehavior {};
    static GameInteractor* Instance;
    std::map<HOOK_ID, std::function<void()>> playerHooks;
    using CollisionHook = std::function<void(GIVanillaBehavior, bool*, va_list)>;
    std::map<HOOK_ID, std::pair<GIVanillaBehavior, CollisionHook>> collisionHooks;
    HOOK_ID nextId = 1;
    template<class Hook> HOOK_ID RegisterGameHook(std::function<void()> function) {
        static_assert(std::is_same_v<Hook, OnPlayerUpdate>);
        HOOK_ID id = nextId++;
        playerHooks.emplace(id, function);
        return id;
    }
    template<class Hook> void UnregisterGameHook(HOOK_ID id) {
        static_assert(std::is_same_v<Hook, OnPlayerUpdate>);
        playerHooks.erase(id);
    }
    template<class Hook> HOOK_ID RegisterGameHookForID(GIVanillaBehavior flag, CollisionHook function) {
        static_assert(std::is_same_v<Hook, OnVanillaBehavior>);
        HOOK_ID id = nextId++;
        collisionHooks.emplace(id, std::make_pair(flag, function));
        return id;
    }
    template<class Hook> void UnregisterGameHookForID(HOOK_ID id) {
        static_assert(std::is_same_v<Hook, OnVanillaBehavior>);
        collisionHooks.erase(id);
    }
};
''' + conditional_macros

actor_header = r'''
#pragma once
#include <stdint.h>
typedef struct { float x, y, z; } Vec3f;
typedef struct { int16_t id; Vec3f velocity; float gravity; } Actor;
#define ACTOR_PLAYER 0
'''

game_header = r'''
#pragma once
#include "z64actor.h"
#include <libultraship/libultra/controller.h>
typedef struct { Actor actor; } Player;
typedef struct { struct { struct { uint16_t button; } cur; } input[4]; } GameState;
typedef struct { GameState state; Player* player; } PlayState;
#define GET_PLAYER(play) ((play)->player)
'''

fixture = r'''
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "ControlsBridge3DS.h"
extern "C" {
#include "z64.h"
PlayState* gPlayState;
}
std::map<std::string, int32_t> ints;
std::map<std::string, float> floats;
extern "C" int32_t CVarGetInteger(const char* name, int32_t fallback) {
    auto it = ints.find(name);
    return it == ints.end() ? fallback : it->second;
}
extern "C" float CVarGetFloat(const char* name, float fallback) {
    auto it = floats.find(name);
    return it == floats.end() ? fallback : it->second;
}
extern "C" void CVarSetInteger(const char* name, int32_t value) { ints[name] = value; }
extern "C" void CVarSetFloat(const char* name, float value) { floats[name] = value; }
extern "C" void CVarSave() {}
extern "C" unsigned Soh3dsControls_CurrentMappingMask(int) { return 0; }
extern "C" int Soh3dsControls_GetCStickDeadzone() { return 20; }
extern "C" void Soh3dsControls_SetCStickDeadzone(int) {}
GameInteractor gameInteractor;
GameInteractor* GameInteractor::Instance = &gameInteractor;

static void SetCheat(const char* name, int value) {
    CVarSetInteger(name, value);
    ShipInit::Init(name);
}
static void UpdatePlayer(uint32_t physical) {
    uint16_t buttons = 0;
    Soh3dsControls_TransformPad(physical, &buttons);
    gPlayState->state.input[0].cur.button = buttons;
    for (const auto& [id, hook] : gameInteractor.playerHooks) hook();
}
static bool ShouldCollide(GIVanillaBehavior flag, ...) {
    bool should = true;
    va_list args;
    va_start(args, flag);
    for (const auto& [id, registered] : gameInteractor.collisionHooks) {
        if (registered.first == flag) registered.second(flag, &should, args);
    }
    va_end(args);
    return should;
}
static int Row(const char* name) {
    Soh3dsSettingsRow row{};
    for (int i = 0; i < Soh3dsControls_RowCount(); ++i) {
        assert(Soh3dsControls_Row(i, &row));
        if (std::strcmp(name, row.label) == 0) return i;
    }
    assert(false && "missing controls row");
    return -1;
}

int main() {
    Player player{{ACTOR_PLAYER, {1.0f, -2.0f, 3.0f}, -1.0f}};
    PlayState play{};
    play.player = &player;
    gPlayState = &play;
    ShipInit::InitAll();
    Soh3dsControls_Adjust(Row("Layout"), +1); // OoT3D
    UpdatePlayer(0); // release the mapping-change barrier

    UpdatePlayer(SOH3DS_PHYS_ZL);
    assert(player.actor.velocity.y == -2.0f); // disabled by default
    SetCheat(CVAR_CHEAT("MoonJumpOnL"), 1);
    UpdatePlayer(SOH3DS_PHYS_L);
    assert(player.actor.velocity.y == -2.0f); // physical L targets
    UpdatePlayer(SOH3DS_PHYS_ZL);
    assert(player.actor.velocity.y > 0.0f); // physical ZL lifts Link
    assert(player.actor.velocity.x == 1.0f && player.actor.velocity.z == 3.0f);
    assert(player.actor.gravity == -1.0f);
    player.actor.velocity.y = -2.0f;
    UpdatePlayer(0);
    assert(player.actor.velocity.y == -2.0f); // release permits normal falling
    SetCheat(CVAR_CHEAT("MoonJumpOnL"), 0);
    UpdatePlayer(SOH3DS_PHYS_ZL);
    assert(player.actor.velocity.y == -2.0f); // disabling unregisters hook

    // Remap physical L from Z-target to R-shield, then N64 L using the native
    // row adjustment. MoonJump must honor the resulting custom layout.
    Soh3dsControls_Adjust(Row("L button"), +1);
    Soh3dsControls_Adjust(Row("L button"), +1);
    UpdatePlayer(0);
    SetCheat(CVAR_CHEAT("MoonJumpOnL"), 1);
    UpdatePlayer(SOH3DS_PHYS_L);
    assert(player.actor.velocity.y > 0.0f);
    play.player = nullptr;
    UpdatePlayer(SOH3DS_PHYS_L); // absent player is harmless
    play.player = &player;

    Actor other{1, {}, 0};
    assert(ShouldCollide(VB_PERFORM_WALL_COLLISION_CHECK, &player.actor));
    SetCheat(CVAR_CHEAT("NoClip"), 1);
    assert(!ShouldCollide(VB_PERFORM_WALL_COLLISION_CHECK, &player.actor));
    assert(ShouldCollide(VB_PERFORM_WALL_COLLISION_CHECK, &other));
    assert(ShouldCollide(VB_PERFORM_WALL_COLLISION_CHECK, static_cast<Actor*>(nullptr)));
    assert(player.actor.gravity == -1.0f);
    SetCheat(CVAR_CHEAT("NoClip"), 0);
    assert(ShouldCollide(VB_PERFORM_WALL_COLLISION_CHECK, &player.actor));
    std::cout << "3DS cheat registration, input and collision contracts passed\n";
}
'''

with tempfile.TemporaryDirectory(prefix="soh-cheat-controls-") as directory:
    directory = Path(directory)
    stub = directory / "soh/Enhancements/game-interactor/GameInteractor.h"
    stub.parent.mkdir(parents=True)
    stub.write_text(dispatch)
    (directory / "z64actor.h").write_text(actor_header)
    (directory / "z64.h").write_text(game_header)
    (directory / "macros.h").write_text(
        "#pragma once\n#define CHECK_BTN_ANY(value, mask) (((value) & (mask)) != 0)\n")
    source = directory / "test.cpp"
    source.write_text(fixture)
    binary = directory / "test"
    subprocess.run([
        os.environ.get("CXX", "g++"), "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-DSOH3DS_CONTROLS_TEST", '-DCVAR_PREFIX_CHEAT="gCheats"',
        "-I", str(directory), "-I", str(SOH), "-I", str(ULTRA), "-I", str(BRIDGE),
        str(source), str(CHEATS / "MoonJump.cpp"), str(CHEATS / "NoClip.cpp"),
        str(BRIDGE / "ControlsBridge3DS.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
