#include "setup_bundle.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace Soh3dsSetup {
namespace {
constexpr size_t kBuffer = 32 * 1024;
constexpr size_t kMetadataLimit = 16 * 1024 * 1024;
using File = std::unique_ptr<FILE, int (*)(FILE*)>;

bool Fail(std::string& error, const std::string& message) {
    error = message;
    return false;
}

// Use inflate directly: gzread accepts uncompressed input, concatenated members
// and trailing garbage, whereas each packaged payload is exactly one gzip stream.
class GzipReader {
  public:
    GzipReader(const std::string& path, size_t limit, const char* stage, Progress progress, void* user,
               std::string& error)
        : file_(std::fopen(path.c_str(), "rb"), std::fclose), input_(kBuffer), limit_(limit), stage_(stage),
          progress_(progress), user_(user), error_(error) {
        struct stat info = {};
        if (!file_ || ::stat(path.c_str(), &info) != 0 || info.st_size <= 0) {
            Fail(error_, "Could not open setup bundle: " + path);
            return;
        }
        total_ = static_cast<size_t>(info.st_size);
        initialized_ = inflateInit2(&stream_, 15 + 16) == Z_OK;
        if (!initialized_) Fail(error_, "Could not initialize setup decompression");
    }
    ~GzipReader() { if (initialized_) inflateEnd(&stream_); }

    // Return up to requested bytes; zero means a verified end of the stream.
    bool Read(void* destination, size_t requested, size_t& got) {
        got = 0;
        if (!initialized_) return false;
        if (finished_) return true;
        stream_.next_out = static_cast<Bytef*>(destination);
        stream_.avail_out = static_cast<uInt>(requested);
        while (stream_.avail_out != 0 && !finished_) {
            if (stream_.avail_in == 0) {
                const size_t count = std::fread(input_.data(), 1, input_.size(), file_.get());
                if (!count) return Fail(error_, "Setup bundle is truncated or unreadable");
                stream_.next_in = input_.data();
                stream_.avail_in = static_cast<uInt>(count);
            }
            const uInt before = stream_.avail_out;
            const int result = inflate(&stream_, Z_NO_FLUSH);
            const size_t produced = before - stream_.avail_out;
            if (produced > limit_ - expanded_) return Fail(error_, "Expanded setup bundle exceeds its size limit");
            expanded_ += produced;
            if (result == Z_STREAM_END) {
                if (stream_.avail_in || std::fgetc(file_.get()) != EOF || std::ferror(file_.get()))
                    return Fail(error_, "Unexpected data after setup gzip stream");
                finished_ = true;
            } else if (result != Z_OK) {
                return Fail(error_, "Setup gzip stream is damaged or has an invalid checksum");
            }
        }
        got = requested - stream_.avail_out;
        if (progress_ && !progress_(stage_, static_cast<size_t>(stream_.total_in), total_, user_))
            return Fail(error_, "Setup decompression cancelled");
        return true;
    }

    bool Exact(void* destination, size_t size) {
        size_t got = 0;
        if (!Read(destination, size, got)) return false;
        return got == size || Fail(error_, "Setup tar is truncated");
    }

