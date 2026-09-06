// Run with the linker wrappers in tests/save_transaction_test.sh. These inject
// failures below the shared production helper; all data lives in mkdtemp dirs.
#include "../third_party/shipwright/soh/soh/SaveFileTransaction.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sys/wait.h>

static std::string failure;
static int failAt = 1, seen = 0, interruptAfterRename = 0, renames = 0;
static bool Hit(const char* operation) { return failure == operation && ++seen == failAt; }
extern "C" {
FILE* __real_fopen(const char*, const char*);
size_t __real_fwrite(const void*, size_t, size_t, FILE*);
size_t __real_fread(void*, size_t, size_t, FILE*);
int __real_fclose(FILE*);
int __real_fflush(FILE*);
int __real_fsync(int);
int __real_rename(const char*, const char*);
int __real_remove(const char*);
int __real_ferror(FILE*);
FILE* __wrap_fopen(const char* path, const char* mode) {
    if (Hit("open")) { errno = EIO; return nullptr; }
    return __real_fopen(path, mode);
}
size_t __wrap_fwrite(const void* data, size_t size, size_t count, FILE* file) {
    if (Hit("write")) return __real_fwrite(data, size, count / 2, file);
    return __real_fwrite(data, size, count, file);
}
size_t __wrap_fread(void* data, size_t size, size_t count, FILE* file) {
    if (Hit("read")) return 0;
    return __real_fread(data, size, count, file);
}
int __wrap_ferror(FILE* file) {
    if (failure == "read" && seen >= failAt) return 1;
    return __real_ferror(file);
}
int __wrap_fclose(FILE* file) {
    const int result = __real_fclose(file);
    if (Hit("close")) { errno = EIO; return EOF; }
    return result;
}
int __wrap_fflush(FILE* file) {
    if (Hit("flush")) { errno = EIO; return EOF; }
    return __real_fflush(file);
}
int __wrap_fsync(int fd) {
    if (Hit("sync")) { errno = EIO; return -1; }
    return __real_fsync(fd);
}
int __wrap_rename(const char* from, const char* to) {
    if (Hit("rename")) { errno = EIO; return -1; }
    // Match sdmc: rename does not overwrite an existing destination.
    struct stat info;
    if (::stat(to, &info) == 0) { errno = EEXIST; return -1; }
    const int result = __real_rename(from, to);
    if (result == 0 && interruptAfterRename && ++renames == interruptAfterRename) _exit(77);
    return result;
}
int __wrap_remove(const char* path) {
    if (Hit("remove")) { errno = EIO; return -1; }
    return __real_remove(path);
}
}

