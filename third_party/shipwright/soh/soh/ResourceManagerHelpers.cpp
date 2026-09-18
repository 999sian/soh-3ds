#include <libultraship/bridge/consolevariablebridge.h>
#include <fast/interpreter.h>
#include <fast/resource/ResourceType.h>
#include <fast/resource/type/DisplayList.h>
#include <libultraship/bridge/resourcebridge.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <stb_image.h>
#ifdef __3DS__
#include "audio_font_metadata_3ds.h"
#include <ship/resource/archive/O2rArchive.h>
#include <ship/resource/archive/RoomPrefetchPolicy3DS.h>
#include <malloc.h>
#include <cstring>
#include <string_view>
// libctru's envGetHeapSize is inline; read the same exported reservation here
// without pulling libctru's full type/macro namespace into the game headers.
extern "C" uint32_t __ctru_heap_size;
extern "C" bool Soh3dsUsesOldProfile();
#endif

#include "ResourceManagerHelpers.h"
#include "OTRGlobals.h"
#include "cvar_prefixes.h"
#include "Enhancements/enhancementTypes.h"
#include "Enhancements/randomizer/dungeon.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include <soh/GameVersions.h>
#include "resource/type/SohResourceType.h"
#include "resource/type/Array.h"
#include "resource/type/Skeleton.h"
#include "resource/type/PlayerAnimation.h"

extern "C" {
#include "variables.h"
#include "z64.h"
#include "macros.h"
}

extern "C" PlayState* gPlayState;

struct LinkTunicDListCacheKey {
    size_t operator()(const std::pair<std::string, const char*>& key) const {
        return std::hash<std::string>{}(key.first) ^ std::hash<const char*>{}(key.second);
    }
};

static const char* ResourceMgr_ResolveLinkTunicDListPath(const char* path) {
    if (path == nullptr) {
        return nullptr;
    }

    const char* originalPath = path;
    constexpr std::string_view adultPrefix = "__OTR__objects/object_link_boy/";
    constexpr std::string_view childPrefix = "__OTR__objects/object_link_child/";

    std::string_view objectPrefix;
    const char* objectFolder;

    if (std::string_view(originalPath).starts_with(adultPrefix)) {
        objectPrefix = adultPrefix;
        objectFolder = "object_link_boy";
    } else if (std::string_view(originalPath).starts_with(childPrefix)) {
        objectPrefix = childPrefix;
        objectFolder = "object_link_child";
    } else {
        return path;
    }

    const char* tunicSuffix = nullptr;
    switch (TUNIC_EQUIP_TO_PLAYER(CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC))) {
        case PLAYER_TUNIC_KOKIRI:
            tunicSuffix = "kokiri";
            break;
        case PLAYER_TUNIC_GORON:
            tunicSuffix = "goron";
            break;
        case PLAYER_TUNIC_ZORA:
            tunicSuffix = "zora";
            break;
        default:
            return path;
    }

    static std::unordered_map<std::pair<std::string, const char*>, std::string, LinkTunicDListCacheKey>
        sResolvedLinkTunicDListPaths;
    std::pair<std::string, const char*> cacheKey{ originalPath, tunicSuffix };
    if (auto it = sResolvedLinkTunicDListPaths.find(cacheKey); it != sResolvedLinkTunicDListPaths.end()) {
        return it->second.c_str();
    }

    const std::string candidate = spdlog::fmt_lib::format("__OTR__objects/{}_{}/{}", objectFolder, tunicSuffix,
                                                          originalPath + objectPrefix.size());

    if (!ResourceMgr_IsAltAssetsEnabled() || !ResourceMgr_FileAltExists(candidate.c_str()) ||
        !ResourceGetIsCustomByName(candidate.c_str())) {
        return path;
    }

    auto it = sResolvedLinkTunicDListPaths.emplace(std::move(cacheKey), candidate).first;
    return it->second.c_str();
}

extern "C" uint32_t ResourceMgr_GetNumGameVersions() {
    return static_cast<u32>(
        Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->GetGameVersions().size());
}

