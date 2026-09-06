#pragma once
#include "BinaryWrapper.h"
#include <cstdio>

// ZIP32 writer: payloads and central-directory records are streamed to disk.
// Duplicate names are intentional and retain their original occurrence order.
class StoredZip final : public BinaryWrapper {
public:
    explicit StoredZip(const std::string& path);
    ~StoredZip() override;
    int32_t CreateArchive() override;
    bool AddFile(const std::string& path, std::vector<char> data) override;
    int32_t Close() override;
private:
    void Cleanup() noexcept;
    bool ownsArchive = false;
    bool ownsDirectory = false;
    bool finalized = false;
    FILE* archive = nullptr;
    FILE* directory = nullptr;
    uint64_t offset = 0;
    uint64_t directorySize = 0;
    uint32_t count = 0;
    std::string directoryPath;
};
