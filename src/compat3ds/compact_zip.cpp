#include <ship/resource/archive/CompactZip.h>
#include <ship/resource/archive/RoomCache3DS.h>
#include <climits>
#include <cstring>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../../third_party/shipwright/torch/lib/miniz/zip_file.hpp"

namespace {
struct Reader {
    mz_zip_archive zip{};
    FILE* file = nullptr;
    Ship::RoomCache3DS roomCache;
    ~Reader() {
        mz_zip_reader_end(&zip);
        if (file) std::fclose(file);
    }
};

size_t ReadAt(void* opaque, mz_uint64 offset, void* output, size_t size) {
    auto* reader = static_cast<Reader*>(opaque);
    // This miniz version supports ordinary ZIP only (<4 GiB, no Zip64).
    if (offset > LONG_MAX || std::fseek(reader->file, static_cast<long>(offset), SEEK_SET) != 0) return 0;
    return std::fread(output, 1, size, reader->file);
}

void* Open(const char* path) {
    auto reader = std::make_unique<Reader>();
    reader->file = std::fopen(path, "rb");
    if (!reader->file) return nullptr;
    std::setvbuf(reader->file, nullptr, _IOFBF, 64 * 1024);
    if (std::fseek(reader->file, 0, SEEK_END) != 0) return nullptr;
    const long size = std::ftell(reader->file);
    if (size < 0) return nullptr;
    reader->zip.m_pRead = ReadAt;
    reader->zip.m_pIO_opaque = reader.get();
    if (!mz_zip_reader_init(&reader->zip, static_cast<mz_uint64>(size), 0)) return nullptr;
    return reader.release();
}

void Close(void* handle) { delete static_cast<Reader*>(handle); }
size_t Count(void* handle) { return mz_zip_reader_get_num_files(&static_cast<Reader*>(handle)->zip); }

std::string Name(void* handle, size_t index) {
    auto* zip = &static_cast<Reader*>(handle)->zip;
    const auto length = mz_zip_reader_get_filename(zip, index, nullptr, 0);
    if (length <= 1) return {};
    std::string name(length, '\0');
    if (mz_zip_reader_get_filename(zip, index, name.data(), length) != length) return {};
    name.resize(length - 1);
    return name;
}

// Same comparison as `Name(handle, index) == path` without the heap. The
// sampled allocation profile showed this one std::string dominating the port's
// per-frame small-allocation churn: Read() runs on every archive read, and the
// name was built only to be compared and discarded. Measured ~367 allocations
// per frame in steady state, 60% of them in the 33-64 byte class, feeding a
// heap that fragments to ~50,000 free blocks averaging 30 bytes until a
// 1,072-byte request fails with 1.5 MB still free.
//
// A name longer than the buffer truncates, which compares unequal and simply
// takes the existing case-sensitive fallback below: slower, still correct.
bool NameEquals(void* handle, size_t index, const std::string& path) {
    auto* zip = &static_cast<Reader*>(handle)->zip;
    char name[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
    const auto length = mz_zip_reader_get_filename(zip, index, name, sizeof(name));
    if (length <= 1) return false;
    return path.size() == static_cast<size_t>(length - 1) &&
           std::memcmp(name, path.data(), path.size()) == 0;
}

std::shared_ptr<std::vector<char>> Read(void* handle, const std::string& path) {
    auto* zip = &static_cast<Reader*>(handle)->zip;
    // Miniz's sorted index searches case-insensitively. Verify exact spelling;
    // only the uncommon case-collision path needs its linear exact search.
    int index = mz_zip_reader_locate_file(zip, path.c_str(), nullptr, 0);
    if (index < 0) return nullptr;
    if (!NameEquals(handle, index, path)) {
        index = mz_zip_reader_locate_file(zip, path.c_str(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
        if (index < 0) return nullptr;
    }
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(zip, index, &stat) || stat.m_uncomp_size > SIZE_MAX) return nullptr;
    auto& cache = static_cast<Reader*>(handle)->roomCache;
    if (const auto* entry = cache.Find(index)) return cache.Extract(*entry);
    auto data = std::make_shared<std::vector<char>>(static_cast<size_t>(stat.m_uncomp_size));
    if (!mz_zip_reader_extract_to_mem(zip, index, data->data(), data->size(), 0)) return nullptr;
    return data;
}

size_t PrefetchRoom(void* handle, const std::string& prefix, size_t budget) {
    auto* reader = static_cast<Reader*>(handle);
    auto& cache = reader->roomCache;
    cache.Clear();
    if (prefix.empty() || !cache.Begin(budget)) return 0;
    for (size_t i = 0; i < Count(handle); ++i) {
        // ZIP names already live in miniz's directory. Avoid one heap allocation
        // per archive entry during the synchronous room scan (40k+ names).
        std::array<char, 512> name;
        const auto length = mz_zip_reader_get_filename(&reader->zip, i, nullptr, 0);
        if (length == 0) continue;
        if (length <= name.size()) {
            mz_zip_reader_get_filename(&reader->zip, i, name.data(), name.size());
            if (!Ship::IsRoomAsset3DS(std::string_view(name.data(), length - 1), prefix)) continue;
        } else if (!Ship::IsRoomAsset3DS(Name(handle, i), prefix)) {
            continue;
        }
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&reader->zip, i, &stat)) continue;
        cache.Add(i, stat.m_comp_size, stat.m_uncomp_size, stat.m_crc32, stat.m_method,
            [&](void* data, size_t size) {
                return mz_zip_reader_extract_to_mem(&reader->zip, i, data, size, MZ_ZIP_FLAG_COMPRESSED_DATA) != 0;
            });
    }
    return cache.Bytes();
}
uint64_t PrefetchHits(void* handle) { return static_cast<Reader*>(handle)->roomCache.Hits(); }

// Non-allocating name fetch for ListFiles. Returns the length excluding the
// terminator, or 0 when the entry is absent or does not fit, which the caller
// treats as "fall back to the allocating path".
size_t NameInto(void* handle, size_t index, char* out, size_t capacity) {
    if (out == nullptr || capacity == 0) return 0;
    auto* zip = &static_cast<Reader*>(handle)->zip;
    const auto length = mz_zip_reader_get_filename(zip, index, out, capacity);
    if (length <= 1 || static_cast<size_t>(length) > capacity) return 0;
    return static_cast<size_t>(length - 1);
}

const Soh3dsCompactZipApi api{Open, Close, Count, Name, NameInto, Read, PrefetchRoom, PrefetchHits};
}

extern "C" const Soh3dsCompactZipApi* Soh3dsGetCompactZipApi() { return &api; }