extern "C" uint32_t ResourceMgr_GetGameVersion(int index) {
    return Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->GetGameVersions()[index];
}

extern "C" uint32_t ResourceMgr_GetGamePlatform(int index) {
    uint32_t version =
        Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->GetGameVersions()[index];

    switch (version) {
        case OOT_NTSC_US_10:
        case OOT_NTSC_US_11:
        case OOT_NTSC_US_12:
        case OOT_PAL_10:
        case OOT_PAL_11:
            return GAME_PLATFORM_N64;
        case OOT_NTSC_JP_GC:
        case OOT_NTSC_JP_GC_CE:
        case OOT_NTSC_US_GC:
        case OOT_PAL_GC:
        case OOT_NTSC_JP_MQ:
        case OOT_NTSC_US_MQ:
        case OOT_PAL_MQ:
        case OOT_PAL_GC_DBG1:
        case OOT_PAL_GC_DBG2:
        case OOT_PAL_GC_MQ_DBG:
            return GAME_PLATFORM_GC;
        default:
            assert(false);
            return GAME_PLATFORM_UNKNOWN;
    }
}

extern "C" uint32_t ResourceMgr_GetGameRegion(int index) {
    uint32_t version =
        Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->GetGameVersions()[index];

    switch (version) {
        case OOT_NTSC_US_10:
        case OOT_NTSC_US_11:
        case OOT_NTSC_US_12:
        case OOT_NTSC_JP_GC:
        case OOT_NTSC_JP_GC_CE:
        case OOT_NTSC_US_GC:
        case OOT_NTSC_JP_MQ:
        case OOT_NTSC_US_MQ:
            return GAME_REGION_NTSC;
        case OOT_PAL_10:
        case OOT_PAL_11:
        case OOT_PAL_GC:
        case OOT_PAL_MQ:
        case OOT_PAL_GC_DBG1:
        case OOT_PAL_GC_DBG2:
        case OOT_PAL_GC_MQ_DBG:
            return GAME_REGION_PAL;
        default:
            assert(false);
            return GAME_REGION_UNKNOWN;
    }
}

extern "C" char* _message_0xFFFC_nes;
extern "C" bool ResourceMgr_IsPalLoaded() {
    return _message_0xFFFC_nes != NULL;
}

u32 IsSceneMasterQuest(s16 sceneNum) {
    u8 mqMode = CVarGetInteger(CVAR_GENERAL("BetterDebugWarpScreenMQMode"), WARP_MODE_OVERRIDE_OFF);
    if (mqMode == WARP_MODE_OVERRIDE_MQ_AS_VANILLA) {
        return true;
    }

    if (mqMode == WARP_MODE_OVERRIDE_VANILLA_AS_MQ) {
        return false;
    }

    if (OTRGlobals::Instance->HasMasterQuest()) {
        if (!OTRGlobals::Instance->HasOriginal()) {
            return true;
        }

        if (IS_MASTER_QUEST) {
            return true;
        }

        if (IS_RANDO) {
            auto dungeon = OTRGlobals::Instance->gRandoContext->GetDungeonFromScene((SceneID)sceneNum);
            if (dungeon != nullptr && dungeon->IsMQ()) {
                return true;
            }
        }
    }

    return false;
}

extern "C" uint32_t ResourceMgr_GameHasMasterQuest() {
    return OTRGlobals::Instance->HasMasterQuest();
}

extern "C" uint32_t ResourceMgr_GameHasOriginal() {
    return OTRGlobals::Instance->HasOriginal();
}

extern "C" uint32_t ResourceMgr_IsSceneMasterQuest(s16 sceneNum) {
    return IsSceneMasterQuest(sceneNum);
}

extern "C" uint32_t ResourceMgr_IsGameMasterQuest() {
    return gPlayState != NULL ? IsSceneMasterQuest(gPlayState->sceneNum) : 0;
}

extern "C" void ResourceMgr_LoadDirectory(const char* resName) {
    Ship::Context::GetRawInstance()->GetResourceManager()->LoadResources(resName);
}

