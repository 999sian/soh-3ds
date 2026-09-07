#include "ship/resource/archive/O2rArchive.h"

#include "ship/Context.h"
#include "ship/window/Window.h"
#include "spdlog/spdlog.h"
#include <unordered_map>
#include <cstdio>

namespace Ship {
O2rArchive::O2rArchive(const std::string& archivePath) : Archive(archivePath) {
    mZipArchive = nullptr;
}

O2rArchive::~O2rArchive() {
    SPDLOG_TRACE("destruct o2rarchive: {}", GetPath());
    Close();
}

#ifdef __3DS__
static zip_t* OpenBufferedReadArchive(const std::string& path) {
    // SoH-3DS: libzip reads entries through stdio, and newlib sizes a FILE's
    // buffer from st_blksize, which the sdmc devoptab reports as 512 bytes -
    // so every stored entry came in as size/512 separate FS_ReadFile IPC
    // round trips (~100 us each): a 20 KiB texture was ~40 syscalls, a scene
    // load hundreds of thousands. Own the FILE, give it a 64 KiB buffer, and
    // hand it to libzip; zip_open_from_source takes ownership of the source
    // and the file is closed with the handle (ZIP_SOURCE_FREE -> fclose).
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return nullptr;
    }
    std::setvbuf(file, nullptr, _IOFBF, 64 * 1024);
    zip_source_t* source = zip_source_filep_create(file, 0, -1, nullptr);
    if (source == nullptr) {
        std::fclose(file);
        return nullptr;
    }
    zip_t* handle = zip_open_from_source(source, ZIP_RDONLY, nullptr);
    if (handle == nullptr) {
        zip_source_free(source); // also closes the FILE
    }
    return handle;
}
#endif

zip_t* O2rArchive::GetZipHandle() {
#ifdef __3DS__
    // The caller holds mReadMutex until the entry has been read and closed.
    // Each extra libzip handle duplicates the entire archive directory.
    return mZipArchive;
#else
    std::lock_guard<std::mutex> lock(mPoolMutex);
    if (!mZipArchivePool.empty()) {
        zip_t* handle = mZipArchivePool.back();
        mZipArchivePool.pop_back();
        return handle;
    }
    return zip_open(GetPath().c_str(), ZIP_RDONLY, nullptr);
#endif
}

void O2rArchive::ReleaseZipHandle(zip_t* handle) {
#ifndef __3DS__
    if (handle == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mPoolMutex);
    mZipArchivePool.push_back(handle);
#endif
}

std::shared_ptr<File> O2rArchive::LoadFile(uint64_t hash) {
    const std::string& filePath =
        *Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->HashToString(hash);
    return LoadFile(filePath);
}

std::shared_ptr<File> O2rArchive::LoadFile(const std::string& filePath) {
#ifdef __3DS__
    const std::lock_guard<std::mutex> readLock(mReadMutex);
#endif
    zip_t* zipArchive = GetZipHandle();
    if (zipArchive == nullptr) {
        SPDLOG_TRACE("Failed to open file {} from zip archive {}. Archive not open.", filePath, GetPath());
        return nullptr;
    }

    auto zipEntryIndex = zip_name_locate(zipArchive, filePath.c_str(), 0);
    if (zipEntryIndex < 0) {
        SPDLOG_TRACE("Failed to find file {} in zip archive  {}.", filePath, GetPath());
        ReleaseZipHandle(zipArchive);
        return nullptr;
    }

    struct zip_stat zipEntryStat;
    zip_stat_init(&zipEntryStat);
    if (zip_stat_index(zipArchive, zipEntryIndex, 0, &zipEntryStat) != 0) {
        SPDLOG_TRACE("Failed to get entry information for file {} in zip archive  {}.", filePath, GetPath());
        ReleaseZipHandle(zipArchive);
        return nullptr;
    }

    // Filesize 0, no logging needed
    if (zipEntryStat.size == 0) {
        SPDLOG_TRACE("Failed to load file {}; filesize 0", filePath, GetPath());
        ReleaseZipHandle(zipArchive);
        return nullptr;
    }

    struct zip_file* zipEntryFile = zip_fopen_index(zipArchive, zipEntryIndex, 0);
    if (!zipEntryFile) {
        SPDLOG_TRACE("Failed to open file {} in zip archive  {}.", filePath, GetPath());
        ReleaseZipHandle(zipArchive);
        return nullptr;
    }

    auto fileToLoad = std::make_shared<File>();
#ifdef __3DS__
    // SoH-3DS: this buffer allocation is the file-load path's OOM throw site
    // (hardware throw-tracer session: __cxa_throw <- operator new <- here <-
    // ArchiveManager::LoadFile <- ResourceManager::LoadFileProcess) and it
    // runs OUTSIDE ResourceLoader::LoadResource's catch. Degrade to a null
    // file - callers already handle "file failed to load" - instead of
    // std::terminate. Allocation-free handler.
    try {
        fileToLoad->Buffer = std::make_shared<std::vector<char>>(zipEntryStat.size);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "resource: file buffer alloc failed (%lu bytes): %s\n",
                     (unsigned long)zipEntryStat.size, e.what());
        zip_fclose(zipEntryFile);
        ReleaseZipHandle(zipArchive);
        return nullptr;
    }
