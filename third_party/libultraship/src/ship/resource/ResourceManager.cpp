#include "ship/resource/ResourceManager.h"
#include <spdlog/spdlog.h>
#include "ship/resource/File.h"
#include "ship/resource/archive/Archive.h"
#include <algorithm>
#include <thread>
#include "ship/utils/StringHelper.h"
#include "ship/utils/Utils.h"
#include "ship/config/ConsoleVariable.h"
#include "ship/Context.h"

namespace Ship {

ResourceFilter::ResourceFilter(const std::list<std::string>& includeMasks, const std::list<std::string>& excludeMasks,
                               const uintptr_t owner, const std::shared_ptr<Archive> parent)
    : IncludeMasks(includeMasks), ExcludeMasks(excludeMasks), Owner(owner), Parent(parent) {
}

size_t ResourceIdentifier::GetHash() const {
    return mHash;
}

ResourceIdentifier::ResourceIdentifier(const std::string& path, const uintptr_t owner,
                                       const std::shared_ptr<Archive> parent)
    : Path(path), Owner(owner), Parent(parent) {
    mHash = CalculateHash();
}

ResourceIdentifier::ResourceIdentifier(std::string&& path, const uintptr_t owner, const std::shared_ptr<Archive> parent)
    : Path(std::move(path)), Owner(owner), Parent(parent) {
    mHash = CalculateHash();
}

bool ResourceIdentifier::operator==(const ResourceIdentifier& rhs) const {
    return Owner == rhs.Owner && Path == rhs.Path && Parent == rhs.Parent;
}

size_t ResourceIdentifier::CalculateHash() {
    size_t hash = Math::HashCombine(std::hash<std::string>{}(Path), std::hash<std::uintptr_t>{}(Owner));
    if (Parent != nullptr) {
        hash = Math::HashCombine(hash, std::hash<std::string>{}(Parent->GetPath()));
    }
    return hash;
}

size_t ResourceIdentifierHash::operator()(const ResourceIdentifier& rcd) const {
    return rcd.GetHash();
}

ResourceManager::ResourceManager() {
}

void ResourceManager::Init(const std::vector<std::string>& archivePaths,
                           const std::unordered_set<uint32_t>& validHashes, int32_t reservedThreadCount) {
    mResourceLoader = std::make_shared<ResourceLoader>();
    mArchiveManager = std::make_shared<ArchiveManager>();
    GetArchiveManager()->Init(archivePaths, validHashes);

    // the extra `- 1` is because we reserve an extra thread for spdlog
    size_t threadCount = std::max<int32_t>(1, (int32_t)(std::thread::hardware_concurrency() - reservedThreadCount - 1));
    mThreadPool = std::make_shared<BS::thread_pool>(threadCount);

    if (!IsLoaded()) {
        // Nothing ever unpauses the thread pool since nothing will ever try to load the archive again.
        mThreadPool->pause();
    }
}

ResourceManager::~ResourceManager() {
    SPDLOG_INFO("destruct ResourceManager");
}

bool ResourceManager::IsLoaded() {
    return mArchiveManager != nullptr && mArchiveManager->IsLoaded();
}

std::shared_ptr<File> ResourceManager::LoadFileProcess(const std::string& filePath) {
    auto file = mArchiveManager->LoadFile(filePath);
    if (file != nullptr) {
        SPDLOG_TRACE("Loaded File {} on ResourceManager", filePath);
    } else {
        SPDLOG_TRACE("Could not load File {} in ResourceManager", filePath);
    }
    return file;
}

std::shared_ptr<File> ResourceManager::LoadFileProcess(const ResourceIdentifier& identifier) {
    if (identifier.Parent == nullptr) {
        return LoadFileProcess(identifier.Path);
    }
    auto archive = identifier.Parent;
    auto file = archive->LoadFile(identifier.Path);
    if (file != nullptr) {
        SPDLOG_TRACE("Loaded File {} on ResourceManager", identifier.Path);
    } else {
        SPDLOG_TRACE("Could not load File {} in ResourceManager", identifier.Path);
    }
    return file;
}

std::shared_ptr<IResource> ResourceManager::LoadResourceProcess(const ResourceIdentifier& identifier, bool loadExact,
                                                                std::shared_ptr<ResourceInitData> initData) {
    // Check for and remove the OTR signature
    if (OtrSignatureCheck(identifier.Path.c_str())) {
        const auto newFilePath = identifier.Path.substr(7);
        return LoadResourceProcess({ newFilePath, identifier.Owner, identifier.Parent }, false, initData);
    }

    // Cache the starts_with check to avoid repeated string comparisons
    const bool isAltPath = identifier.Path.starts_with(IResource::gAltAssetPrefix);
    const bool shouldCheckAlt = !loadExact && mAltAssetsEnabled && !isAltPath;

    // Attempt to load the alternate version of the asset, if we fail then we continue trying to load the standard
    // asset.
    if (shouldCheckAlt) {
        std::string altPath = IResource::gAltAssetPrefix;
        altPath += identifier.Path;
        auto altResource =
            LoadResourceProcess({ std::move(altPath), identifier.Owner, identifier.Parent }, loadExact, initData);

        if (altResource != nullptr) {
            return altResource;
        }
    }

    // While waiting in the queue, another thread could have loaded the resource.
    // In a last attempt to avoid doing work that will be discarded, let's check if the cached version exists.
    auto cacheLine = CheckCache(identifier, loadExact);
    auto cachedResource = GetCachedResource(cacheLine);
    if (cachedResource != nullptr) {
        return cachedResource;
    }

    // Check for resource load errors which can indicate an alternate asset.
    // If we are attempting to load an alternate asset, we can return null
    if (!loadExact && mAltAssetsEnabled && isAltPath) {
        if (std::holds_alternative<ResourceLoadError>(cacheLine)) {
            try {
                // If we have attempted to cache an alternate asset, but failed, we return nullptr and rely on the
                // calling function to return a regular asset. If we have NOT attempted load already, attempt the load.
                auto loadError = std::get<ResourceLoadError>(cacheLine);
                if (loadError != ResourceLoadError::NotCached) {
                    return nullptr;
                }
            } catch (std::bad_variant_access const& e) {
                // This should never happen. The holds_alternative check above should prevent it.
                SPDLOG_ERROR("Unexpected bad_variant_access in LoadResourceProcess: {}", e.what());
            }
        }
    }

    // Get the file from the OTR. It may be null when the resource exists only as a `.meta`
    // alias (no real file at this path); fall through so the loader can resolve the alias,
    // but only when a `.meta` for this path actually exists.
#ifdef __3DS__
    // SoH-3DS: LoadFileProcess runs BEFORE ResourceLoader::LoadResource's own
    // catch, and any archive implementation can throw here (hardware
    // throw-tracer caught a bad_alloc in O2rArchive's buffer allocation at
    // the heap ceiling). One catch at this boundary covers every archive
    // source - including the ".meta" string concat below, which also
    // allocates. Allocation-free handler; failure is cached like NotFound.
    std::shared_ptr<File> file = nullptr;
    bool missingWithoutMeta = false;
    try {
        file = LoadFileProcess(identifier.Path);
        missingWithoutMeta = file == nullptr && !mArchiveManager->HasFile(identifier.Path + ".meta");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "resource: file load threw for %s: %s\n", identifier.Path.c_str(), e.what());
        // Transient failure (heap ceiling): return null WITHOUT the negative
        // cache below - once memory is reclaimed (eviction, scene change) a
        // retry of this same path must be allowed to succeed. Only a true
        // absent file earns the permanent NotFound cache line.
        return nullptr;
    }
    if (missingWithoutMeta) {
        SPDLOG_TRACE("Failed to load resource file at path {}", identifier.Path);
        // SoH-3DS: mResourceCache mutation MUST hold mMutex - the core-2 audio
        // thread loads synchronously while the game thread runs CheckCache and
        // the scene-eviction UnloadResources; an unlocked insert can rehash
        // under a concurrent locked find (heap corruption class).
        const std::lock_guard<std::mutex> lock(mMutex);
        mResourceCache[identifier] = ResourceLoadError::NotFound;
        return nullptr;
    }
#else
    auto file = LoadFileProcess(identifier.Path);
    if (file == nullptr && !mArchiveManager->HasFile(identifier.Path + ".meta")) {
        SPDLOG_TRACE("Failed to load resource file at path {}", identifier.Path);
        // SoH-3DS: locked for the same reason as the 3DS branch above.
        const std::lock_guard<std::mutex> lock(mMutex);
        mResourceCache[identifier] = ResourceLoadError::NotFound;
        return nullptr;
    }
#endif

