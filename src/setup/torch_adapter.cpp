#include "torch_adapter.h"
#include "Companion.h"
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace Soh3dsSetup {
bool ExtractRom(const std::string& normalizedRom, const std::string& metadataDir,
                const std::string& stagingDir, const std::string& version,
                Progress progress, void* user, std::string& outputName, std::string& error) {
    outputName.clear();
    error.clear();
    if (Companion::Instance) { error = "Another extraction is already running"; return false; }
    try {
        auto companion = std::make_unique<Companion>(std::filesystem::path(normalizedRom),
            ArchiveType::O2R, false, false, metadataDir, stagingDir);
        Companion::Instance = companion.get();
        companion->SetLowMemoryMode(true);
        companion->SetVersion(version);
        companion->SetSetupProgress([=](const char* stage, size_t done, size_t total) {
            return !progress || progress(stage, done, total, user);
        });
        companion->Init(ExportType::Binary);
        auto path = std::filesystem::path(companion->GetOutputPath());
        auto name = path.filename().string();
        if ((name != "oot.o2r" && name != "oot-mq.o2r") ||
            !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) < 22)
            throw std::runtime_error("Extractor produced no valid game archive");
        outputName = name;
        return true;
    } catch (const std::exception& e) { error = e.what(); }
      catch (...) { error = "Unknown extraction error"; }
    Companion::Instance = nullptr;
    return false;
}
}
