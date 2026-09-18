#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <vector>

// Optional executable-provided reader. The archive owns the handle and holds
// its read mutex across every callback. Normal builds keep the libzip reader.
struct Soh3dsCompactZipApi {
    void* (*open)(const char* path);
    void (*close)(void* handle);
    size_t (*count)(void* handle);
    std::string (*name)(void* handle, size_t index);
    // Non-allocating variant. ListFiles walks every entry in the archive
    // (39,057 for a vanilla oot.o2r) and previously built a std::string for
    // each one just to glob it, discarding the non-matches. That burst of
    // ~39k short-lived 33-64 byte allocations per call is what shatters the
    // free list into ~50,000 fragments averaging 30 bytes. Writes a NUL
    // terminated name into caller storage and returns its length excluding the
    // terminator, or 0 if the entry is missing or does not fit.
    size_t (*nameInto)(void* handle, size_t index, char* out, size_t capacity);
    std::shared_ptr<std::vector<char>> (*read)(void* handle, const std::string& path);
    size_t (*prefetchRoom)(void* handle, const std::string& prefix, size_t budget);
    uint64_t (*prefetchHits)(void* handle);
};

extern "C" const Soh3dsCompactZipApi* Soh3dsGetCompactZipApi() __attribute__((weak));