    // Transform the raw data into a resource
    auto resource = GetResourceLoader()->LoadResource(identifier.Path, file, initData);

    // Another thread could have loaded the resource while we were processing, so we want to check before setting to
    // the cache.
    cachedResource = GetCachedResource(identifier, true);

    {
        const std::lock_guard<std::mutex> lock(mMutex);

        if (cachedResource != nullptr) {
            // If another thread has already loaded this resource, discard the work we already did and return from
            // cache.
            resource = cachedResource;
        }

        // Set the cache to the loaded resource
        if (resource != nullptr) {
            mResourceCache[identifier] = resource;
        } else {
            mResourceCache[identifier] = ResourceLoadError::NotFound;
        }
    }

    if (resource != nullptr) {
        SPDLOG_TRACE("Loaded Resource {} on ResourceManager", identifier.Path);
    } else {
        SPDLOG_TRACE("Resource load FAILED {} on ResourceManager", identifier.Path);
    }

    return resource;
}

std::shared_ptr<IResource> ResourceManager::LoadResourceProcess(const std::string& filePath, bool loadExact,
                                                                std::shared_ptr<ResourceInitData> initData) {
    return LoadResourceProcess({ filePath, mDefaultCacheOwner, mDefaultCacheArchive }, loadExact, initData);
}

