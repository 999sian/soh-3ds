#!/usr/bin/env python3
"""Exercise real mirror registration and state updates with game boundaries stubbed.

Catches a persisted derived flag surviving Off at startup/file select, as well
as scene changes or repeated settings losing the registered update callback.
The ShipInit registry, COND_HOOK macro and full mirror module body are real.
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
MODULE = SOH / "soh/Enhancements/ExtraModes/MirroredWorld.cpp"
INTERACTOR = SOH / "soh/Enhancements/game-interactor/GameInteractor.h"

module = re.sub(r'^#include[^\n]*\n', '', MODULE.read_text(), flags=re.MULTILINE)
header = INTERACTOR.read_text()
conditional_hook = header[header.index('#define COND_HOOK('):header.index('#define COND_ID_HOOK(')]

HARNESS = r'''
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/enhancementTypes.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

#define CVAR_ENHANCEMENT(name) "gEnhancements." name
#define IS_RANDO false
struct SaveContext {
    struct { struct { uint64_t fileCreatedAt = 1234; } stats; } ship;
};
struct PlayState { int32_t sceneNum; };
extern "C" {
SaveContext gSaveContext;
PlayState* gPlayState = nullptr;
}
enum {
    SCENE_DEKU_TREE = 0, SCENE_INSIDE_GANONS_CASTLE_COLLAPSE = 13,
    SCENE_THIEVES_HIDEOUT = 12, SCENE_DEKU_TREE_BOSS = 17,
    SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR = 25, SCENE_GANON_BOSS = 79,
    TEST_OVERWORLD = 81
};
static std::map<std::string, int> cvars;
int CVarGetInteger(const char* name, int fallback) {
    const auto found = cvars.find(name);
    return found == cvars.end() ? fallback : found->second;
}
void CVarSetInteger(const char* name, int value) { cvars[name] = value; }
void CVarClear(const char* name) { cvars.erase(name); }
static int patchCalls = 0;
static bool patchedMirror = false;
void ApplyMirrorWorldGfxPatches() {
    ++patchCalls;
    patchedMirror = CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0) != 0;
}
bool ResourceMgr_IsSceneMasterQuest(int32_t) { return false; }
namespace Rando {
struct Context {
    static Context* GetInstance() { static Context instance; return &instance; }
    uint64_t GetSeed() const { return 5678; }
};
}
static int randomCalls = 0;
namespace ShipUtils {
void RandInit(uint64_t seed, uint64_t* state) { *state = seed; }
int Random(int, int, uint64_t* = nullptr) { ++randomCalls; return 0; }
}
using HOOK_ID = uint32_t;
struct GameInteractor {
    struct OnSceneInit {};
    static GameInteractor* Instance;
    std::map<HOOK_ID, std::function<void(int32_t)>> sceneHooks;
    HOOK_ID nextId = 1;
    template <typename Hook>
    HOOK_ID RegisterGameHook(std::function<void(int32_t)> callback) {
        const auto id = nextId++;
        sceneHooks[id] = callback;
        return id;
    }
    template <typename Hook> void UnregisterGameHook(HOOK_ID id) { sceneHooks.erase(id); }
    void SceneInit(int32_t scene) {
        for (const auto& [id, callback] : sceneHooks) callback(scene);
    }
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
static void Select(MirroredWorldMode mode) {
    CVarSetInteger(CVAR_ENHANCEMENT("MirroredWorldMode"), mode);
    ShipInit::Init(CVAR_ENHANCEMENT("MirroredWorldMode"));
}
static void ExpectMirror(bool expected, const char* description) {
    Require((CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0) != 0) == expected, description);
    Require(patchedMirror == expected, "graphics patches disagree with derived mirror state");
}
int main(int argc, char** argv) {
    Require(argc == 2, "missing scenario");
    PlayState play{SCENE_DEKU_TREE};
    const std::string scenario = argv[1];
    if (scenario == "startup_off") {
        // Configuration can contain the previous session's derived flag.
        CVarSetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 1);
        CVarSetInteger(CVAR_ENHANCEMENT("MirroredWorldMode"), MIRRORED_WORLD_OFF);
        ShipInit::InitAll();
        ExpectMirror(false, "Off startup retained a persisted mirror flag");
        interactor.SceneInit(SCENE_DEKU_TREE);
        ExpectMirror(false, "Off startup registered an active mirror callback");
    } else if (scenario == "startup_dungeon") {
        CVarSetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 1);
        CVarSetInteger(CVAR_ENHANCEMENT("MirroredWorldMode"), MIRRORED_WORLD_DUNGEONS_ALL);
        ShipInit::InitAll();
        gPlayState = &play;
        play.sceneNum = TEST_OVERWORLD;
        interactor.SceneInit(play.sceneNum);
        ExpectMirror(false, "first non-dungeon scene retained a persisted mirror flag");
        play.sceneNum = SCENE_DEKU_TREE;
        interactor.SceneInit(play.sceneNum);
        ExpectMirror(true, "dungeon scene did not enable mirror mode");
        play.sceneNum = TEST_OVERWORLD;
        interactor.SceneInit(play.sceneNum);
        ExpectMirror(false, "leaving a dungeon did not disable mirror mode");
    } else if (scenario == "file_select_off") {
        ShipInit::InitAll();
        gPlayState = &play;
        Select(MIRRORED_WORLD_ALWAYS);
        ExpectMirror(true, "Always did not enable mirror mode during play");
        gPlayState = nullptr;
        Select(MIRRORED_WORLD_OFF);
        ExpectMirror(false, "Off at file select retained the previous scene's mirror flag");
        gPlayState = &play;
        interactor.SceneInit(play.sceneNum);
        ExpectMirror(false, "disabled mirror callback survived file-select Off");
    } else if (scenario == "alternating") {
        ShipInit::InitAll();
        gPlayState = &play;
        for (int i = 0; i < 4; ++i) {
            Select(MIRRORED_WORLD_ALWAYS);
            ExpectMirror(true, "alternating Always failed");
            const auto calls = patchCalls;
            Select(MIRRORED_WORLD_ALWAYS);
            interactor.SceneInit(play.sceneNum);
            Require(patchCalls == calls, "unchanged mirror state reapplied graphics patches");
            Select(MIRRORED_WORLD_OFF);
            ExpectMirror(false, "alternating Off failed");
            interactor.SceneInit(play.sceneNum);
            ExpectMirror(false, "Off left an enabled scene callback");
        }
    } else if (scenario == "defer_without_scene") {
        ShipInit::InitAll();
        Select(MIRRORED_WORLD_RANDOM);
        Require(randomCalls == 0 && patchCalls == 0,
                "selecting random mirror at file select evaluated a nonexistent scene");
        gPlayState = &play;
        interactor.SceneInit(play.sceneNum);
        Require(randomCalls == 1, "random mirror scene callback did not execute exactly once");
        ExpectMirror(true, "first scene did not apply its selected mirror mode");
    } else {
        Require(false, "unknown scenario");
    }
    std::cout << scenario << " passed\n";
}
'''

with tempfile.TemporaryDirectory(prefix='soh-mirror-test-') as directory:
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
    for scenario in ['startup_off', 'startup_dungeon', 'file_select_off', 'alternating',
                     'defer_without_scene']:
        if subprocess.run([str(executable), scenario]).returncode:
            failures.append(scenario)
    if failures:
        sys.exit('Mirror regression failed: ' + ', '.join(failures))
