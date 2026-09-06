#pragma once

#include <cerrno>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

// Console filesystems support rename to an absent destination, but sdmc cannot
// atomically overwrite one. Keep each complete generation until its successor
// has been closed and promoted. Callers serialize access to each slot.
namespace SaveFileTransaction {
struct Paths {
    std::string target, temporary, pending, backup;
    explicit Paths(const std::string& path)
        : target(path), temporary(path + ".temp"), pending(path + ".pending"), backup(path + ".backup") {}
};

// Distinguish a missing file from a filesystem error. Treating an unreadable
// card as an empty slot could otherwise discard recovery generations.
inline int Exists(const std::string& path) {
    struct stat info;
    if (::stat(path.c_str(), &info) == 0) return 1;
    return errno == ENOENT ? 0 : -1;
}

inline bool RemoveIfPresent(const std::string& path) {
    return std::remove(path.c_str()) == 0 || errno == ENOENT;
}

inline bool Recover(const Paths& paths) {
    const int target = Exists(paths.target);
    if (target != 0) return target == 1;
    const int backup = Exists(paths.backup);
    if (backup < 0) return false;
    if (backup == 1) return std::rename(paths.backup.c_str(), paths.target.c_str()) == 0;
    const int pending = Exists(paths.pending);
    if (pending < 0) return false;
    // A pending file is created only after a checked write, flush, sync and
    // close. An interrupted first save can be recovered; raw .temp cannot.
    return pending == 0 || std::rename(paths.pending.c_str(), paths.target.c_str()) == 0;
}

inline bool Recover(const std::string& path) { return Recover(Paths(path)); }

inline bool Prepare(const Paths& paths) {
    return Recover(paths) && RemoveIfPresent(paths.temporary) && RemoveIfPresent(paths.pending);
}

inline bool CloseWritten(FILE* file, bool complete) {
    if (complete && std::fflush(file) != 0) complete = false;
    if (complete && ::fsync(::fileno(file)) != 0) complete = false;
    if (std::fclose(file) != 0) complete = false;
    return complete;
}

inline bool Promote(const Paths& paths) {
    if (std::rename(paths.temporary.c_str(), paths.pending.c_str()) != 0) return false;
    const int target = Exists(paths.target);
    if (target < 0) return false;
    if (target == 1) {
        // The current slot and pending generation are both complete before
        // recycling an older backup. Never copy over a live slot.
        if (!RemoveIfPresent(paths.backup) ||
            std::rename(paths.target.c_str(), paths.backup.c_str()) != 0) return false;
    }
    // On failure leave BOTH the old backup and the completed pending save.
    // Startup restores the committed backup before considering the pending file.
    return std::rename(paths.pending.c_str(), paths.target.c_str()) == 0;
}

inline bool Write(const std::string& path, const std::string& data) {
    const Paths paths(path);
    if (!Prepare(paths)) return false;
    FILE* output = std::fopen(paths.temporary.c_str(), "wb");
    if (!output) return false;
    const bool complete = std::fwrite(data.data(), 1, data.size(), output) == data.size();
    return CloseWritten(output, complete) && Promote(paths);
}

inline bool Copy(const std::string& source, const std::string& destination) {
    if (source == destination) return Recover(source);
    const Paths paths(destination);
    if (!Recover(source) || !Prepare(paths)) return false;
    FILE* input = std::fopen(source.c_str(), "rb");
    if (!input) return false;
    FILE* output = std::fopen(paths.temporary.c_str(), "wb");
    if (!output) {
        std::fclose(input);
        return false;
    }
    alignas(0x40) unsigned char buffer[4096];
    bool complete = true;
    size_t size;
    while ((size = std::fread(buffer, 1, sizeof(buffer), input)) > 0) {
        if (std::fwrite(buffer, 1, size, output) != size) {
            complete = false;
            break;
        }
    }
    if (std::ferror(input)) complete = false;
    if (std::fclose(input) != 0) complete = false;
    return CloseWritten(output, complete) && Promote(paths);
}

inline bool Delete(const std::string& path) {
    const Paths paths(path);
    // Remove recoverable generations before the primary so an intentional
    // deletion cannot resurrect on restart. A failure retains the primary.
    return Recover(paths) && RemoveIfPresent(paths.temporary) && RemoveIfPresent(paths.pending) &&
           RemoveIfPresent(paths.backup) && RemoveIfPresent(paths.target);
}
} // namespace SaveFileTransaction