std::shared_future<std::shared_ptr<IResource>>
ResourceManager::LoadResourceAsync(const ResourceIdentifier& identifier, bool loadExact, BS::priority_t priority,
                                   std::shared_ptr<ResourceInitData> initData) {
    // Check for and remove the OTR signature
    if (OtrSignatureCheck(identifier.Path.c_str())) {
        auto newFilePath = identifier.Path.substr(7);
        return LoadResourceAsync({ newFilePath, identifier.Owner, identifier.Parent }, loadExact, priority);
    }

    // Check the cache before queueing the job.
    auto cacheCheck = GetCachedResource(identifier, loadExact);
    if (cacheCheck) {
        auto promise = std::make_shared<std::promise<std::shared_ptr<IResource>>>();
        promise->set_value(cacheCheck);
        return promise->get_future().share();
    }

    return mThreadPool->submit_task(
        [this, identifier, loadExact, initData]() -> std::shared_ptr<IResource> {
            return LoadResourceProcess(identifier, loadExact, initData);
        },
        priority);
}

std::shared_future<std::shared_ptr<IResource>>
ResourceManager::LoadResourceAsync(const std::string& filePath, bool loadExact, BS::priority_t priority,
                                   std::shared_ptr<ResourceInitData> initData) {
    return LoadResourceAsync({ filePath, mDefaultCacheOwner, mDefaultCacheArchive }, loadExact, priority, initData);
}

std::shared_ptr<IResource> ResourceManager::LoadResource(const ResourceIdentifier& identifier, bool loadExact,
                                                         std::shared_ptr<ResourceInitData> initData) {
#ifdef __3DS__
    // SoH-3DS: run the load on the calling thread instead of submitting it to the
    // pool and blocking on the future.
    //
    // threadCount is max(1, hardware_concurrency() - reserved - 1), which on a
    // 2-core handheld is 1. A single worker deadlocks the moment a load nested
    // inside LoadResourceProcess calls LoadResource again: the only worker is
    // already blocked in .get() waiting for a task that only it could run.
    // That is exactly how startup stalled - in the first resource patch, with no
    // fault and no memory pressure.
    //
    // The pool also buys little here. Loads are SD-card bound, the archive index
    // is already in memory, and both usable cores are wanted for the game and
    // audio. Asynchronous callers still get the pool via LoadResourceAsync.
    if (OtrSignatureCheck(identifier.Path.c_str())) {
        return LoadResource({ identifier.Path.substr(7), identifier.Owner, identifier.Parent }, loadExact, initData);
    }
    if (auto cached = GetCachedResource(identifier, loadExact)) {
        return cached;
    }
    auto resource = LoadResourceProcess(identifier, loadExact, initData);
#else
    auto resource = LoadResourceAsync(identifier, loadExact, BS::pr::highest, initData).get();
#endif
    if (resource == nullptr) {
        SPDLOG_TRACE("Failed to load resource file at path {}", identifier.Path);
    }
    return resource;
}