#ifdef __3DS__
extern "C" void ResourceMgr_PrefetchRoom3DS(const char* roomPath) {
    const auto manager = Ship::Context::GetRawInstance()->GetResourceManager();
    const auto archives = manager->GetArchiveManager()->GetArchives();
    // Drop the previous room before measuring headroom. Archive locks serialize
    // these operations with the existing resource-loading worker.
    for (const auto& archive : *archives) {
        if (auto zip = std::dynamic_pointer_cast<Ship::O2rArchive>(archive)) zip->PrefetchRoom("", 0);
    }
    try {
        if (!roomPath) return;
        const auto prefix = Ship::RoomPrefix3DS(roomPath, ResourceMgr_IsGameMasterQuest());
        if (prefix.empty()) return;
        const auto heap = mallinfo();
        const size_t capacity = __ctru_heap_size;
        const size_t used = static_cast<size_t>(heap.uordblks);
        size_t remaining = Ship::RoomPrefetchBudget3DS(capacity > used ? capacity - used : 0,
            Soh3dsUsesOldProfile(), manager->GetCachedResource(prefix) != nullptr);
        // One budget across all mounted archives. Mods get priority; reads
        // still use ArchiveManager's normal override and alternate-asset rules.
        for (auto it = archives->rbegin(); it != archives->rend() && remaining > 0; ++it) {
            if (auto zip = std::dynamic_pointer_cast<Ship::O2rArchive>(*it)) {
                remaining -= zip->PrefetchRoom(prefix, remaining);
            }
        }
    } catch (const std::bad_alloc&) {
        for (const auto& archive : *archives) {
            if (auto zip = std::dynamic_pointer_cast<Ship::O2rArchive>(archive)) zip->PrefetchRoom("", 0);
        }
    }
}
#endif

extern "C" void ResourceMgr_DirtyDirectory(const char* resName) {
    Ship::Context::GetRawInstance()->GetResourceManager()->DirtyResources(resName);
}

extern "C" void ResourceMgr_UnloadResource(const char* resName) {
    std::string path = resName;
    if (path.substr(0, 7) == "__OTR__") {
        path = path.substr(7);
    }
    auto res = Ship::Context::GetRawInstance()->GetResourceManager()->UnloadResource(path);
}

// OTRTODO: There is probably a more elegant way to go about this...
// Caller must free each string and the array itself when done.
extern "C" char** ResourceMgr_ListFiles(const char* searchMask, int* resultSize) {
    auto lst = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->ListFiles(searchMask);
    char** result = (char**)malloc(lst->size() * sizeof(char*));

    for (size_t i = 0; i < lst->size(); i++) {
        char* str = (char*)malloc(lst.get()[0][i].size() + 1);
        memcpy(str, lst.get()[0][i].data(), lst.get()[0][i].size());
        str[lst.get()[0][i].size()] = '\0';
        result[i] = str;
    }
    *resultSize = static_cast<int>(lst->size());

    return result;
}

extern "C" uint8_t ResourceMgr_FileExists(const char* filePath) {
    std::string path = filePath;
    if (path.substr(0, 7) == "__OTR__") {
        path = path.substr(7);
    }

    return ExtensionCache.contains(path);
}

extern "C" uint8_t ResourceMgr_FileAltExists(const char* filePath) {
    std::string path = filePath;
    if (path.substr(0, 7) == "__OTR__") {
        path = path.substr(7);
    }

    if (path.substr(0, 4) != "alt/") {
        path = "alt/" + path;
    }

    return ExtensionCache.contains(path);
}

extern "C" bool ResourceMgr_IsAltAssetsEnabled() {
    return Ship::Context::GetRawInstance()->GetResourceManager()->IsAltAssetsEnabled();
}

// Unloads a resource if an alternate version exists when alt assets are enabled
// The resource is only removed from the internal cache to prevent it from used in the next resource lookup
extern "C" void ResourceMgr_UnloadOriginalWhenAltExists(const char* resName) {
    if (ResourceMgr_IsAltAssetsEnabled() && ResourceMgr_FileAltExists((char*)resName)) {
        ResourceMgr_UnloadResource((char*)resName);
    }
}

