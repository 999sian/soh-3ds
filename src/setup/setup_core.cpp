#include "setup_core.h"
#include "setup_bundle.h"

#include <zip.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace Soh3dsSetup {
namespace {

constexpr size_t kBufferSize = 64 * 1024;

std::string ErrnoMessage(const char* action, const std::string& path) {
    return std::string(action) + " " + path + ": " + std::strerror(errno);
}

uint32_t RotateLeft(uint32_t value, unsigned shift) {
    return (value << shift) | (value >> (32 - shift));
}

class Sha1 {
  public:
    void Update(const uint8_t* data, size_t length) {
        totalBytes_ += length;
        while (length != 0) {
            const size_t take = std::min(length, block_.size() - used_);
            std::memcpy(block_.data() + used_, data, take);
            used_ += take;
            data += take;
            length -= take;
            if (used_ == block_.size()) {
                Transform(block_.data());
                used_ = 0;
            }
        }
    }

    std::string Finish() {
        const uint64_t bitCount = totalBytes_ * 8;
        block_[used_++] = 0x80;
        if (used_ > 56) {
            std::fill(block_.begin() + static_cast<std::ptrdiff_t>(used_), block_.end(), 0);
            Transform(block_.data());
            used_ = 0;
        }
        std::fill(block_.begin() + static_cast<std::ptrdiff_t>(used_), block_.begin() + 56, 0);
        for (unsigned i = 0; i < 8; ++i) {
            block_[63 - i] = static_cast<uint8_t>(bitCount >> (i * 8));
        }
        Transform(block_.data());

        char text[41];
        std::snprintf(text, sizeof(text), "%08lx%08lx%08lx%08lx%08lx", static_cast<unsigned long>(state_[0]),
                      static_cast<unsigned long>(state_[1]), static_cast<unsigned long>(state_[2]),
                      static_cast<unsigned long>(state_[3]), static_cast<unsigned long>(state_[4]));
        return text;
    }