std::shared_ptr<IResource> ResourceManager::LoadResource(const std::string& filePath, bool loadExact,
                                                         std::shared_ptr<ResourceInitData> initData) {
    return LoadResource({ filePath, mDefaultCacheOwner, mDefaultCacheArchive }, loadExact, initData);
}

std::shared_ptr<IResource> ResourceManager::LoadResource(uint64_t crc, bool loadExact,
                                                         std::shared_ptr<ResourceInitData> initData) {
    const std::string* hashStr = GetArchiveManager()->HashToString(crc);
    if (hashStr == nullptr || hashStr->length() == 0) {
        SPDLOG_TRACE("ResourceLoad: Unknown crc {}\n", crc);
        return nullptr;
    }

    return LoadResource(*hashStr, loadExact, initData);
}

std::variant<ResourceManager::ResourceLoadError, std::shared_ptr<IResource>>
ResourceManager::CheckCache(const ResourceIdentifier& identifier, bool loadExact) {
    if (!loadExact && mAltAssetsEnabled && !identifier.Path.starts_with(IResource::gAltAssetPrefix)) {
        const auto altPath = IResource::gAltAssetPrefix + identifier.Path;
        auto altCacheResult = CheckCache({ altPath, identifier.Owner, identifier.Parent }, loadExact);

        // If the type held at this cache index is a resource, then we return it.
        // Else we attempt to load standard definition assets.
        if (std::holds_alternative<std::shared_ptr<IResource>>(altCacheResult)) {
            return altCacheResult;
        }
    }

    const std::lock_guard<std::mutex> lock(mMutex);

    auto cacheFind = mResourceCache.find(identifier);
    if (cacheFind == mResourceCache.end()) {
        return ResourceLoadError::NotCached;
    }

    return cacheFind->second;
}

std::variant<ResourceManager::ResourceLoadError, std::shared_ptr<IResource>>
ResourceManager::CheckCache(const std::string& filePath, bool loadExact) {
    return CheckCache({ filePath, mDefaultCacheOwner, mDefaultCacheArchive }, loadExact);
}

std::shared_ptr<IResource> ResourceManager::GetCachedResource(const ResourceIdentifier& identifier, bool loadExact) {
    // Gets the cached resource based on filePath.
    return GetCachedResource(CheckCache(identifier, loadExact));
}

std::shared_ptr<IResource> ResourceManager::GetCachedResource(const std::string& filePath, bool loadExact) {
    // Gets the cached resource based on filePath.
    return GetCachedResource({ filePath, mDefaultCacheOwner, mDefaultCacheArchive }, loadExact);
}

std::shared_ptr<IResource>
ResourceManager::GetCachedResource(std::variant<ResourceLoadError, std::shared_ptr<IResource>> cacheLine) {
    // Gets the cached resource based on a cache line std::variant from the cache map.
    if (std::holds_alternative<std::shared_ptr<IResource>>(cacheLine)) {
        try {
            auto resource = std::get<std::shared_ptr<IResource>>(cacheLine);

            if (resource.use_count() <= 0) {
                return nullptr;
            }

            if (resource->IsDirty()) {
                return nullptr;
            }

            return resource;
        } catch (std::bad_variant_access const& e) {
            // This should never happen. The holds_alternative check above should prevent it.
            SPDLOG_ERROR("Unexpected bad_variant_access in GetCachedResource: {}", e.what());
        }
    }

    return nullptr;
}

std::shared_ptr<std::vector<std::shared_ptr<IResource>>>
ResourceManager::LoadResourcesProcess(const ResourceFilter& filter) {
    auto loadedList = std::make_shared<std::vector<std::shared_ptr<IResource>>>();
    auto fileList = GetArchiveManager()->ListFiles(filter.IncludeMasks, filter.ExcludeMasks);
    loadedList->reserve(fileList->size());

    for (size_t i = 0; i < fileList->size(); i++) {
        auto fileName = std::string(fileList->operator[](i));
        auto resource = LoadResource({ fileName, filter.Owner, filter.Parent });
        loadedList->push_back(resource);
    }

    return loadedList;
}