  private:
    File file_;
    std::vector<unsigned char> input_;
    z_stream stream_ = {};
    size_t limit_, total_ = 0, expanded_ = 0;
    const char* stage_;
    Progress progress_;
    void* user_;
    std::string& error_;
    bool initialized_ = false, finished_ = false;
};

bool Zero(const unsigned char* data, size_t size) {
    return std::all_of(data, data + size, [](unsigned char c) { return c == 0; });
}

bool Octal(const unsigned char* data, size_t length, size_t& value) {
    value = 0;
    size_t i = 0;
    while (i < length && data[i] == ' ') ++i;
    const size_t start = i;
    while (i < length && data[i] >= '0' && data[i] <= '7') {
        if (value > (kMetadataLimit - (data[i] - '0')) / 8) return false;
        value = value * 8 + (data[i++] - '0');
    }
    if (i == start) return false;
    while (i < length && (data[i] == ' ' || data[i] == 0)) ++i;
    return i == length;
}

std::string TarString(const unsigned char* data, size_t length) {
    const auto* end = std::find(data, data + length, 0);
    return std::string(reinterpret_cast<const char*>(data), static_cast<size_t>(end - data));
}

bool SafeYamlPath(const std::string& path, const std::string& variant) {
    if (path == "config.yml") return true;
    if (!path.starts_with(variant + "/") || !path.ends_with(".yml") || path.size() > 255 ||
        path.find_first_of("\\:") != std::string::npos) return false;
    size_t start = 0;
    unsigned depth = 0;
    while (start < path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        const std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || ++depth > 16) return false;
        for (unsigned char c : part) if (c < 32 || c >= 127) return false;
        start = end + 1;
    }
    return true;
}

bool CreateParents(const std::string& root, const std::string& name, std::string& error) {
    size_t slash = name.find('/');
    while (slash != std::string::npos) {
        const std::string path = root + "/" + name.substr(0, slash);
        if (::mkdir(path.c_str(), 0777) != 0 && errno != EEXIST)
            return Fail(error, "Could not create setup metadata directory: " + path);
        struct stat info = {};
        if (::stat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode))
            return Fail(error, "Setup metadata path is not a directory: " + path);
        slash = name.find('/', slash + 1);
    }
    return true;
}

bool Unpack(GzipReader& input, const std::string& root, const std::string& variant, std::string& error) {
    std::array<unsigned char, 512> header = {};
    std::vector<unsigned char> buffer(kBuffer);
    std::set<std::string> names;
    while (true) {
        if (!input.Exact(header.data(), header.size())) return false;
        if (Zero(header.data(), header.size())) {
            if (!input.Exact(header.data(), header.size())) return false;
            if (!Zero(header.data(), header.size())) return Fail(error, "Invalid setup tar terminator");
            // Consume all padding and the gzip trailer, including its CRC.
            size_t got = 0;
            do {
                if (!input.Read(buffer.data(), buffer.size(), got)) return false;
                if (!Zero(buffer.data(), got)) return Fail(error, "Unexpected data after setup tar terminator");
            } while (got);
            return (names.contains("config.yml") && names.size() >= 2) ||
                   Fail(error, "Setup bundle is missing configuration or variant metadata");
        }
        size_t checksum = 0, size = 0, sum = 0;
        for (size_t i = 0; i < header.size(); ++i) sum += (i >= 148 && i < 156) ? ' ' : header[i];
        if (!Octal(header.data() + 148, 8, checksum) || checksum != sum ||
            std::memcmp(header.data() + 257, "ustar\0" "00", 8) != 0 ||
            (header[156] != '0' && header[156] != 0) ||
            !Octal(header.data() + 124, 12, size) || size > 1024 * 1024)
            return Fail(error, "Invalid setup tar header, file type, checksum or size");
        std::string name = TarString(header.data(), 100);
        const std::string prefix = TarString(header.data() + 345, 155);
        if (!prefix.empty()) name = prefix + "/" + name;
        if (!SafeYamlPath(name, variant) || !names.insert(name).second || names.size() > 4096)
            return Fail(error, "Unsafe, duplicate or excessive setup metadata path: " + name);
        if (!CreateParents(root, name, error)) return false;
        const std::string path = root + "/" + name;
        File output(std::fopen(path.c_str(), "wb"), std::fclose);
        if (!output) return Fail(error, "Could not create setup metadata: " + path);
        for (size_t remaining = size; remaining;) {
            const size_t count = std::min(remaining, buffer.size());
            if (!input.Exact(buffer.data(), count)) return false;
            if (std::fwrite(buffer.data(), 1, count, output.get()) != count)
                return Fail(error, "Could not write setup metadata: " + path);
            remaining -= count;
        }
        if (std::fclose(output.release()) != 0) return Fail(error, "Could not close setup metadata: " + path);
        const size_t padding = (512 - size % 512) % 512;
        if (!input.Exact(header.data(), padding)) return false;
        if (!Zero(header.data(), padding)) return Fail(error, "Invalid setup tar file padding");
    }
}
} // namespace

