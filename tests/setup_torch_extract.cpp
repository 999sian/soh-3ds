#include "setup/torch_adapter.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/resource.h>
#include <malloc.h>
static size_t peakAllocated = 0;

static bool TraceProgress(const char* stage, size_t done, size_t total, void*) {
    const auto allocation = mallinfo2();
    peakAllocated = std::max(peakAllocated, allocation.uordblks + allocation.hblkhd);
    if (!std::getenv("SETUP_TORCH_TRACE_RSS")) return true;
    static long previous = 0;
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    if (usage.ru_maxrss > previous + 2048) {
        previous = usage.ru_maxrss;
        std::cerr << "RSS " << previous << ' ' << done << '/' << total << ' ' << stage << '\n';
    }
    return true;
}
static bool CancelDuringExport(const char* stage, size_t, size_t, void*) {
    return std::string(stage) != "Writing assets";
}
int main(int argc, char** argv) {
    if (argc != 5 && argc != 6) return 2;
    std::string output, error;
    if (argc == 6) {
        if (Soh3dsSetup::ExtractRom(argv[1], argv[2], argv[3], argv[4], CancelDuringExport, nullptr, output, error) ||
            !output.empty() || error != "Extraction cancelled") return 3;
        if (Soh3dsSetup::ExtractRom(argv[1], "/missing-codex-metadata", argv[3], argv[4], nullptr, nullptr, output, error) ||
            !output.empty() || error.empty()) return 4;
        // The next successful run is in this same process: globals/caches from both
        // interrupted and failed extraction must have been released by RAII.
    }
    if (!Soh3dsSetup::ExtractRom(argv[1], argv[2], argv[3], argv[4], TraceProgress, nullptr, output, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    const auto remaining = mallinfo2();
    std::cerr << "sampled_peak_allocated_bytes=" << peakAllocated << " remaining_allocated_bytes=" << (remaining.uordblks + remaining.hblkhd) << "\n";
    std::cout << output << '\n';
}