#else
    fileToLoad->Buffer = std::make_shared<std::vector<char>>(zipEntryStat.size);
#endif

    if (zip_fread(zipEntryFile, fileToLoad->Buffer->data(), zipEntryStat.size) < 0) {
        SPDLOG_TRACE("Error reading file {} in zip archive  {}.", filePath, GetPath());
    }

    if (zip_fclose(zipEntryFile) != 0) {
        SPDLOG_TRACE("Error closing file {} in zip archive  {}.", filePath, GetPath());
    }

    ReleaseZipHandle(zipArchive);

    fileToLoad->IsLoaded = true;

    return fileToLoad;
}

bool O2rArchive::Open() {
#ifdef __3DS__
    const std::lock_guard<std::mutex> readLock(mReadMutex);
    mZipArchive = OpenBufferedReadArchive(GetPath());
#else
    mZipArchive = zip_open(GetPath().c_str(), ZIP_CREATE, nullptr);
#endif
    if (mZipArchive == nullptr) {
        SPDLOG_ERROR("Failed to load zip file \"{}\"", GetPath());
        return false;
    }

    auto zipNumEntries = zip_get_num_entries(mZipArchive, 0);
    // Pre-size the index for the known entry count so a ~40k-entry OoT archive
    // does not rehash the hash->path map repeatedly while indexing.
    if (zipNumEntries > 0) {
        ReserveIndex(static_cast<size_t>(zipNumEntries));
    }
    for (auto i = 0; i < zipNumEntries; i++) {
        auto zipEntryName = zip_get_name(mZipArchive, i, 0);

        // It is possible for directories to have entries in a zip
        // file, we don't want those indexed as files in the archive
        if (zipEntryName[strlen(zipEntryName) - 1] == '/') {
            continue;
        }

        IndexFile(zipEntryName);
    }

    return true;
}

bool O2rArchive::Close() {
#ifdef __3DS__
    const std::lock_guard<std::mutex> readLock(mReadMutex);
#endif
    bool success = true;

    if (mZipArchive != nullptr) {
        if (zip_close(mZipArchive) == -1) {
            SPDLOG_ERROR("Failed to close zip file \"{}\"", GetPath());
            success = false;
        }
        mZipArchive = nullptr;
    }

    std::lock_guard<std::mutex> lock(mPoolMutex);
    for (auto* handle : mZipArchivePool) {
        if (zip_close(handle) == -1) {
            SPDLOG_ERROR("Failed to close pooled zip file \"{}\"", GetPath());
            success = false;
        }
    }
    mZipArchivePool.clear();

    return success;
}

bool O2rArchive::WriteFile(const std::string& filePath, const std::vector<uint8_t>& data) {
#ifdef __3DS__
    const std::lock_guard<std::mutex> readLock(mReadMutex);
#endif
    if (!mZipArchive) {
        SPDLOG_ERROR("Cannot write to zip: Archive is not open.");
        return false;
    }

#ifdef __3DS__
    // Reads use a buffered, read-only source. Replace it under the same lock
    // for writes, so readers never see a stale or concurrently modified ZIP.
    zip_discard(mZipArchive);
    mZipArchive = zip_open(GetPath().c_str(), ZIP_CREATE, nullptr);
    if (mZipArchive == nullptr) {
        return false;
    }
#endif

    // Create a new zip source from the data buffer
    zip_source_t* source = zip_source_buffer(mZipArchive, data.data(), data.size(), 0);
    if (!source) {
        SPDLOG_ERROR("Failed to create zip source for file \"{}\"", filePath);
        return false;
    }

    // Add or replace the file in the zip archive
    if (zip_file_add(mZipArchive, filePath.c_str(), source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0) {
        SPDLOG_ERROR("Failed to add file \"{}\" to ZIP", filePath);
        zip_source_free(source);
        return false;
    }

    // Save changes to disk
    if (zip_close(mZipArchive) < 0) {
        zip_error_t* error = zip_get_error(mZipArchive);
        SPDLOG_ERROR("Failed to save changes to zip archive: {} ({})", zip_error_strerror(error),
                     zip_error_code_zip(error));
        zip_discard(mZipArchive); // Close zip and discard changes
        mZipArchive = nullptr;
        return false;
    }

    // Clear the pool as the file on disk has likely changed
    {
        std::lock_guard<std::mutex> lock(mPoolMutex);
        for (auto* handle : mZipArchivePool) {
            zip_close(handle);
        }
        mZipArchivePool.clear();
    }

    SPDLOG_INFO("Successfully wrote file: {}", filePath);

    // Reopen after committing; 3DS returns to buffered, shared reads.
#ifdef __3DS__
    mZipArchive = OpenBufferedReadArchive(GetPath());
#else
    mZipArchive = zip_open(GetPath().c_str(), ZIP_CREATE, nullptr);
#endif
    if (mZipArchive == nullptr) {
        SPDLOG_ERROR("Failed to reopen zip file after writing.");
        return false;
    }

    IndexFile(filePath);

    // Success
    return true;
}

} // namespace Ship
