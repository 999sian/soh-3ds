#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>
#include <zlib.h>

namespace Ship {
inline bool IsRoomAsset3DS(std::string_view path, std::string_view prefix) {
    if (path.starts_with("alt/")) path.remove_prefix(4);
    if (prefix.empty() || !path.starts_with(prefix)) return false;
    // room_1 also prefixes room_10. Asset suffixes begin with a non-digit.
    return path.size() == prefix.size() || path[prefix.size()] < '0' || path[prefix.size()] > '9';
}

// Ordinary heap only. No decoded resources, GPU allocations or ZIP names are
// retained. The byte charge includes the fixed index and allocator allowance.
class RoomCache3DS {
  public:
    struct Entry {
        size_t index = 0, size = 0, decodedSize = 0;
        uint32_t crc = 0;
        uint16_t method = 0;
        std::unique_ptr<unsigned char[]> data;
    };
    void Clear() { mStorage.reset(); mBudget = mBytes = mHits = 0; }
    bool Begin(size_t budget) {
        Clear();
        if (budget < sizeof(Storage) + 16) return false;
        mStorage.reset(new (std::nothrow) Storage);
        if (!mStorage) return false;
        mBudget = budget;
        mBytes = sizeof(Storage) + 16;
        return true;
    }
    template<class ReadCompressed>
    bool Add(size_t index, uint64_t size, uint64_t decodedSize, uint32_t crc,
             uint16_t method, ReadCompressed read) {
        if (!mStorage || mStorage->count == mStorage->entries.size() ||
            (method != 0 && method != 8) || size == 0 ||
            size > std::numeric_limits<uInt>::max() || decodedSize > std::numeric_limits<uInt>::max() ||
            mBudget - mBytes < 16 || size > mBudget - mBytes - 16) return false;
        auto data = std::unique_ptr<unsigned char[]>(new (std::nothrow) unsigned char[static_cast<size_t>(size)]);
        if (!data || !read(data.get(), static_cast<size_t>(size))) return false;
        mStorage->entries[mStorage->count++] = {index, static_cast<size_t>(size),
            static_cast<size_t>(decodedSize), crc, method, std::move(data)};
        mBytes += static_cast<size_t>(size) + 16;
        return true;
    }
    const Entry* Find(size_t index) const {
        if (mStorage) for (size_t i = 0; i < mStorage->count; ++i) {
            if (mStorage->entries[i].index == index) return &mStorage->entries[i];
        }
        return nullptr;
    }
    std::shared_ptr<std::vector<char>> Extract(const Entry& entry) {
        auto output = std::make_shared<std::vector<char>>(entry.decodedSize);
        if (entry.method == 0) {
            if (entry.size != entry.decodedSize) return nullptr;
            std::memcpy(output->data(), entry.data.get(), entry.size);
        } else {
            z_stream stream{};
            stream.next_in = entry.data.get(); stream.avail_in = static_cast<uInt>(entry.size);
            // Empty deflated entries still need room for inflate to reach END.
            unsigned char empty = 0;
            stream.next_out = output->empty() ? &empty : reinterpret_cast<Bytef*>(output->data());
            stream.avail_out = output->empty() ? 1 : static_cast<uInt>(output->size());
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) return nullptr;
            const int result = inflate(&stream, Z_FINISH);
            const bool valid = result == Z_STREAM_END && stream.total_out == entry.decodedSize &&
                               stream.total_in == entry.size;
            inflateEnd(&stream);
            if (!valid) return nullptr;
        }
        if (crc32(0, reinterpret_cast<const Bytef*>(output->data()), static_cast<uInt>(output->size())) != entry.crc)
            return nullptr;
        ++mHits;
        return output;
    }
    size_t Bytes() const { return mBytes; }
    uint64_t Hits() const { return mHits; }
  private:
    struct Storage { std::array<Entry, 128> entries; size_t count = 0; };
    std::unique_ptr<Storage> mStorage;
    size_t mBudget = 0, mBytes = 0;
    uint64_t mHits = 0;
};
}