std::shared_ptr<Ship::IResource> ResourceMgr_GetResourceByNameHandlingMQ(const char* path) {
    // SoH-3DS: this is the by-name lookup the game runs per display list per frame (Link's limb
    // DLs, chests, GbiWrap) and it nearly always hits the cache, so the hit path must not
    // allocate. Probe with a view first; only a miss builds the owning string, and the MQ
    // rewrite uses a stack buffer so it stays allocation-free too.
    // Context owns the manager for the whole run; take the raw pointer so the hit path
    // does not do shared_ptr refcount traffic per lookup either.
    auto* resourceManager = Ship::Context::GetRawInstance()->GetResourceManager().get();
    const size_t length = std::strlen(path);
    std::string_view view(path, length);
    char rewritten[256];
    if (ResourceMgr_IsGameMasterQuest()) {
        const size_t position = view.find("/nonmq/");
        if (position != std::string_view::npos) {
            if (length - 3 < sizeof(rewritten)) {
                // "/nonmq/" -> "/mq/": shrinks by 3, so the tail moves down.
                std::memcpy(rewritten, path, position);
                std::memcpy(rewritten + position, "/mq/", 4);
                const size_t tail = length - position - 7;
                std::memcpy(rewritten + position + 4, path + position + 7, tail);
                view = std::string_view(rewritten, position + 4 + tail);
            } else {
                std::string resolvedPath(path);
                resolvedPath.replace(position, 7, "/mq/");
                return resourceManager->LoadResource(std::move(resolvedPath));
            }
        }
    }
    if (auto cached = resourceManager->GetCachedResource(view)) {
        return cached;
    }
    return resourceManager->LoadResource(std::string(view));
}

extern "C" char* ResourceMgr_GetResourceDataByNameHandlingMQ(const char* path) {
    auto res = ResourceMgr_GetResourceByNameHandlingMQ(path);

    if (res == nullptr) {
        return nullptr;
    }

    return (char*)res->GetRawPointer();
}

extern "C" uint8_t ResourceMgr_TexIsRaw(const char* texPath) {
    auto res = std::static_pointer_cast<Fast::Texture>(ResourceMgr_GetResourceByNameHandlingMQ(texPath));
    return res->Flags & TEX_FLAG_LOAD_AS_RAW;
}

extern "C" uint8_t ResourceMgr_ResourceIsBackground(char* texPath) {
    auto res = ResourceMgr_GetResourceByNameHandlingMQ(texPath);
    // SoH-3DS: hardware dump 21 (data abort, FAR=4) was this exact deref of
    // a null lookup. The emulator repro proved the trigger: the resource's
    // own load threw a transient bad_alloc at the heap ceiling ("resource:
    // exception loading scenes/.../link_home_room_0Background...:
    // std::bad_alloc" logged immediately before these null lookups) - NOT
    // object eviction. Transient failures write no NotFound cache line, so
    // a later lookup of the same path MAY succeed; this function itself
    // only classifies - it schedules nothing. Null means "not a
    // background"; the caller's normal-texture path has its own
    // null-background guard downstream.
    if (res == nullptr) {
        fprintf(stderr, "resource: IsBackground null lookup %s\n", texPath);
        return 0;
    }
    return res->GetInitData()->Type == static_cast<uint32_t>(SOH::ResourceType::SOH_Background);
}

// Returns an owned buffer; the caller releases it after copying the RGBA16 image.
extern "C" char* ResourceMgr_LoadJPEG(char* data, size_t dataSize) {
    if (data == nullptr || dataSize == 0 || dataSize > 0x7fffffff) return nullptr;
    int w = 0, h = 0, comp = 0;
    // JPEG has no alpha. Convert RGB to RGBA16 in place to avoid a second
    // image allocation and the old permanently retained conversion buffer.
    unsigned char* pixels = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(data), static_cast<int>(dataSize), &w, &h, &comp, STBI_rgb);
    if (pixels == nullptr) {
        fprintf(stderr, "resource: JPEG decode failed: %s\n", stbi_failure_reason());
        return nullptr;
    }
    if (w <= 0 || h <= 0 || static_cast<size_t>(w) > dataSize / 2 / static_cast<size_t>(h) ||
        static_cast<size_t>(w) * h * 2 != dataSize) {
        stbi_image_free(pixels);
        return nullptr;
    }
    const size_t count = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < count; ++i) {
        const uint16_t pixel = ((pixels[i * 3] >> 3) << 11) |
                               ((pixels[i * 3 + 1] >> 3) << 6) |
                               ((pixels[i * 3 + 2] >> 3) << 1) | 1;
        pixels[i * 2] = pixel >> 8;
        pixels[i * 2 + 1] = pixel & 0xff;
    }
    return reinterpret_cast<char*>(pixels);
}