bool IsMetadataVariant(const std::string& variant) {
    return !variant.empty() && variant.size() <= 64 &&
           std::all_of(variant.begin(), variant.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
           });
}

bool CleanupMetadata(const std::string& directory, std::string& error) {
    struct stat info = {};
#ifdef __3DS__
    // The 3DS SD filesystem has no symlinks and newlib does not expose lstat.
    const int inspected = ::stat(directory.c_str(), &info);
#else
    const int inspected = ::lstat(directory.c_str(), &info);
#endif
    if (inspected != 0) return errno == ENOENT || Fail(error, "Could not inspect setup metadata: " + directory);
    if (!S_ISDIR(info.st_mode))
        return std::remove(directory.c_str()) == 0 || Fail(error, "Could not remove setup metadata: " + directory);
    DIR* handle = ::opendir(directory.c_str());
    if (!handle) return Fail(error, "Could not open setup metadata directory: " + directory);
    bool ok = true;
    while (true) {
        errno = 0;
        dirent* entry = ::readdir(handle);
        if (!entry) {
            if (errno != 0) ok = Fail(error, "Could not read setup metadata directory: " + directory);
            break;
        }
        const std::string name = entry->d_name;
        if (name != "." && name != ".." && !CleanupMetadata(directory + "/" + name, error)) {
            ok = false;
            break;
        }
    }
    ::closedir(handle);
    return ok && (::rmdir(directory.c_str()) == 0 || Fail(error, "Could not remove setup metadata directory: " + directory));
}

bool InflateGzipFile(const std::string& source, const std::string& destination, size_t maximumBytes,
                     Progress progress, void* user, std::string& error) {
    error.clear();
    const std::string partial = destination + ".part";
    bool ok = [&]() {
        GzipReader input(source, maximumBytes, "Unpacking support data", progress, user, error);
        File output(std::fopen(partial.c_str(), "wb"), std::fclose);
        if (!output) return Fail(error, "Could not create staged support data");
        std::vector<unsigned char> buffer(kBuffer);
        size_t got = 0;
        do {
            if (!input.Read(buffer.data(), buffer.size(), got)) return false;
            if (std::fwrite(buffer.data(), 1, got, output.get()) != got)
                return Fail(error, "Could not write staged support data");
        } while (got);
        if (std::fflush(output.get()) != 0 || ::fsync(::fileno(output.get())) != 0)
            return Fail(error, "Could not flush staged support data");
        if (std::fclose(output.release()) != 0) return Fail(error, "Could not close staged support data");
        return std::rename(partial.c_str(), destination.c_str()) == 0 || Fail(error, "Could not finish staged support data");
    }();
    if (!ok) std::remove(partial.c_str());
    return ok;
}

bool UnpackMetadata(const std::string& source, const std::string& destination, const std::string& variant,
                    Progress progress, void* user, std::string& error) {
    error.clear();
    if (!IsMetadataVariant(variant)) return Fail(error, "Invalid setup metadata variant");
    // Refuse existing trees so a failed attempt can only remove its own files.
    if (::mkdir(destination.c_str(), 0777) != 0) return Fail(error, "Could not create fresh setup metadata directory");
    bool ok = false;
    {
        GzipReader input(source, kMetadataLimit, "Unpacking ROM definitions", progress, user, error);
        ok = Unpack(input, destination, variant, error);
    }
    if (!ok) {
        std::string cleanupError;
        if (!CleanupMetadata(destination, cleanupError)) error += "; " + cleanupError;
    }
    return ok;
}
} // namespace Soh3dsSetup
