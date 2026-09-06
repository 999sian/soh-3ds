#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/utils/StringHelper.h>
#ifdef __3DS__
#include <ship/resource/archive/ArchiveManager.h>
#include <fast/resource/ResourceType.h>
#include <fast/resource/type/Texture.h>
#include <malloc.h>
#include <cstdio>
// OTRGlobals.h only exposes this to C translation units.
extern "C" void Gfx_TextureCacheDelete(const uint8_t* addr);
extern "C" void Soh3dsResourceCacheReport(char* out, unsigned size);
// libctru's application heap reservation - the hard cap malloc's sbrk grows
// into. Declared here rather than via <3ds.h>, which cannot be included in
// this translation unit: its u32/s32 typedefs collide with libultra's. MUST
// stay at file scope with C linkage; inside a namespace it mangles to a
// different, undefined symbol.
extern "C" unsigned int __ctru_heap_size;
#include "soh/frame_interpolation.h"
#endif
#include <spdlog/spdlog.h>

#include "ResourceManagerHelpers.h"
#include "soh/resource/type/Scene.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include "functions.h"
#include "variables.h"
}

extern "C" void Play_InitScene(PlayState* play, s32 spawn);
extern "C" void Play_InitEnvironment(PlayState* play, s16 skyboxId);
void OTRPlay_InitScene(PlayState* play, s32 spawn);
s32 OTRScene_ExecuteCommands(PlayState* play, SOH::Scene* scene);

// LUS::OTRResource* OTRPlay_LoadFile(PlayState* play, RomFile* file) {
Ship::IResource* OTRPlay_LoadFile(PlayState* play, const char* fileName) {
    auto res = Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(fileName);
    return res.get();
}

#ifdef __3DS__
// SoH-3DS: recorded-object-set eviction, the MK64-3DS pattern (its LoadTrack
// records the track's directories; the CM_CleanWorld boundary releases that
// exact set - never a global sweep, which regressed when we tried it).
// Play_Destroy records the departing scene's object bank here; the eviction
// seam below releases those directories, minus the always-loaded keeps and
// Link objects (also MK64's exceptions: pinned, and the cosmetics-patch hot
// spots). The incoming scene lazily reloads anything it shares.
#define DEFINE_OBJECT(name, enum_) #name,
#define DEFINE_OBJECT_NULL(name, enum_) "",
#define DEFINE_OBJECT_UNSET(enum_) "",
static const char* const sSoh3dsObjectDirNames[] = {
#include "tables/object_table.h"
};
#undef DEFINE_OBJECT
#undef DEFINE_OBJECT_NULL
#undef DEFINE_OBJECT_UNSET

static s16 sSoh3dsDepartingObjects[35];
static s32 sSoh3dsDepartingObjectCount = 0;

extern "C" void Soh3dsRecordDepartingObjects(const s16* ids, s32 count) {
    if (count > (s32)(sizeof(sSoh3dsDepartingObjects) / sizeof(sSoh3dsDepartingObjects[0]))) {
        count = (s32)(sizeof(sSoh3dsDepartingObjects) / sizeof(sSoh3dsDepartingObjects[0]));
    }
    for (s32 i = 0; i < count; i++) {
        sSoh3dsDepartingObjects[i] = ids[i];
    }
    sSoh3dsDepartingObjectCount = count;
}
#endif

// SoH-3DS: lets the framebuffer dumper label each capture with the scene it
// actually rendered, by NAME. A numeric id already caused one capture to be
// analysed as the wrong room (entrance 0xC1 was expected to be the Kokiri
// shop; the game was actually in scene 1, Dodongo's Cavern).
static const char* sSoh3dsCurrentScene = "none";
extern "C" const char* Soh3dsCurrentSceneName(void) {
    return sSoh3dsCurrentScene;
}