extern "C" char* ResourceMgr_LoadTexOrDListByName(const char* filePath) {
    auto res = ResourceMgr_GetResourceByNameHandlingMQ(filePath);

    if (res->GetInitData()->Type == static_cast<uint32_t>(Fast::ResourceType::DisplayList)) {
        return (char*)&((std::static_pointer_cast<Fast::DisplayList>(res))->Instructions[0]);
    }

    if (res->GetInitData()->Type == static_cast<uint32_t>(SOH::ResourceType::SOH_Array)) {
        return (char*)(std::static_pointer_cast<SOH::Array>(res))->Vertices.data();
    }

    return (char*)ResourceMgr_GetResourceDataByNameHandlingMQ(filePath);
}

extern "C" char* ResourceMgr_LoadIfDListByName(const char* filePath) {
    auto res = ResourceMgr_GetResourceByNameHandlingMQ(filePath);

    if (res->GetInitData()->Type == static_cast<uint32_t>(Fast::ResourceType::DisplayList)) {
        return (char*)&((std::static_pointer_cast<Fast::DisplayList>(res))->Instructions[0]);
    }

    return nullptr;
}

extern "C" char* ResourceMgr_LoadPlayerAnimByName(const char* animPath) {
    auto anim = std::static_pointer_cast<SOH::PlayerAnimation>(ResourceMgr_GetResourceByNameHandlingMQ(animPath));

    return (char*)&anim->limbRotData[0];
}

extern "C" Gfx* ResourceMgr_LoadGfxByName(const char* path) {
    path = ResourceMgr_ResolveLinkTunicDListPath(path);
    // When an alt resource exists for the DL, we need to unload the original asset
    // to clear the cache so the alt asset will be loaded instead
    // OTRTODO: If Alt loading over original cache is fixed, this line can most likely be removed
    ResourceMgr_UnloadOriginalWhenAltExists(path);

    auto res = std::static_pointer_cast<Fast::DisplayList>(ResourceMgr_GetResourceByNameHandlingMQ(path));
    if (!res)
        return nullptr;
    return (Gfx*)&res->Instructions[0];
}

extern "C" uint8_t ResourceMgr_FileIsCustomByName(const char* path) {
    auto res = std::static_pointer_cast<Fast::DisplayList>(ResourceMgr_GetResourceByNameHandlingMQ(path));
    return res->GetInitData()->IsCustom;
}

typedef struct {
    int index;
    Gfx instruction;
    const void* instructionsPtr;
    size_t instructionCount;
    bool isCustom;
} GfxPatch;

std::unordered_map<std::string, std::unordered_map<std::string, GfxPatch>> originalGfx;

