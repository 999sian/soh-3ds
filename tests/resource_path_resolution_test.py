#!/usr/bin/env python3
"""Extract and exercise SoH's production MQ resource lookup wrapper."""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get(
    "SOH_RESOURCE_HELPERS_SOURCE",
    ROOT / "third_party/shipwright/soh/soh/ResourceManagerHelpers.cpp",
))


def extract_function(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError("unterminated production function")


body = extract_function(
    SOURCE.read_text(),
    "std::shared_ptr<Ship::IResource> ResourceMgr_GetResourceByNameHandlingMQ(const char* path)",
)
resolver_include = ""
if "ResourcePathResolver" in body:
    resolver_include = '#include "third_party/shipwright/soh/soh/ResourcePathResolver.h"\n'

harness = r'''
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <string_view>

static size_t allocations = 0;
static bool countAllocations = false;
void* operator new(std::size_t size) {
    if (countAllocations) ++allocations;
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace Ship {
struct IResource { explicit IResource(int value) : value(value) {} int value; };
struct ResourceIdentifier {
    // Mirrors libultraship: the identifier OWNS a std::string copy of the path,
    // so a by-value LoadResource that moves costs one allocation per lookup and
    // a const-ref one that copies costs two.
    explicit ResourceIdentifier(std::string&& path) : Path(std::move(path)) {}
    const std::string Path;
};
struct ResourceManager {
    bool alternate = false;
    std::string observed;
    std::shared_ptr<IResource> cached;
    bool cachedAlternate = false;
    std::string cachedKey;

    // Mirrors libultraship: the cache is probed from a view (no allocation, and the
    // __OTR__ prefix is stripped the same way), while a miss owns its path.
    std::shared_ptr<IResource> GetCachedResource(std::string_view path) {
        if (path.starts_with("__OTR__")) path.remove_prefix(7);
        if (cached && cachedKey == path) return cached;
        return nullptr;
    }
    std::shared_ptr<IResource> LoadResource(std::string path) {
        ResourceIdentifier identifier{ std::move(path) };
        observed = identifier.Path;
        std::string_view key = identifier.Path;
        if (key.starts_with("__OTR__")) key.remove_prefix(7);
        if (!cached) {
            cachedAlternate = alternate;
            cachedKey = key;
            cached = std::make_shared<IResource>(alternate ? 2 : 1);
        }
        return cached;
    }
    void UnloadResource() { cached.reset(); cachedKey.clear(); }
};
struct Context {
    static Context* GetRawInstance() { static Context value; return &value; }
    // Production hands back a shared_ptr the Context keeps owning; the lookup takes the raw
    // pointer out of it, so the mock must have the same shape or the test cannot catch a
    // per-lookup refcount copy creeping back in.
    const std::shared_ptr<ResourceManager>& GetResourceManager() { return manager; }
    std::shared_ptr<ResourceManager> manager = std::make_shared<ResourceManager>();
};
}

static bool masterQuest = false;
extern "C" unsigned ResourceMgr_IsGameMasterQuest() { return masterQuest; }
'''

harness = resolver_include + harness
harness += "\n" + body + r'''

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
    auto* manager = Ship::Context::GetRawInstance()->GetResourceManager().get();
    const char* original = "__OTR__scenes/shared/a_very_long_resource_name_that_exceeds_small_string_storage";

    masterQuest = false;
    ResourceMgr_GetResourceByNameHandlingMQ(original); // warm manager-owned storage
    allocations = 0;
    countAllocations = true;
    auto first = ResourceMgr_GetResourceByNameHandlingMQ(original);
    auto second = ResourceMgr_GetResourceByNameHandlingMQ(original);
    countAllocations = false;
    std::cout << "measured non-MQ allocations=" << allocations << '\n';
    require(first == second, "cache hit changed resource identity");
    require(manager->observed == original, "original path changed");
    require(allocations == 0, "a warmed non-MQ cache hit allocated - the view probe is not being used");

    manager->UnloadResource();
    masterQuest = true;
    const char* nonMq = "__OTR__scenes/nonmq/a_very_long_resource_name_that_exceeds_small_string_storage";
    auto mq = ResourceMgr_GetResourceByNameHandlingMQ(nonMq);
    require(manager->observed == "__OTR__scenes/mq/a_very_long_resource_name_that_exceeds_small_string_storage",
            "MQ lookup did not rewrite the first /nonmq/ component");
    require(mq->value == 1, "MQ rewrite changed selected original resource");
    allocations = 0;
    countAllocations = true;
    ResourceMgr_GetResourceByNameHandlingMQ(nonMq);
    countAllocations = false;
    require(allocations == 0, "a warmed MQ-rewritten cache hit allocated - the stack rewrite or probe is bypassed");

    manager->UnloadResource();
    const char* alreadyMq = "__OTR__scenes/mq/a_very_long_resource_name_that_exceeds_small_string_storage";
    ResourceMgr_GetResourceByNameHandlingMQ(alreadyMq); // warm manager resource and observed storage
    allocations = 0;
    countAllocations = true;
    ResourceMgr_GetResourceByNameHandlingMQ(alreadyMq);
    countAllocations = false;
    require(manager->observed == alreadyMq, "MQ mode changed a path without a /nonmq/ component");
    require(allocations == 0, "a warmed MQ-mode lookup with no /nonmq/ component allocated");

    // A path too long for the stack rewrite buffer must fall back to the owning
    // path and still rewrite correctly. masterQuest is still on here.
    manager->UnloadResource();
    std::string longTail(300, 'x');
    const std::string longNonMq = "__OTR__scenes/nonmq/" + longTail;
    const std::string longMq = "__OTR__scenes/mq/" + longTail;
    auto oversized = ResourceMgr_GetResourceByNameHandlingMQ(longNonMq.c_str());
    require(oversized != nullptr, "oversized MQ path did not resolve");
    require(manager->observed == longMq, "oversized MQ path was not rewritten by the fallback");

    manager->UnloadResource();
    ResourceMgr_GetResourceByNameHandlingMQ("__OTR__/nonmq_suffix/kept/nonmq/first/nonmq/second");
    require(manager->observed == "__OTR__/nonmq_suffix/kept/mq/first/nonmq/second",
            "MQ lookup did not preserve embedded text and rewrite only the first exact component");

    manager->UnloadResource();
    manager->alternate = true;
    auto alternate = ResourceMgr_GetResourceByNameHandlingMQ(nonMq);
    require(alternate->value == 2, "alternate selection did not follow manager state after unload");
    std::weak_ptr<Ship::IResource> released = alternate;
    alternate.reset();
    manager->UnloadResource();
    require(released.expired(), "lookup retained a resource after manager unload and caller release");

    manager->alternate = false;
    auto restored = ResourceMgr_GetResourceByNameHandlingMQ(nonMq);
    require(restored->value == 1, "original resource was not restored after alternate unload");
    std::cout << "resource path resolution: ok; warmed cache hits allocate nothing\n";
}
'''

with tempfile.TemporaryDirectory(prefix="soh-resource-path-") as temp:
    cpp = Path(temp) / "resource_path_test.cpp"
    exe = Path(temp) / "resource_path_test"
    cpp.write_text(harness)
    flags = shlex.split(os.environ.get("SOH_RESOURCE_TEST_FLAGS", "-O2"))
    subprocess.run(
        ["c++", "-std=c++20", *flags, "-I", str(ROOT), str(cpp), "-o", str(exe)],
        check=True,
    )
    subprocess.run([str(exe)], check=True)