extern "C" void OTRPlay_SpawnScene(PlayState* play, s32 sceneId, s32 spawn) {
#ifdef __3DS__
    // SoH-3DS DEV: force a scene by ID from sdmc:/3ds/soh/warpto.txt (hex,
    // e.g. "2D" = Kokiri shop). Overriding the ENTRANCE index is unreliable
    // on a randomizer save - entrance shuffle sent 0xC1 (nominally the
    // Kokiri shop) to Dodongo's Cavern. Forcing the scene id bypasses the
    // entrance table completely. Absent file = normal play.
    {
        static s32 sForceScene = -2;
        if (sForceScene == -2) {
            sForceScene = -1;
            FILE* wf = fopen("warpto.txt", "r");
            if (wf != NULL) {
                unsigned v = 0;
                if (fscanf(wf, "%x", &v) == 1 && v < SCENE_ID_MAX) {
                    sForceScene = (s32)v;
                }
                fclose(wf);
            }
        }
        if (sForceScene >= 0) {
            fprintf(stderr, "gfx-dev: forcing scene 0x%X (was 0x%X)\n", (unsigned)sForceScene, (unsigned)sceneId);
            sceneId = sForceScene;
            spawn = 0;
        }
    }
#endif
    sSoh3dsCurrentScene = gSceneTable[sceneId].sceneFile.fileName;
    SceneTableEntry* scene = &gSceneTable[sceneId];

    scene->unk_13 = 0;
    play->loadedScene = scene;
    play->sceneNum = sceneId;
    play->sceneConfig = scene->config;

    // osSyncPrintf("\nSCENE SIZE %fK\n", (scene->sceneFile.vromEnd - scene->sceneFile.vromStart) / 1024.0f);

    // Scenes considered "dungeon" with a MQ variant
    int16_t inNonSharedScene = (sceneId >= SCENE_DEKU_TREE && sceneId <= SCENE_ICE_CAVERN) ||
                               sceneId == SCENE_GERUDO_TRAINING_GROUND || sceneId == SCENE_INSIDE_GANONS_CASTLE;

    std::string sceneVersion = "shared";
    if (inNonSharedScene) {
        sceneVersion = ResourceMgr_IsGameMasterQuest() ? "mq" : "nonmq";
    }
    std::string scenePath = StringHelper::Sprintf("scenes/%s/%s/%s", sceneVersion.c_str(), scene->sceneFile.fileName,
                                                  scene->sceneFile.fileName);

#ifdef __3DS__
    // SoH-3DS: the resource cache has no eviction, so the 66 MB heap climbs to
    // its ceiling within minutes of scene churn and dies of bad_alloc - the
    // dominant crash on hardware and emulator (279 absorbed ceiling events in
    // one measured session before the fatal one). v1 evicts the ENTIRE
    // departing scene directory (scene + rooms + their textures) here:
    //  - Play_SpawnScene's only callers are Play_Init (fresh PlayState; the
    //    previous gamestate - the sole C-side holder of raw pointers into the
    //    departing scene's payloads - was destroyed by GameState_Destroy
    //    before Play_Init ran) and the failed-load fallback recursion;
    //    play->sceneSegment is assigned AFTER this block;
    //  - belt-and-braces: if this play arrives with a live sceneSegment (a
    //    reused PlayState - no known path, but the hazard if one appears),
    //    eviction is skipped outright;
    //  - textures get Gfx_TextureCacheDelete first (GPU copies + raw-address
    //    cache keys); the GPU itself never reads resource memory (uploads
    //    copy into C3D_Tex; retired-texture machinery covers those);
    //  - bulk UnloadResources handles .meta aliases correctly;
    //  - same-scene respawns skip; audio/text/fonts live outside scenes/ and
    //    are untouched; objects are released ONLY from the recorded departing
    //    set (pass 3), never swept.
    //  DEFAULT ON (kill switch: gSceneEvict=0). Soak record, 420 s each:
    //    v1 scenes-only validated twice (zero null loads/faults, ceiling
    //    death pushed to ~frame 3120); v2 global objects sweep REVERTED
    //    (regressed); v2b recorded-set survived to frame 8820 with 649 KiB
    //    reclaims per transition and zero live faults (the post-FATAL
    //    unmapped storm is teardown debris, dumps-18/19 class).
    if (CVarGetInteger("gSceneEvict", 1) && play->sceneSegment == NULL) {
        static std::string sPrevSceneDir;
        std::string newSceneDir = StringHelper::Sprintf("scenes/%s/%s/*", sceneVersion.c_str(),
                                                        scene->sceneFile.fileName);
        if (!sPrevSceneDir.empty() && sPrevSceneDir != newSceneDir) {
            auto resMgr = Ship::Context::GetRawInstance()->GetResourceManager();
            struct mallinfo miBefore = mallinfo();
            long invalidatedBlobs = 0;
            // GPU-side cache teardown for one resource about to be freed.
            //
            // Fast3D keys its texture cache on the ADDRESS handed to SETTIMG,
            // so any freed payload whose address the allocator hands out again
            // would be drawn from the previous occupant's decoded texture. The
            // Texture case is the obvious one, but prerendered room
            // backgrounds (SOH_Background, `scenes/<scene>/..._roomNBackground`)
            // reach the GPU as a RAW payload pointer: z_room.c hands
            // `res->GetRawPointer()` to gDPLoadMultiTile via bg->b.imagePtr.
            // Filtering on Texture alone left those blobs cached under a dead
            // address, and a same-sized background allocated into the freed
            // block then rendered the OLD scene's image - the wrong-image
            // report for the prerendered rooms between Hyrule Castle and the
            // Temple of Time. Invalidating by payload address is unconditional
            // now: a miss costs one re-upload, a stale hit shows a wrong room.
            auto invalidateGpuCache = [&](const std::shared_ptr<Ship::IResource>& res) {
                if (res == nullptr) {
                    return;
                }
                if (res->GetInitData()->Type == static_cast<uint32_t>(Fast::ResourceType::Texture)) {
                    auto tex = std::static_pointer_cast<Fast::Texture>(res);
                    if (tex->ImageData != nullptr) {
                        Gfx_TextureCacheDelete(tex->ImageData);
                    }
                }
                if (auto* raw = (const uint8_t*)res->GetRawPointer(); raw != nullptr) {
                    Gfx_TextureCacheDelete(raw);
                    invalidatedBlobs++;
                }
            };

            auto files = resMgr->GetArchiveManager()->ListFiles(sPrevSceneDir);
            if (files != nullptr) {
                // Pass 1: GPU-side cache teardown (stale-key removal).
                for (const auto& f : *files) {
                    invalidateGpuCache(resMgr->GetCachedResource(f));
                }
            }
            // Pass 2: bulk eviction of the whole scene directory (alias-aware).
            resMgr->UnloadResources(sPrevSceneDir);
            // Pass 3 (v2b): release the RECORDED departing-object set (see the
            // block above OTRPlay_SpawnScene). A global objects/* sweep was
            // tried and regressed (died earlier than baseline, unattributed);
            // this releases only the ~dozen directories the departing scene's
            // object bank actually declared, minus keeps and Link objects.
            // Liveness guard: UnloadResources erases cache entries
            // unconditionally, so any file still owned outside the cache
            // (use_count > 2: the cache line + our probe copy is the
            // baseline) pins its WHOLE directory for this transition -
            // preserving the alias-correct bulk unload for clean dirs and
            // making pins observable (logged) instead of assumed absent.
            {
                constexpr size_t kObjectNameCount = sizeof(sSoh3dsObjectDirNames) / sizeof(sSoh3dsObjectDirNames[0]);
                // Release one objects/<name> directory: keep filter, pin
                // guard, GPU-side texture teardown, alias-aware bulk unload.
                auto releaseObjectDir = [&](const char* name) -> bool {
                    if (name[0] == '\0' || strncmp(name, "gameplay_", 9) == 0 ||
                        strncmp(name, "object_link_", 12) == 0) {
                        return false;
                    }
                    std::string objDir = StringHelper::Sprintf("objects/%s/*", name);
                    auto objFiles = resMgr->GetArchiveManager()->ListFiles(objDir);
                    if (objFiles == nullptr) {
                        return false;
                    }
                    long pinned = 0;
                    for (const auto& f : *objFiles) {
                        auto res = resMgr->GetCachedResource(f);
                        if (res != nullptr && res.use_count() > 2) {
                            pinned++;
                        }
                    }
                    if (pinned != 0) {
                        char line[128];
                        std::snprintf(line, sizeof(line),
                                      "soh-3ds evict: PINNED objects/%s (%ld pinned resources), skipped\n", name,
                                      pinned);
                        std::fputs(line, stderr);
                        return false;
                    }
                    for (const auto& f : *objFiles) {
                        invalidateGpuCache(resMgr->GetCachedResource(f));
                    }
                    resMgr->UnloadResources(objDir);
                    return true;
                };

                for (s32 i = 0; i < sSoh3dsDepartingObjectCount; i++) {
                    s16 id = sSoh3dsDepartingObjects[i];
                    if (id <= 0 || (size_t)id >= kObjectNameCount) {
                        continue;
                    }
                    releaseObjectDir(sSoh3dsObjectDirNames[id]);
                }
                sSoh3dsDepartingObjectCount = 0;

                // Pass 4: wide object sweep, ONLY under heap pressure. The
                // recorded set above covers the departing scene's object BANK
                // (35 slots); objects an actor loaded outside the bank were
                // never released, and hardware telemetry (perf-hw-62b9ca20)
                // showed that bucket climb 306 -> 1256 cache entries in five
                // minutes of play, heap 52.6 -> 70.8 MB against an 84 MB
                // ceiling - the historical OOM. An unconditional global sweep
                // regressed once (see the log above), so it stays gated: below
                // the threshold this seam behaves exactly like the proven
                // recorded-set version, and the wide sweep only runs in the
                // sessions that would otherwise reach the ceiling. Safety here
                // rests on the departing scene's actors already being
                // destroyed, the keep/Link filter, and the pin guard; the
                // incoming scene reloads whatever it still needs.
                // Trigger on HEADROOM, not on an absolute heap figure. The
                // first attempt used "uordblks >= 64 MB" and never fired once
                // in a ten-minute hardware session (perf-hw-5650ffd5: heap
                // reached 66 MB only in the final window) while the cache grew
                // 775 -> 2054 entries - a threshold picked against a derived
                // ceiling rather than a measured one. mallinfo().fordblks is
                // the free space actually left inside the arena, which is the
                // distance to a bad_alloc regardless of what the region size
                // is. envGetHeapSize() is libctru's heap reservation - the
                // hard cap sbrk grows into - so cap minus in-use bytes is the
                // real headroom. gSceneEvictSweepMB is that floor in MB; 0
                // disables the sweep entirely.
                const unsigned long heapCap = (unsigned long)__ctru_heap_size;
                const unsigned long inUse = (unsigned long)miBefore.uordblks;
                const unsigned long headroom = heapCap > inUse ? heapCap - inUse : 0ul;
                const unsigned long sweepFloor =
                    (unsigned long)CVarGetInteger("gSceneEvictSweepMB", 12) * 1024ul * 1024ul;
                if (sweepFloor != 0 && headroom <= sweepFloor) {
                    long released = 0;
                    const auto dirs = resMgr->Soh3dsCachedSubdirs("objects/");
                    for (const auto& dir : dirs) {
                        released += releaseObjectDir(dir.c_str()) ? 1 : 0;
                    }
                    char line[160];
                    std::snprintf(line, sizeof(line),
                                  "soh-3ds evict: pressure sweep, %lu KiB headroom, released %ld of %u dirs\n",
                                  headroom / 1024ul, released, (unsigned)dirs.size());
                    std::fputs(line, stderr);
                }
            }
            // The interpolation recorder retains its node tree across ticks so
            // recording allocates nothing in the steady state; that retention
            // is only bounded here, where a hitch already exists and the
            // departing scene's shape is about to be irrelevant.
            FrameInterpolation_ShrinkRecording();
            struct mallinfo miAfter = mallinfo();
            // One buffered write: Azahar splits a multi-argument fprintf at
            // every format argument, which made these lines unparseable.
            {
                char line[160];
                std::snprintf(line, sizeof(line),
                              "soh-3ds evict: %s reclaimed %d KiB (heap %u -> %u KiB) gpuKeys=%ld\n",
                              sPrevSceneDir.c_str(),
                              (int)(((long)miBefore.uordblks - (long)miAfter.uordblks) / 1024),
                              (unsigned)(miBefore.uordblks / 1024), (unsigned)(miAfter.uordblks / 1024),
                              invalidatedBlobs);
                std::fputs(line, stderr);
            }
            // Which prefixes hold the cache, and how much of each nothing
            // references. Once per transition; it walks every cache line.
            {
                char buckets[320];
                Soh3dsResourceCacheReport(buckets, sizeof(buckets));
                char line[400];
                std::snprintf(line, sizeof(line), "soh-3ds cache: %s\n", buckets);
                std::fputs(line, stderr);
            }
        }
        sPrevSceneDir = newSceneDir;
    }
#endif

    play->sceneSegment = OTRPlay_LoadFile(play, scenePath.c_str());

    // Failed to load scene... default to doodongs cavern
    if (play->sceneSegment == nullptr) {
        lusprintf(__FILE__, __LINE__, 2, "Unable to load scene %s... Defaulting to Doodong's Cavern!\n",
                  scenePath.c_str());
        OTRPlay_SpawnScene(play, 0x01, 0);
        return;
    }

    scene->unk_13 = 0;

    // gSegments[2] = VIRTUAL_TO_PHYSICAL(play->sceneSegment);

    OTRPlay_InitScene(play, spawn);
    auto roomSize = func_80096FE8(play, &play->roomCtx);

    osSyncPrintf("ROOM SIZE=%fK\n", roomSize / 1024.0f);

    GameInteractor_ExecuteOnSceneInit(play->sceneNum);
    SPDLOG_INFO("Scene Init - sceneNum: {0:#x}, entranceIndex: {1:#x}", play->sceneNum, gSaveContext.entranceIndex);
}