// Attention! This is primarily for cosmetics & bug fixes. For things like mods and model replacement you should be
// using OTRs instead (When that is available). Index can be found using the commented out section below.
extern "C" void ResourceMgr_PatchGfxByName(const char* path, const char* patchName, int index, Gfx instruction) {
    auto res = std::static_pointer_cast<Fast::DisplayList>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path));

    if (res == nullptr || static_cast<size_t>(index) >= res->Instructions.size()) {
        return;
    }

    // Leaving this here for people attempting to find the correct Dlist index to patch
    /*if (strcmp("__OTR__objects/object_gi_longsword/gGiBiggoronSwordDL", path) == 0) {
        for (int i = 0; i < res->instructions.size(); i++) {
            Gfx* gfx = (Gfx*)&res->instructions[i];
            // Log all commands
            // SPDLOG_INFO("index:{} command:{}", i, gfx->words.w0 >> 24);
            // Log only SetPrimColors
            if (gfx->words.w0 >> 24 == 250) {
                SPDLOG_INFO("index:{} r:{} g:{} b:{} a:{}", i, _SHIFTR(gfx->words.w1, 24, 8), _SHIFTR(gfx->words.w1, 16,
    8), _SHIFTR(gfx->words.w1, 8, 8), _SHIFTR(gfx->words.w1, 0, 8));
            }
        }
    }*/

    // Index refers to individual gfx words, which are half the size on 32-bit
    // if (sizeof(uintptr_t) < 8) {
    // index /= 2;
    // }

    // Do not patch custom assets as they most likely do not have the same instructions as authentic assets
    if (res->GetInitData()->IsCustom) {
        return;
    }

    Gfx* gfx = (Gfx*)&res->Instructions[index];

    if (!originalGfx.contains(path) || !originalGfx[path].contains(patchName)) {
        originalGfx[path][patchName] = { index, *gfx, res->Instructions.data(), res->Instructions.size(),
                                         res->GetInitData()->IsCustom };
    }

    *gfx = instruction;
}

extern "C" void ResourceMgr_PatchGfxCopyCommandByName(const char* path, const char* patchName, int destinationIndex,
                                                      int sourceIndex) {
    auto res = std::static_pointer_cast<Fast::DisplayList>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path));

    if (res == nullptr || static_cast<size_t>(destinationIndex) >= res->Instructions.size() ||
        static_cast<size_t>(sourceIndex) >= res->Instructions.size()) {
        return;
    }

    // Do not patch custom assets as they most likely do not have the same instructions as authentic assets
    if (res->GetInitData()->IsCustom) {
        return;
    }

    Gfx* destinationGfx = (Gfx*)&res->Instructions[destinationIndex];
    Gfx sourceGfx = *(Gfx*)&res->Instructions[sourceIndex];

    if (!originalGfx.contains(path) || !originalGfx[path].contains(patchName)) {
        originalGfx[path][patchName] = { destinationIndex, *destinationGfx, res->Instructions.data(),
                                         res->Instructions.size(), res->GetInitData()->IsCustom };
    }

    *destinationGfx = sourceGfx;
}

extern "C" void ResourceMgr_PatchCustomGfxByName(const char* path, const char* patchName, int index, Gfx instruction) {
    auto res = std::static_pointer_cast<Fast::DisplayList>(
        Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path));

    if (res == nullptr || static_cast<size_t>(index) >= res->Instructions.size()) {
        return;
    }

    Gfx* gfx = (Gfx*)&res->Instructions[index];

    if (!originalGfx.contains(path) || !originalGfx[path].contains(patchName)) {
        originalGfx[path][patchName] = { index, *gfx, res->Instructions.data(), res->Instructions.size(),
                                         res->GetInitData()->IsCustom };
    }

    *gfx = instruction;
}

extern "C" void ResourceMgr_UnpatchGfxByName(const char* path, const char* patchName) {
    if (originalGfx.contains(path) && originalGfx[path].contains(patchName)) {
        auto res = std::static_pointer_cast<Fast::DisplayList>(
            Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(path));

        // If the resource is unavailable (e.g. swapped out when toggling alt assets), clean up the record and bail.
        if (res == nullptr) {
            ResourceMgr_UnloadResource(path);
            originalGfx[path].erase(patchName);
            return;
        }

        const GfxPatch& patch = originalGfx[path][patchName];
        // Skip and clean up if the backing resource changed since we recorded the patch (e.g. alt<->vanilla swap)
        // to avoid writing instructions from a different asset onto the current one.
        if (res->Instructions.data() != patch.instructionsPtr || res->Instructions.size() != patch.instructionCount ||
            res->GetInitData()->IsCustom != patch.isCustom) {
            ResourceMgr_UnloadResource(path);
            originalGfx[path].erase(patchName);
            return;
        }

        // Skip and clean up if the loaded resource is smaller than the recorded patch index (can happen when alt assets
        // swap in shorter display lists).
        if (static_cast<size_t>(patch.index) >= res->Instructions.size()) {
            originalGfx[path].erase(patchName);
            return;
        }

        Gfx* gfx = (Gfx*)&res->Instructions[patch.index];
        *gfx = patch.instruction;

        originalGfx[path].erase(patchName);
    }
}

