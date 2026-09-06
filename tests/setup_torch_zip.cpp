#include "archive/StoredZip.h"
#include <cassert>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <sys/resource.h>

namespace fs = std::filesystem;
static void Sentinel(const fs::path& path) { std::ofstream(path) << "unrelated"; }
static void AssertSentinel(const fs::path& path) {
    std::ifstream file(path);
    std::string value;
    file >> value;
    assert(value == "unrelated");
}
int main(int argc, char** argv) {
    const std::string output = argv[1];
    {
        StoredZip zip(output);
        zip.CreateArchive();
        zip.AddFile("empty", {});
        zip.AddFile("duplicate", {'a', '\0', 'b'});
        zip.AddFile("duplicate", {'c'});
        zip.AddFile("large", std::vector<char>(1024 * 1024, 'x'));
        zip.Close();
    }
    assert(fs::exists(output)); // Successful finalization transfers ownership.
    const auto partial = output + ".partial";
    {
        StoredZip zip(partial);
        zip.CreateArchive();
        zip.AddFile("payload", std::vector<char>(1024 * 1024, 'x'));
    }
    assert(!fs::exists(partial));
    assert(!fs::exists(partial + ".central.tmp"));

    // Merely constructing a writer cannot claim either existing path.
    const auto untouched = output + ".untouched";
    Sentinel(untouched);
    Sentinel(untouched + ".central.tmp");
    { StoredZip zip(untouched); }
    AssertSentinel(untouched);
    AssertSentinel(untouched + ".central.tmp");

    // A failed payload open must leave the preexisting payload and sidecar alone.
    const auto blocked = output + ".blocked";
    fs::create_directory(blocked);
    Sentinel(fs::path(blocked) / "sentinel");
    Sentinel(blocked + ".central.tmp");
    bool failed = false;
    try { StoredZip zip(blocked); zip.CreateArchive(); }
    catch (const std::exception&) { failed = true; }
    assert(failed);
    AssertSentinel(fs::path(blocked) / "sentinel");
    AssertSentinel(blocked + ".central.tmp");

    // If only the payload was opened, clean it without claiming the blocked sidecar.
    const auto blockedSidecar = output + ".blocked-sidecar";
    fs::create_directory(blockedSidecar + ".central.tmp");
    Sentinel(fs::path(blockedSidecar + ".central.tmp") / "sentinel");
    failed = false;
    try { StoredZip zip(blockedSidecar); zip.CreateArchive(); }
    catch (const std::exception&) { failed = true; }
    assert(failed && !fs::exists(blockedSidecar));
    AssertSentinel(fs::path(blockedSidecar + ".central.tmp") / "sentinel");

    // Force a real final flush error: the local entry and sidecar each fit in 64
    // bytes, but the completed archive does not. Restore the process limit after.
    struct rlimit original{};
    assert(getrlimit(RLIMIT_FSIZE, &original) == 0);
    const auto oldSignal = std::signal(SIGXFSZ, SIG_IGN);
    auto limited = original;
    limited.rlim_cur = 64;
    assert(setrlimit(RLIMIT_FSIZE, &limited) == 0);
    const auto closeFailure = output + ".close-failure";
    failed = false;
    try {
        StoredZip zip(closeFailure);
        zip.CreateArchive();
        zip.AddFile("small", {'x'});
        zip.Close();
    } catch (const std::exception& e) {
        failed = std::string(e.what()) == "Cannot finish extraction archive";
    }
    assert(setrlimit(RLIMIT_FSIZE, &original) == 0);
    std::signal(SIGXFSZ, oldSignal);
    assert(failed);
    assert(!fs::exists(closeFailure));
    assert(!fs::exists(closeFailure + ".central.tmp"));
}