std::shared_future<std::shared_ptr<std::vector<std::shared_ptr<IResource>>>>
ResourceManager::LoadResourcesAsync(const ResourceFilter& filter, BS::priority_t priority) {
    return mThreadPool->submit_task(
        [this, filter]() -> std::shared_ptr<std::vector<std::shared_ptr<IResource>>> {
            return LoadResourcesProcess(filter);
        },
        priority);
}

std::shared_future<std::shared_ptr<std::vector<std::shared_ptr<IResource>>>>
ResourceManager::LoadResourcesAsync(const std::string& searchMask, BS::priority_t priority) {
    return LoadResourcesAsync({ { searchMask }, {}, mDefaultCacheOwner, mDefaultCacheArchive }, priority);
}

std::shared_ptr<std::vector<std::shared_ptr<IResource>>> ResourceManager::LoadResources(const std::string& searchMask) {
    return LoadResources({ { searchMask }, {}, mDefaultCacheOwner, mDefaultCacheArchive });
}

std::shared_ptr<std::vector<std::shared_ptr<IResource>>>
ResourceManager::LoadResources(const ResourceFilter& filter) {
    return LoadResourcesAsync(filter, BS::pr::highest).get();
}

void ResourceManager::DirtyResources(const ResourceFilter& filter) {
    mThreadPool->submit_task([this, filter]() -> void {
        auto list = GetArchiveManager()->ListFiles(filter.IncludeMasks, filter.ExcludeMasks);

        for (const auto& key : *list.get()) {
            auto resource = GetCachedResource({ key, filter.Owner, filter.Parent });
            // If it's a resource, we will set the dirty flag, else we will just unload it.
            if (resource != nullptr) {
                resource->Dirty();
            } else {
                UnloadResource({ key, filter.Owner, filter.Parent });
            }
        }
    });
}

void ResourceManager::DirtyResources(const std::string& searchMask) {
    DirtyResources({ { searchMask }, {}, mDefaultCacheOwner, mDefaultCacheArchive });
}

void ResourceManager::UnloadResourcesAsync(const std::string& searchMask, BS::priority_t priority) {
    UnloadResourcesAsync({ { searchMask }, {}, mDefaultCacheOwner, mDefaultCacheArchive }, priority);
}

void ResourceManager::UnloadResourcesAsync(const ResourceFilter& filter, BS::priority_t priority) {
    mThreadPool->submit_task([this, filter]() -> void { UnloadResourcesProcess(filter); }, priority);
}

void ResourceManager::UnloadResources(const std::string& searchMask) {
    UnloadResources({ { searchMask }, {}, mDefaultCacheOwner, mDefaultCacheArchive });
}

void ResourceManager::UnloadResources(const ResourceFilter& filter) {
    UnloadResourcesProcess(filter);
}

void ResourceManager::UnloadResourcesProcess(const ResourceFilter& filter) {
    auto list = GetArchiveManager()->ListFiles(filter.IncludeMasks, filter.ExcludeMasks);

    for (const auto& key : *list.get()) {
        UnloadResource({ key, mDefaultCacheOwner, mDefaultCacheArchive });

        // A `.meta` alias resource is cached under its base path, which is not itself a listed
        // file. Evict it too so it can't survive stale after its target/dependencies are unloaded.
        if (key.ends_with(".meta")) {
            UnloadResource({ key.substr(0, key.size() - 5), mDefaultCacheOwner, mDefaultCacheArchive });
        }
    }
}

std::shared_ptr<ArchiveManager> ResourceManager::GetArchiveManager() {
    return mArchiveManager;
}

std::shared_ptr<ResourceLoader> ResourceManager::GetResourceLoader() {
    return mResourceLoader;
}