extern "C" char* ResourceMgr_LoadArrayByName(const char* path) {
    auto res = std::static_pointer_cast<SOH::Array>(ResourceMgr_GetResourceByNameHandlingMQ(path));

    return (char*)res->Scalars.data();
}

// Return of LoadArrayByNameAsVec3s must be freed by the caller
extern "C" char* ResourceMgr_LoadArrayByNameAsVec3s(const char* path) {
    auto res = std::static_pointer_cast<SOH::Array>(ResourceMgr_GetResourceByNameHandlingMQ(path));

    // if (res->CachedGameAsset != nullptr)
    //     return (char*)res->CachedGameAsset;
    // else
    // {
    Vec3s* data = (Vec3s*)malloc(sizeof(Vec3s) * res->Scalars.size());

    for (size_t i = 0; i < res->Scalars.size(); i += 3) {
        data[(i / 3)].x = res->Scalars[i + 0].s16;
        data[(i / 3)].y = res->Scalars[i + 1].s16;
        data[(i / 3)].z = res->Scalars[i + 2].s16;
    }

    // res->CachedGameAsset = data;

    return (char*)data;
    // }
}

extern "C" CollisionHeader* ResourceMgr_LoadColByName(const char* path) {
    return (CollisionHeader*)ResourceGetDataByName(path);
}

extern "C" Vtx* ResourceMgr_LoadVtxByName(char* path) {
    return (Vtx*)ResourceGetDataByName(path);
}

extern "C" SequenceData ResourceMgr_LoadSeqByName(const char* path) {
    SequenceData* sequence = (SequenceData*)ResourceGetDataByName(path);
    return *sequence;
}

extern "C" SequenceData* ResourceMgr_LoadSeqPtrByName(const char* path) {
    SequenceData* sequence = (SequenceData*)ResourceGetDataByName(path);
    return sequence;
}

extern "C" SoundFontSample* ResourceMgr_LoadAudioSample(const char* path) {
    return (SoundFontSample*)ResourceGetDataByName(path);
}

extern "C" SoundFont* ResourceMgr_LoadAudioSoundFontByName(const char* path) {
    // SoH-3DS: fontMap lookups can legitimately produce NULL (out-of-table or
    // unset SAF fontIds); std::string(NULL) downstream is UB.
    if (path == nullptr) {
        return nullptr;
    }
    return (SoundFont*)ResourceGetDataByName(path);
}

extern "C" int ResourceMgr_GetAudioSoundFontIndex(const char* path) {
    if (path == nullptr) return -1;
#ifdef __3DS__
    if (Soh3dsUsesOldProfile()) {
        const auto manager = Ship::Context::GetRawInstance()->GetResourceManager();
        // Aliases, alternate assets and nonstandard formats keep the normal
        // importer. For standard fonts, indexing need not load their samples.
        if (!manager->IsAltAssetsEnabled() && !manager->GetArchiveManager()->HasFile(std::string(path) + ".meta")) {
            const auto file = manager->LoadFileProcess(path);
            if (file && file->Buffer) {
                const int index = Soh3dsReadFontIndex(reinterpret_cast<const uint8_t*>(file->Buffer->data()),
                                                      file->Buffer->size());
                if (index >= 0) return index;
            }
        }
    }
#endif
    const SoundFont* font = ResourceMgr_LoadAudioSoundFontByName(path);
    return font ? font->fntIndex : -1;
}

