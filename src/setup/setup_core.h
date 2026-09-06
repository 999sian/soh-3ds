#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Soh3dsSetup {

using Progress = bool (*)(const char* stage, size_t done, size_t total, void* user);

struct RomMetadata {
    std::string sha1;
    std::string outputArchive;
    std::string displayName;
    std::string metadataVariant;
};

enum class ArchiveKind {
    Support,
    Game,
};

bool NormalizeRom(const std::string& source, const std::string& destination, Progress progress, void* user,
                  std::string& sha1, std::string& error);

bool LoadRomMetadata(const std::string& path, std::vector<RomMetadata>& rows, std::string& error);
const RomMetadata* FindRomMetadata(const std::vector<RomMetadata>& rows, const std::string& sha1);

bool CopyFile(const std::string& source, const std::string& destination, Progress progress, void* user,
              std::string& error);
bool AtomicInstall(const std::string& staged, const std::string& destination, std::string& error);
bool CleanupSetupArtifacts(const std::string& stagingDirectory, std::string& error);
bool HasDiskHeadroom(const std::string& directory, uint64_t requiredBytes, uint64_t reserveBytes,
                     uint64_t& availableBytes, std::string& error);

// A full validation reads every payload so libzip checks decompression and CRC.
// Quick validation still checks ZIP structure plus the entries needed at boot.
bool ValidateArchive(const std::string& path, const std::string& expectedPortVersion, ArchiveKind kind,
                     bool full, std::string& error, Progress progress = nullptr, void* user = nullptr);

} // namespace Soh3dsSetup