static void Reset() { failure.clear(); seen = 0; failAt = 1; interruptAfterRename = 0; renames = 0; }
static std::string Read(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void Put(const std::string& path, const std::string& data) {
    std::ofstream file(path, std::ios::binary); file << data;
}
struct Fixture {
    std::string dir, path, source;
    Fixture() {
        Reset();
        char name[] = "/tmp/soh-save-transaction-XXXXXX";
        assert(mkdtemp(name)); dir = name; path = dir + "/slot.sav"; source = dir + "/source.sav";
        Put(path, "previous complete save");
        // Exercise multiple copy blocks, not just the first fwrite.
        Put(source, std::string(10000, 's'));
    }
    ~Fixture() { Reset(); std::filesystem::remove_all(dir); }
};
static void Check(bool condition, const std::string& name) {
    if (!condition) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
static int cases = 0;
int main() {
    // A missing byte-count/close/error check or premature removal of the slot
    // must fail these assertions on returned status AND recoverable disk data.
    for (bool copy : {false, true}) {
        std::map<std::string, int> faults{{"open", copy ? 2 : 1}, {"write", copy ? 3 : 1},
            {"close", copy ? 2 : 1}, {"flush", 1}, {"sync", 1}, {"rename", 3}, {"remove", 3}};
        if (copy) faults["read"] = 2;
        for (const auto& fault : faults) for (int nth = 1; nth <= fault.second; ++nth) {
            Fixture f;
            failure = fault.first; failAt = nth;
            const std::string name = (copy ? "copy " : "write ") + fault.first + " #" + std::to_string(nth);
            const bool saved = copy ? SaveFileTransaction::Copy(f.source, f.path)
                                    : SaveFileTransaction::Write(f.path, "new complete save");
            Check(!saved, name + " must report failure");
            Check(Read(f.path) == "previous complete save" || Read(f.path + ".backup") == "previous complete save",
                  name + " must preserve previous save");
            Check(Read(f.source) == std::string(10000, 's'), name + " must preserve copy source");
            if (fault.first == "rename" && nth == 3)
                Check(Read(f.path + ".pending") == (copy ? std::string(10000, 's') : "new complete save"),
                      name + " must retain completed pending generation");
            Reset();
            Check(SaveFileTransaction::Recover(f.path), name + " restart recovery");
            Check(Read(f.path) == "previous complete save", name + " recovered content");
            Check(SaveFileTransaction::Write(f.path, "retry complete save"), name + " retry");
            Check(Read(f.path) == "retry complete save", name + " retry content");
            ++cases;
        }
    }
    // Terminate after each real rename; recover in a fresh process context.
    for (bool existing : {false, true}) for (int step = 1; step <= (existing ? 3 : 2); ++step) {
        Fixture f;
        if (!existing) std::filesystem::remove(f.path);
        const pid_t child = fork();
        Check(child >= 0, "fork");
        if (child == 0) {
            interruptAfterRename = step;
            SaveFileTransaction::Write(f.path, "new complete save");
            _exit(1);
        }
        int status;
        Check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 77,
              "interruption reached rename");
        Check(SaveFileTransaction::Recover(f.path), "restart recovery after rename");
        Check(Read(f.path) == (existing && step < 3 ? "previous complete save" : "new complete save"),
              "restart selects complete committed generation");
        Check(SaveFileTransaction::Recover(f.path), "recovery is idempotent");
        ++cases;
    }
    {
        Fixture f;
        std::filesystem::remove(f.path);
        failure = "close";
        Check(!SaveFileTransaction::Write(f.path, "uncommitted"), "first save close failure");
        Reset();
        Check(SaveFileTransaction::Recover(f.path), "first save recovery check");
        Check(!std::filesystem::exists(f.path), "never recover uncommitted temporary data");
        Check(Read(f.path + ".temp") == "uncommitted", "retain failed temporary data");
        ++cases;
    }
    {
        Fixture f;
        Check(SaveFileTransaction::Write(f.path, "generation two"), "successful replacement");
        Check(Read(f.path + ".backup") == "previous complete save", "successful replacement retains backup");
        Check(SaveFileTransaction::Copy(f.source, f.path), "successful copy replacement");
        Check(Read(f.path) == std::string(10000, 's'), "complete copied contents");
        Check(SaveFileTransaction::Delete(f.path), "delete");
        Check(SaveFileTransaction::Recover(f.path) && !std::filesystem::exists(f.path), "delete cannot resurrect backup");
        ++cases;
    }
    {
        Fixture f;
        Put(f.path + ".backup", "old backup");
        failure = "remove"; failAt = 3;
        Check(!SaveFileTransaction::Delete(f.path), "failed delete");
        Check(Read(f.path) == "previous complete save", "failed delete preserves primary");
        ++cases;
    }
    {
        Fixture f;
        failure = "rename"; failAt = 3;
        Check(!SaveFileTransaction::Write(f.path, "new complete save"), "leave interrupted commit");
        Reset(); failure = "rename";
        Check(!SaveFileTransaction::Recover(f.path), "failed recovery reports failure");
        Check(Read(f.path + ".backup") == "previous complete save", "failed recovery preserves backup");
        Check(Read(f.path + ".pending") == "new complete save", "failed recovery preserves pending");
        Reset();
        Check(SaveFileTransaction::Recover(f.path), "recovery retry succeeds");
        Check(Read(f.path) == "previous complete save", "recovery retry restores committed save");
        ++cases;
    }
    std::cout << "Save transactions: " << cases << " fault/restart/success cases passed\n";
}
