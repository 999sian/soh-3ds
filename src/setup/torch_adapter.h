#pragma once
#include <cstddef>
#include <string>
namespace Soh3dsSetup {
using Progress = bool (*)(const char* stage, size_t done, size_t total, void* user);
bool ExtractRom(const std::string& normalizedRom, const std::string& metadataDir,
                const std::string& stagingDir, const std::string& version,
                Progress progress, void* user, std::string& outputName, std::string& error);
}