size_t ResourceManager::UnloadResource(const ResourceIdentifier& identifier) {
    // Hold the cache line's value across the erase so the resource is NOT
    // destructed while mMutex is held (a destructor that re-enters the
    // ResourceManager would self-deadlock a non-recursive mutex). The old
    // code declared this intent but never assigned `value`; it also ran
    // contains() before taking the lock (rehash race against concurrent
    // inserts). Lookup, move-out, and erase all happen under the lock;
    // destruction happens after it is released.
    std::variant<ResourceLoadError, std::shared_ptr<IResource>> value = nullptr;
    size_t ret = 0;
    {
        const std::lock_guard<std::mutex> lock(mMutex);
        auto it = mResourceCache.find(identifier);
        if (it == mResourceCache.end()) {
            return ret;
        }
        value = std::move(it->second);
        mResourceCache.erase(it);
        ret = 1;
    }
    // `value` releases its reference here, outside the lock.
    return ret;
}

size_t ResourceManager::UnloadResource(const std::string& filePath) {
    return UnloadResource({ filePath, mDefaultCacheOwner, mDefaultCacheArchive });
}

void ResourceManager::CacheExternalResource(const std::string& filePath, std::shared_ptr<IResource> resource) {
    const std::lock_guard<std::mutex> lock(mMutex);
    mResourceCache[{ filePath, mDefaultCacheOwner, mDefaultCacheArchive }] = resource;
}

bool ResourceManager::WriteResource(const ResourceIdentifier& identifier, const std::vector<uint8_t>& data,
                                    bool unloadFile) {
    std::shared_ptr<Archive> archive = identifier.Parent;

    if (!archive) {
        archive = mArchiveManager->GetArchiveFromFile(identifier.Path);
    }

    if (!archive) {
        return false;
    }

    if (!mArchiveManager->WriteFile(archive, identifier.Path, data)) {
        return false;
    }

    if (unloadFile) {
        UnloadResource(identifier);
    }

    return true;
}

bool ResourceManager::OtrSignatureCheck(const char* fileName) {
    static const char* sOtrSignature = "__OTR__";
    return strncmp(fileName, sOtrSignature, strlen(sOtrSignature)) == 0;
}

#ifdef __3DS__
// SoH-3DS: read by the renderer heartbeat (res=live/free) so heap creep across
// a long session can be attributed to the resource cache rather than guessed
// at. `free` counts cache lines that nothing else holds a reference to - the
// cache is the sole owner, so those are the ones an eviction could actually
// reclaim. Game code keeps raw pointers into resource payloads (display lists
// patched with texture addresses), so a line with use_count > 1 must never be
// dropped.
extern "C" unsigned Soh3dsResourceCacheSize(unsigned* unreferenced) {
    auto* context = Context::GetRawInstance();
    if (context == nullptr) {
        return 0;
    }
    auto manager = context->GetResourceManager();
    if (manager == nullptr) {
        return 0;
    }
    return (unsigned)manager->Soh3dsCacheStats(unreferenced);
}

// Per-prefix breakdown, printed at the scene-eviction seam (once per scene
// transition, not per frame - it walks every cache line and formats strings).
extern "C" void Soh3dsResourceCacheReport(char* out, unsigned size) {
    if (out == nullptr || size == 0) {
        return;
    }
    out[0] = '\0';
    auto* context = Context::GetRawInstance();
    if (context == nullptr) {
        return;
    }
    auto manager = context->GetResourceManager();
    if (manager != nullptr) {
        manager->Soh3dsCacheReport(out, size);
    }
}
#endif

size_t ResourceManager::GetResourceCacheSize() {
    const std::lock_guard<std::mutex> lock(mMutex);
    return mResourceCache.size();
}

#ifdef __3DS__
size_t ResourceManager::Soh3dsCacheStats(unsigned* unreferenced) {
    const std::lock_guard<std::mutex> lock(mMutex);
    if (unreferenced != nullptr) {
        unsigned free = 0;
        for (const auto& [identifier, line] : mResourceCache) {
            auto* resource = std::get_if<std::shared_ptr<IResource>>(&line);
            // use_count 1 == this cache line is the only owner.
            if (resource != nullptr && *resource != nullptr && resource->use_count() == 1) {
                ++free;
            }
        }
        *unreferenced = free;
    }
    return mResourceCache.size();
}

