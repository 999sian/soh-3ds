#!/usr/bin/env python3
"""Exercise the production per-port gamepad cache and its invalidation points.

The cache is read once per frame by every SDL mapping object, and a stale entry means a
port silently sees no controller. Extract the real functions and drive them against a stub
that has the same members, so the invalidation contract is checked rather than assumed.
"""

from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get(
    "SOH_CONNECTED_DEVICE_SOURCE",
    ROOT / "third_party/libultraship/src/ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.cpp",
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


text = SOURCE.read_text()
bodies = "\n\n".join(
    extract_function(text, signature)
    for signature in (
        "ConnectedPhysicalDeviceManager::GetConnectedSDLGamepadsForPort(uint8_t portIndex)",
        "bool ConnectedPhysicalDeviceManager::PortIsIgnoringInstanceId(uint8_t portIndex, int32_t instanceId)",
        "void ConnectedPhysicalDeviceManager::IgnoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId)",
        "void ConnectedPhysicalDeviceManager::UnignoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId)",
    )
)
# The production definition returns a reference; keep the leading return type that precedes
# the extracted signature line.
bodies = "const std::unordered_map<int32_t, SDL_GameController*>&\n" + bodies

harness = r'''
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <unordered_map>
#include <unordered_set>

static size_t allocations = 0;
static bool countAllocations = false;
void* operator new(std::size_t size) {
    if (countAllocations) ++allocations;
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

struct SDL_GameController { int id; };

namespace Ship {
struct ConnectedPhysicalDeviceManager {
    const std::unordered_map<int32_t, SDL_GameController*>& GetConnectedSDLGamepadsForPort(uint8_t portIndex);
    bool PortIsIgnoringInstanceId(uint8_t portIndex, int32_t instanceId);
    void IgnoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId);
    void UnignoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId);

    std::unordered_map<int32_t, SDL_GameController*> mConnectedSDLGamepads;
    std::unordered_map<uint8_t, std::unordered_map<int32_t, SDL_GameController*>> mPortGamepadCache;
    std::unordered_map<uint8_t, std::unordered_set<int32_t>> mIgnoredInstanceIds;
};
'''

harness += bodies + r'''
} // namespace Ship

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

int main() {
    SDL_GameController padA{1};
    SDL_GameController padB{2};

    Ship::ConnectedPhysicalDeviceManager manager;
    manager.mConnectedSDLGamepads[10] = &padA;
    manager.mConnectedSDLGamepads[11] = &padB;

    // Port 0 ignores nothing, so it sees both pads.
    const auto& port0 = manager.GetConnectedSDLGamepadsForPort(0);
    require(port0.size() == 2, "port 0 did not see both connected pads");

    // A warmed read must be allocation free - that is the whole point of the cache.
    allocations = 0;
    countAllocations = true;
    const auto& again = manager.GetConnectedSDLGamepadsForPort(0);
    countAllocations = false;
    require(allocations == 0, "a warmed per-port read allocated");
    require(&again == &port0, "warmed read did not return the cached map");

    // Querying an unknown port must not silently register an ignore set for it.
    manager.GetConnectedSDLGamepadsForPort(3);
    require(manager.mIgnoredInstanceIds.find(3) == manager.mIgnoredInstanceIds.end(),
            "querying a port inserted an empty ignore set");

    // Ignoring a device must be visible immediately, not after the cache expires.
    manager.IgnoreInstanceIdForPort(0, 10);
    const auto& afterIgnore = manager.GetConnectedSDLGamepadsForPort(0);
    require(afterIgnore.size() == 1, "ignoring a device did not invalidate the port cache");
    require(afterIgnore.count(11) == 1, "ignore dropped the wrong device");

    // And un-ignoring it must bring it back.
    manager.UnignoreInstanceIdForPort(0, 10);
    require(manager.GetConnectedSDLGamepadsForPort(0).size() == 2,
            "un-ignoring a device did not invalidate the port cache");

    // An ignore aimed at another port must not disturb this one's contents.
    manager.IgnoreInstanceIdForPort(1, 10);
    require(manager.GetConnectedSDLGamepadsForPort(0).size() == 2, "another port's ignore changed port 0");
    require(manager.GetConnectedSDLGamepadsForPort(1).size() == 1, "port 1 ignore was not applied");

    // A device disconnect clears the cache in production via RefreshConnectedSDLGamepads;
    // model the same erase plus clear and confirm no stale pointer survives.
    manager.mPortGamepadCache.clear();
    manager.mConnectedSDLGamepads.erase(11);
    const auto& afterUnplug = manager.GetConnectedSDLGamepadsForPort(0);
    require(afterUnplug.size() == 1, "cache kept a disconnected device");
    require(afterUnplug.count(10) == 1, "wrong device survived the disconnect");

    std::cout << "connected gamepad cache: ok; warmed per-port reads allocate nothing\n";
}
'''

with tempfile.TemporaryDirectory(prefix="soh-gamepad-cache-") as temp:
    cpp = Path(temp) / "gamepad_cache_test.cpp"
    exe = Path(temp) / "gamepad_cache_test"
    cpp.write_text(harness)
    subprocess.run(["c++", "-std=c++20", "-O2", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