  private:
    void Transform(const uint8_t* block) {
        uint32_t words[80];
        for (unsigned i = 0; i < 16; ++i) {
            words[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                       (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | block[i * 4 + 3];
        }
        for (unsigned i = 16; i < 80; ++i) {
            words[i] = RotateLeft(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
        }

        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];
        uint32_t e = state_[4];
        for (unsigned i = 0; i < 80; ++i) {
            uint32_t f;
            uint32_t k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5a827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdc;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6;
            }
            const uint32_t next = RotateLeft(a, 5) + f + e + k + words[i];
            e = d;
            d = c;
            c = RotateLeft(b, 30);
            b = a;
            a = next;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }

    std::array<uint32_t, 5> state_ = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
    std::array<uint8_t, 64> block_ = {};
    uint64_t totalBytes_ = 0;
    size_t used_ = 0;
};

enum class RomOrder {
    BigEndian,
    ByteSwapped,
    LittleEndian,
};

bool DetectOrder(const uint8_t magic[4], RomOrder& order) {
    if (std::memcmp(magic, "\x80\x37\x12\x40", 4) == 0) {
        order = RomOrder::BigEndian;
    } else if (std::memcmp(magic, "\x37\x80\x40\x12", 4) == 0) {
        order = RomOrder::ByteSwapped;
    } else if (std::memcmp(magic, "\x40\x12\x37\x80", 4) == 0) {
        order = RomOrder::LittleEndian;
    } else {
        return false;
    }
    return true;
}

void NormalizeBytes(uint8_t* bytes, size_t length, RomOrder order) {
    if (order == RomOrder::ByteSwapped) {
        for (size_t i = 0; i < length; i += 2) {
            std::swap(bytes[i], bytes[i + 1]);
        }
    } else if (order == RomOrder::LittleEndian) {
        for (size_t i = 0; i < length; i += 4) {
            std::swap(bytes[i], bytes[i + 3]);
            std::swap(bytes[i + 1], bytes[i + 2]);
        }
    }
}

bool FlushFile(FILE* file, const std::string& path, std::string& error) {
    if (std::fflush(file) != 0 || ::fsync(::fileno(file)) != 0) {
        error = ErrnoMessage("Could not flush", path);
        return false;
    }
    return true;
}

bool ParseVersion(const std::string& version, std::array<uint8_t, 7>& encoded) {
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
    char trailing = 0;
    if (std::sscanf(version.c_str(), "%u.%u.%u%c", &major, &minor, &patch, &trailing) != 3 || major > 65535 ||
        minor > 65535 || patch > 65535) {
        return false;
    }
    encoded = { 1, static_cast<uint8_t>(major >> 8), static_cast<uint8_t>(major),
                static_cast<uint8_t>(minor >> 8), static_cast<uint8_t>(minor),
                static_cast<uint8_t>(patch >> 8), static_cast<uint8_t>(patch) };
    return true;
}

bool ReadZipEntry(zip_t* archive, zip_uint64_t index, std::vector<uint8_t>* capture, size_t captureLimit,
                  std::string& error) {
    zip_stat_t stat = {};
    zip_stat_init(&stat);
    if (zip_stat_index(archive, index, 0, &stat) != 0 || (stat.valid & ZIP_STAT_SIZE) == 0) {
        error = "Could not inspect an archive entry";
        return false;
    }
    zip_file_t* entry = zip_fopen_index(archive, index, 0);
    if (entry == nullptr) {
        error = "Could not open an archive entry: " + std::string(zip_strerror(archive));
        return false;
    }
    if (capture != nullptr) {
        if (stat.size > captureLimit || stat.size > std::numeric_limits<size_t>::max()) {
            zip_fclose(entry);
            error = "Archive metadata entry is too large";
            return false;
        }
        capture->clear();
        capture->reserve(static_cast<size_t>(stat.size));
    }
    std::array<uint8_t, kBufferSize> buffer;
    zip_uint64_t total = 0;
    uLong crc = crc32(0, Z_NULL, 0);
    bool ok = true;
    while (total < stat.size) {
        const zip_uint64_t remaining = stat.size - total;
        const zip_uint64_t request = std::min<zip_uint64_t>(buffer.size(), remaining);
        const zip_int64_t got = zip_fread(entry, buffer.data(), request);
        if (got <= 0) {
            error = "Archive payload is truncated or corrupt";
            ok = false;
            break;
        }
        if (capture != nullptr) {
            capture->insert(capture->end(), buffer.begin(), buffer.begin() + got);
        }
        crc = crc32(crc, buffer.data(), static_cast<uInt>(got));
        total += static_cast<zip_uint64_t>(got);
    }
    if (ok) {
        const zip_int64_t eof = zip_fread(entry, buffer.data(), 1);
        if (eof != 0) {
            error = "Archive payload has inconsistent length";
            ok = false;
        }
    }
    if (ok && (stat.valid & ZIP_STAT_CRC) != 0 && static_cast<zip_uint32_t>(crc) != stat.crc) {
        error = "Archive payload failed its CRC check";
        ok = false;
    }
    if (zip_fclose(entry) != 0) {
        error = "Archive payload failed its integrity check";
        ok = false;
    }
    return ok && total == stat.size;
}

bool IsKnownGameVersion(const std::vector<uint8_t>& version) {
    if (version.size() != 5 || version[0] != 1) return false;
    const uint32_t crc = (static_cast<uint32_t>(version[1]) << 24) |
                         (static_cast<uint32_t>(version[2]) << 16) |
                         (static_cast<uint32_t>(version[3]) << 8) | version[4];
    constexpr std::array<uint32_t, 17> known = {
        0xEC7011B7, 0xD43DA81F, 0x693BA2AE, 0xB044B569, 0xB2055FBD, 0xF7F52DB8,
        0xF611F4BA, 0xF3DD35BA, 0x09465AC3, 0xF43B45BA, 0xF034001A, 0x1D4136F3,
        0x871E1C92, 0x87121EFE, 0x917D18F6, 0x3D81FB3E, 0xB1E1E07B,
    };
    return std::find(known.begin(), known.end(), crc) != known.end();
}

bool HasRequiredEntry(zip_t* archive, const char* name, std::string& error) {
    const zip_int64_t index = zip_name_locate(archive, name, 0);
    if (index < 0) {
        error = "Archive is missing required asset " + std::string(name);
        return false;
    }
    zip_stat_t stat = {};
    zip_stat_init(&stat);
    if (zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &stat) != 0 ||
        (stat.valid & ZIP_STAT_SIZE) == 0 || stat.size == 0) {
        error = "Archive has an empty required asset " + std::string(name);
        return false;
    }
    return ReadZipEntry(archive, static_cast<zip_uint64_t>(index), nullptr, 0, error);
}

} // namespace

bool NormalizeRom(const std::string& source, const std::string& destination, Progress progress, void* user,
                  std::string& sha1, std::string& error) {
    error.clear();
    sha1.clear();
    struct stat sourceStat = {};
    if (::stat(source.c_str(), &sourceStat) != 0 || sourceStat.st_size < 4) {
        error = ErrnoMessage("Could not read ROM", source);
        return false;
    }

    FILE* input = std::fopen(source.c_str(), "rb");
    if (input == nullptr) {
        error = ErrnoMessage("Could not open ROM", source);
        return false;
    }
    uint8_t magic[4];
    RomOrder order = RomOrder::BigEndian;
    if (std::fread(magic, 1, sizeof(magic), input) != sizeof(magic) || !DetectOrder(magic, order)) {
        std::fclose(input);
        error = "ROM has an unsupported byte order or invalid header";
        return false;
    }
    if ((order == RomOrder::ByteSwapped && sourceStat.st_size % 2 != 0) ||
        (order == RomOrder::LittleEndian && sourceStat.st_size % 4 != 0)) {
        std::fclose(input);
        error = "ROM is truncated in the middle of a swapped word";
        return false;
    }
    std::rewind(input);

    const std::string partial = destination + ".part";
    FILE* output = std::fopen(partial.c_str(), "wb");
    if (output == nullptr) {
        std::fclose(input);
        error = ErrnoMessage("Could not create", partial);
        return false;
    }

    Sha1 hash;
    std::array<uint8_t, kBufferSize> buffer;
    uint64_t done = 0;
    bool ok = true;
    while (done < static_cast<uint64_t>(sourceStat.st_size)) {
        const size_t request = static_cast<size_t>(std::min<uint64_t>(buffer.size(), sourceStat.st_size - done));
        const size_t got = std::fread(buffer.data(), 1, request, input);
        if (got != request) {
            error = "ROM read stopped before the advertised file size";
            ok = false;
            break;
        }
        NormalizeBytes(buffer.data(), got, order);
        hash.Update(buffer.data(), got);
        if (std::fwrite(buffer.data(), 1, got, output) != got) {
            error = ErrnoMessage("Could not write normalized ROM", partial);
            ok = false;
            break;
        }
        done += got;
        if (progress != nullptr && !progress("Normalizing ROM", static_cast<size_t>(done),
                                             static_cast<size_t>(sourceStat.st_size), user)) {
            error = "ROM normalization cancelled";
            ok = false;
            break;
        }
    }
    if (ok) {
        ok = FlushFile(output, partial, error);
    }
    if (std::fclose(output) != 0 && ok) {
        error = ErrnoMessage("Could not close", partial);
        ok = false;
    }
    std::fclose(input);
    if (!ok) {
        std::remove(partial.c_str());
        return false;
    }
    sha1 = hash.Finish();
    if (std::rename(partial.c_str(), destination.c_str()) != 0) {
        error = ErrnoMessage("Could not finish normalized ROM", destination);
        std::remove(partial.c_str());
        return false;
    }
    return true;
}

bool LoadRomMetadata(const std::string& path, std::vector<RomMetadata>& rows, std::string& error) {
    rows.clear();
    error.clear();
    std::ifstream input(path);
    if (!input) {
        error = "Could not open supported-ROM metadata: " + path;
        return false;
    }
    std::set<std::string> hashes;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t first = line.find('\t');
        const size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
        const size_t third = second == std::string::npos ? second : line.find('\t', second + 1);
        if (first == std::string::npos || second == std::string::npos || third == std::string::npos ||
            line.find('\t', third + 1) != std::string::npos) {
            error = "Malformed supported-ROM metadata at line " + std::to_string(lineNumber);
            rows.clear();
            return false;
        }
        RomMetadata row{ line.substr(0, first), line.substr(first + 1, second - first - 1), line.substr(second + 1, third - second - 1), line.substr(third + 1) };
        std::transform(row.sha1.begin(), row.sha1.end(), row.sha1.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool hashOk = row.sha1.size() == 40 &&
                            std::all_of(row.sha1.begin(), row.sha1.end(), [](unsigned char c) { return std::isxdigit(c); });
        const bool outputOk = row.outputArchive == "oot.o2r" || row.outputArchive == "oot-mq.o2r";
        if (!hashOk || !outputOk || !IsMetadataVariant(row.metadataVariant) || row.displayName.empty() || !hashes.insert(row.sha1).second) {
            error = "Invalid supported-ROM metadata at line " + std::to_string(lineNumber);
            rows.clear();
            return false;
        }
        rows.push_back(std::move(row));
    }
    if (!input.eof() || rows.empty()) {
        error = rows.empty() ? "Supported-ROM metadata is empty" : "Could not read supported-ROM metadata";
        rows.clear();
        return false;
    }
    return true;
}

const RomMetadata* FindRomMetadata(const std::vector<RomMetadata>& rows, const std::string& sha1) {
    auto found = std::find_if(rows.begin(), rows.end(), [&sha1](const RomMetadata& row) { return row.sha1 == sha1; });
    return found == rows.end() ? nullptr : &*found;
}

bool CopyFile(const std::string& source, const std::string& destination, Progress progress, void* user,
              std::string& error) {
    error.clear();
    struct stat sourceStat = {};
    if (::stat(source.c_str(), &sourceStat) != 0 || sourceStat.st_size < 0) {
        error = ErrnoMessage("Could not inspect", source);
        return false;
    }
    FILE* input = std::fopen(source.c_str(), "rb");
    const std::string partial = destination + ".part";
    FILE* output = input == nullptr ? nullptr : std::fopen(partial.c_str(), "wb");
    if (input == nullptr || output == nullptr) {
        if (input != nullptr) std::fclose(input);
        if (output != nullptr) std::fclose(output);
        std::remove(partial.c_str());
        error = ErrnoMessage("Could not stage file", source);
        return false;
    }
    std::array<uint8_t, kBufferSize> buffer;
    uint64_t done = 0;
    bool ok = true;
    while (done < static_cast<uint64_t>(sourceStat.st_size)) {
        const size_t request = static_cast<size_t>(std::min<uint64_t>(buffer.size(), sourceStat.st_size - done));
        const size_t got = std::fread(buffer.data(), 1, request, input);
        if (got != request || std::fwrite(buffer.data(), 1, got, output) != got) {
            error = "File copy stopped before completion";
            ok = false;
            break;
        }
        done += got;
        if (progress != nullptr && !progress("Copying support archive", static_cast<size_t>(done),
                                             static_cast<size_t>(sourceStat.st_size), user)) {
            error = "File copy cancelled";
            ok = false;
            break;
        }
    }
    if (ok) ok = FlushFile(output, partial, error);
    if (std::fclose(output) != 0 && ok) ok = false;
    std::fclose(input);
    if (!ok || std::rename(partial.c_str(), destination.c_str()) != 0) {
        if (ok) error = ErrnoMessage("Could not finish staging", destination);
        std::remove(partial.c_str());
        return false;
    }
    return true;
}

bool AtomicInstall(const std::string& staged, const std::string& destination, std::string& error) {
    error.clear();
    if (std::rename(staged.c_str(), destination.c_str()) != 0) {
        error = ErrnoMessage("Could not install", destination);
        return false;
    }
    return true;
}

bool CleanupSetupArtifacts(const std::string& stagingDirectory, std::string& error) {
    error.clear();
    constexpr std::array<const char*, 10> names = {
        "normalized.z64", "normalized.z64.part", "oot.o2r", "oot.o2r.part",
        "oot-mq.o2r",     "oot-mq.o2r.part",     "soh.o2r", "soh.o2r.part",
        "oot.o2r.central.tmp", "oot-mq.o2r.central.tmp",
    };
    bool ok = true;
    const std::string separator = !stagingDirectory.empty() && stagingDirectory.back() == '/' ? "" : "/";
    for (const char* name : names) {
        const std::string path = stagingDirectory + separator + name;
        if (std::remove(path.c_str()) != 0 && errno != ENOENT) {
            if (ok) error = ErrnoMessage("Could not remove stale setup file", path);
            ok = false;
        }
    }
    std::string metadataError;
    if (!CleanupMetadata(stagingDirectory + separator + "torch", metadataError)) {
        if (ok) error = metadataError;
        ok = false;
    }
    return ok;
}

bool HasDiskHeadroom(const std::string& directory, uint64_t requiredBytes, uint64_t reserveBytes,
                     uint64_t& availableBytes, std::string& error) {
    error.clear();
    struct statvfs stats = {};
    if (::statvfs(directory.c_str(), &stats) != 0) {
        availableBytes = 0;
        error = ErrnoMessage("Could not inspect free space in", directory);
        return false;
    }
    availableBytes = static_cast<uint64_t>(stats.f_bavail) * static_cast<uint64_t>(stats.f_frsize);
    if (requiredBytes > std::numeric_limits<uint64_t>::max() - reserveBytes ||
        availableBytes < requiredBytes + reserveBytes) {
        error = "Not enough free space for setup";
        return false;
    }
    return true;
}

bool ValidateArchive(const std::string& path, const std::string& expectedPortVersion, ArchiveKind kind,
                     bool full, std::string& error, Progress progress, void* user) {
    error.clear();
    std::array<uint8_t, 7> expected = {};
    if (!ParseVersion(expectedPortVersion, expected)) {
        error = "Invalid expected port version: " + expectedPortVersion;
        return false;
    }
    int openError = 0;
    // OoT archives intentionally contain duplicate path occurrences for assets
    // emitted more than once. ZIP_CHECKCONS rejects those valid archives with
    // ZIP_ER_EXISTS, so validate the parsed directory and payload CRCs ourselves.
    zip_t* archive = zip_open(path.c_str(), ZIP_RDONLY, &openError);
    if (archive == nullptr) {
        zip_error_t detail;
        zip_error_init_with_code(&detail, openError);
        error = "Could not open archive " + path + ": " + zip_error_strerror(&detail);
        zip_error_fini(&detail);
        return false;
    }
    bool ok = true;
    const zip_int64_t count = zip_get_num_entries(archive, 0);
    if (count <= 1) {
        error = "Archive contains no asset payloads";
        ok = false;
    }
    const zip_int64_t portIndex = ok ? zip_name_locate(archive, "portVersion", 0) : -1;
    std::vector<uint8_t> version;
    if (ok && (portIndex < 0 ||
               !ReadZipEntry(archive, static_cast<zip_uint64_t>(portIndex), &version, 7, error))) {
        if (portIndex < 0) error = "Archive has no portVersion entry";
        ok = false;
    }
    if (ok && !std::equal(expected.begin(), expected.end(), version.begin(), version.end())) {
        error = "Archive portVersion does not match " + expectedPortVersion;
        ok = false;
    }
    if (ok && kind == ArchiveKind::Game) {
        const zip_int64_t gameVersionIndex = zip_name_locate(archive, "version", 0);
        std::vector<uint8_t> gameVersion;
        if (gameVersionIndex < 0 ||
            !ReadZipEntry(archive, static_cast<zip_uint64_t>(gameVersionIndex), &gameVersion, 5, error) ||
            !IsKnownGameVersion(gameVersion)) {
            error = "Game archive has no recognized ROM version entry";
            ok = false;
        }
    }
    if (ok && kind == ArchiveKind::Game) {
        ok = HasRequiredEntry(archive, "objects/object_link_boy/gLinkAdultSkel", error) &&
             HasRequiredEntry(archive, "textures/nintendo_rogo_static/gNintendo64LogoDL", error);
    } else if (ok) {
        ok = HasRequiredEntry(archive, "textures/icons/gIcon.png", error);
    }
    if (ok && full) {
        if (progress != nullptr && !progress("Checking game data", 0, static_cast<size_t>(count), user)) {
            error = "Archive validation cancelled";
            ok = false;
        }
        for (zip_int64_t i = 0; i < count; ++i) {
            if (!ok) break;
            const char* name = zip_get_name(archive, static_cast<zip_uint64_t>(i), 0);
            if (name == nullptr) {
                error = "Archive contains an unreadable filename";
                ok = false;
                break;
            }
            const size_t length = std::strlen(name);
            if (length != 0 && name[length - 1] == '/') continue;
            if (!ReadZipEntry(archive, static_cast<zip_uint64_t>(i), nullptr, 0, error)) {
                ok = false;
                break;
            }
            if (progress != nullptr &&
                !progress("Checking game data", static_cast<size_t>(i + 1), static_cast<size_t>(count), user)) {
                error = "Archive validation cancelled";
                ok = false;
                break;
            }
        }
    }
    if (zip_close(archive) != 0 && ok) {
        error = "Could not close archive after validation";
        ok = false;
    }
    return ok;
}

} // namespace Soh3dsSetup
