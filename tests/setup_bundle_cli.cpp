#include "setup/setup_bundle.h"
#include <cstdlib>
#include <iostream>

static bool Progress(const char*, size_t done, size_t, void* user) {
    return !user || done == 0;
}

int main(int argc, char** argv) {
    if (argc != 6) return 2;
    std::string error;
    void* cancel = std::string(argv[5]) == "cancel" ? &error : nullptr;
    const bool ok = std::string(argv[1]) == "metadata"
        ? Soh3dsSetup::UnpackMetadata(argv[2], argv[3], argv[4], Progress, cancel, error)
        : Soh3dsSetup::InflateGzipFile(argv[2], argv[3], std::strtoull(argv[4], nullptr, 10), Progress, cancel, error);
    if (!ok) std::cerr << error << '\n';
    return ok ? 0 : 1;
}