void OTRPlay_InitScene(PlayState* play, s32 spawn) {
    play->curSpawn = spawn;
    play->linkActorEntry = nullptr;
    play->unk_11DFC = nullptr;
    play->setupEntranceList = nullptr;
    play->setupExitList = nullptr;
    play->cUpElfMsgs = nullptr;
    play->setupPathList = nullptr;
    play->numSetupActors = 0;
    Object_InitBank(play, &play->objectCtx);
    LightContext_Init(play, &play->lightCtx);
    TransitionActor_InitContext(&play->state, &play->transiActorCtx);
    func_80096FD4(play, &play->roomCtx.curRoom);
    YREG(15) = 0;
    gSaveContext.worldMapArea = 0;
    OTRScene_ExecuteCommands(play, (SOH::Scene*)play->sceneSegment);

    GameInteractor_ExecuteAfterSceneCommands(play->sceneNum);
    Play_InitEnvironment(play, play->skyboxId);
    /* auto data = static_cast<LUS::Vertex*>(Ship::Context::GetRawInstance()
                                               ->GetResourceManager()
                                               ->ResourceLoad("object_link_child\\object_link_childVtx_01FE08")
                                               .get());

    auto data2 = ResourceMgr_LoadVtxByCRC(0x68d4ea06044e228f);*/

    volatile int a = 0;
}