std::vector<std::string> ResourceManager::Soh3dsCachedSubdirs(const std::string& prefix) {
    std::vector<std::string> dirs;
    const std::lock_guard<std::mutex> lock(mMutex);
    dirs.reserve(64);
    for (const auto& [identifier, line] : mResourceCache) {
        const std::string& path = identifier.Path;
        if (!path.starts_with(prefix)) {
            continue;
        }
        const size_t start = prefix.size();
        const size_t slash = path.find('/', start);
        if (slash == std::string::npos || slash == start) {
            continue;
        }
        std::string dir = path.substr(start, slash - start);
        if (std::find(dirs.begin(), dirs.end(), dir) == dirs.end()) {
            dirs.push_back(std::move(dir));
        }
    }
    return dirs;
}

void ResourceManager::Soh3dsCacheReport(char* out, unsigned size) {
    // Bucket by the first path segment (objects/, scenes/, textures/, ...),
    // counting total and sole-owned lines per bucket. Fixed table, no
    // allocation: the archives have ~20 top-level directories.
    struct Bucket {
        std::string_view name;
        unsigned total;
        unsigned free;
    };
    std::array<Bucket, 24> buckets{};
    size_t used = 0;

    {
        const std::lock_guard<std::mutex> lock(mMutex);
        for (const auto& [identifier, line] : mResourceCache) {
            const std::string_view path = identifier.Path;
            const size_t slash = path.find('/');
            const std::string_view prefix = slash == std::string_view::npos ? path : path.substr(0, slash);
            auto* resource = std::get_if<std::shared_ptr<IResource>>(&line);
            const bool isFree = resource != nullptr && *resource != nullptr && resource->use_count() == 1;

            size_t slot = 0;
            for (; slot < used; ++slot) {
                if (buckets[slot].name == prefix) {
                    break;
                }
            }
            if (slot == used) {
                if (used == buckets.size()) {
                    continue;
                }
                buckets[used++] = { prefix, 0, 0 };
            }
            ++buckets[slot].total;
            buckets[slot].free += isFree ? 1u : 0u;
        }
    }

    // Largest buckets first, then format as many as fit.
    std::sort(buckets.begin(), buckets.begin() + used,
              [](const Bucket& a, const Bucket& b) { return a.total > b.total; });
    unsigned offset = 0;
    for (size_t i = 0; i < used && offset + 1 < size; ++i) {
        const int written = std::snprintf(out + offset, size - offset, "%s%.*s=%u/%u", i == 0 ? "" : " ",
                                          (int)buckets[i].name.size(), buckets[i].name.data(), buckets[i].total,
                                          buckets[i].free);
        if (written <= 0 || (unsigned)written >= size - offset) {
            break;
        }
        offset += (unsigned)written;
    }
}
#endif

bool ResourceManager::IsAltAssetsEnabled() {
    return mAltAssetsEnabled;
}

void ResourceManager::SetAltAssetsEnabled(bool isEnabled) {
    mAltAssetsEnabled = isEnabled;
}

size_t ResourceManager::GetResourceSize(std::shared_ptr<IResource> resource) {
    if (resource == nullptr) {
        return 0;
    }

    return resource->GetPointerSize();
}

size_t ResourceManager::GetResourceSize(const char* name) {
    auto resource = LoadResource(name);

    return GetResourceSize(resource);
}

size_t ResourceManager::GetResourceSize(uint64_t crc) {
    auto resource = LoadResource(crc);

    return GetResourceSize(resource);
}

bool ResourceManager::GetResourceIsCustom(std::shared_ptr<IResource> resource) {
    if (resource == nullptr) {
        return false;
    }

    return resource->GetInitData()->IsCustom;
}

bool ResourceManager::GetResourceIsCustom(const char* name) {
    auto resource = LoadResource(name);

    return GetResourceIsCustom(resource);
}

bool ResourceManager::GetResourceIsCustom(uint64_t crc) {
    auto resource = LoadResource(crc);

    return GetResourceIsCustom(resource);
}

void* ResourceManager::GetResourceRawPointer(std::shared_ptr<IResource> resource) {
    if (resource == nullptr) {
        return nullptr;
    }

    return resource->GetRawPointer();
}

void* ResourceManager::GetResourceRawPointer(const char* name) {
    auto resource = LoadResource(name);

    return GetResourceRawPointer(resource);
}

void* ResourceManager::GetResourceRawPointer(uint64_t crc) {
    auto resource = LoadResource(crc);

    return GetResourceRawPointer(resource);
}

} // namespace Ship