extern "C" int ResourceMgr_OTRSigCheck(char* imgData) {
    uintptr_t i = (uintptr_t)(imgData);

    // if (i == 0xD9000000 || i == 0xE7000000 || (i & 1) == 1)
    if ((i & 1) == 1)
        return 0;

    // if ((i & 0xFF000000) != 0xAB000000 && (i & 0xFF000000) != 0xCD000000 && i != 0) {
    if (i != 0) {
        if (imgData[0] == '_' && imgData[1] == '_' && imgData[2] == 'O' && imgData[3] == 'T' && imgData[4] == 'R' &&
            imgData[5] == '_' && imgData[6] == '_') {
            return 1;
        }
    }

    return 0;
}

// Load animation with explicit alt asset path checking.
// When Alt Assets is OFF: use original path directly (O2R or vanilla)
// When Alt Assets is ON: try alt/ prefix first, fall back to regular path if not found or invalid
extern "C" AnimationHeaderCommon* ResourceMgr_LoadAnimByName(const char* path) {
    bool isAlt = ResourceMgr_IsAltAssetsEnabled();

    if (isAlt) {
        if (ResourceMgr_FileAltExists(path)) {
            std::string pathStr = std::string(path);
            static const std::string sOtr = "__OTR__";

            if (pathStr.starts_with(sOtr)) {
                pathStr = pathStr.substr(sOtr.length());
            }

            // Try alt/ first
            pathStr = Ship::IResource::gAltAssetPrefix + pathStr;

            AnimationHeaderCommon* animHeader = (AnimationHeaderCommon*)ResourceGetDataByName(pathStr.c_str());

            // If alt loaded successfully, verify it has valid data
            if (animHeader != NULL) {
                // Check for valid frame count (> 0)
                if (animHeader->frameCount > 0) {
                    // For Normal animations: check frameData (comes after frameCount in AnimationHeader)
                    // For Link animations: check segment (comes after frameCount in LinkAnimationHeader)
                    // We check both to be safe - if either is valid, the animation is usable
                    AnimationHeader* normalAnim = (AnimationHeader*)animHeader;
                    LinkAnimationHeader* linkAnim = (LinkAnimationHeader*)animHeader;

                    // Valid if Normal animation has frameData OR Link animation has segment
                    if (normalAnim->frameData != NULL || linkAnim->segment != NULL) {
                        return animHeader;
                    }
                }
                // Alt loaded but is invalid (broken), fall through to original path
            }
        }

        // Fall back to original path
        return (AnimationHeaderCommon*)ResourceGetDataByName(path);
    }

    // Alt OFF: use original path directly
    return (AnimationHeaderCommon*)ResourceGetDataByName(path);
}

extern "C" SkeletonHeader* ResourceMgr_LoadSkeletonByName(const char* path, SkelAnime* skelAnime) {
    std::string pathStr = std::string(path);
    static const std::string sOtr = "__OTR__";

    if (pathStr.starts_with(sOtr)) {
        pathStr = pathStr.substr(sOtr.length());
    }

    bool isAlt = ResourceMgr_IsAltAssetsEnabled();

    if (isAlt) {
        pathStr = Ship::IResource::gAltAssetPrefix + pathStr;
    }

    SkeletonHeader* skelHeader = (SkeletonHeader*)ResourceGetDataByName(pathStr.c_str());

    // If there isn't an alternate model, load the regular one
    if (isAlt && skelHeader == NULL) {
        skelHeader = (SkeletonHeader*)ResourceGetDataByName(path);
    }

    // This function is only called when a skeleton is initialized.
    // Therefore we can take this opportunity to take note of the Skeleton that is created...
    if (skelAnime != nullptr) {
        auto stringPath = std::string(path);
        SOH::SkeletonPatcher::RegisterSkeleton(stringPath, skelAnime);
    }

    return skelHeader;
}

extern "C" void ResourceMgr_UnregisterSkeleton(SkelAnime* skelAnime) {
    if (skelAnime != nullptr) {
        SOH::SkeletonPatcher::UnregisterSkeleton(skelAnime);
    }
}

extern "C" void ResourceMgr_ClearSkeletons() {
    SOH::SkeletonPatcher::ClearSkeletons();
}

extern "C" s32* ResourceMgr_LoadCSByName(const char* path) {
    return (s32*)ResourceMgr_GetResourceDataByNameHandlingMQ(path);
}
