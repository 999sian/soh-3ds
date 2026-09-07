#!/usr/bin/env python3
"""Compile the production reachability filter; check semantics and linear work."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "third_party/shipwright/soh/soh/Enhancements/randomizer/3drando/fill.cpp").read_text()
body = source.split("} while (gals.logicUpdated);", 1)[1].split("// Create the playthrough", 1)[0]
program = r"""
#include <algorithm>
#include <bitset>
#include <vector>
#include <cassert>
#include <cstddef>
constexpr size_t RC_MAX = 1024;
constexpr int RG_NONE = 0;
static size_t comparisons = 0;
struct RandomizerCheck {
    size_t value;
    operator size_t() const { return value; }
    bool operator==(RandomizerCheck other) const { ++comparisons; return value == other.value; }
};
struct Location { int item = 0; int GetPlacedRandomizerGet() { return item; } };
struct Context {
    Location locations[RC_MAX];
    Location* GetItemLocation(RandomizerCheck loc) { return &locations[loc.value]; }
};
struct Search { std::vector<RandomizerCheck> accessibleLocations; };
std::vector<RandomizerCheck> filter(Search gals, const std::vector<RandomizerCheck>& targetLocations,
                                  Context* ctx, bool calculatingAvailableChecks) {
""" + body + r"""
int main() {
    Context ctx;
    for (bool available : {false, true}) {
        for (int mode = 0; mode < 4; ++mode) {
            Search gals;
            std::vector<RandomizerCheck> targets, expected;
            for (size_t i = 0; i < RC_MAX; ++i) {
                size_t value = RC_MAX - 1 - i;
                gals.accessibleLocations.push_back({value});
                ctx.locations[value].item = value % 7 == 0;
                if (mode == 1 || (mode == 2 && value % 2 == 0) || (mode == 3 && value == RC_MAX - 1)) {
                    targets.push_back({value});
                    targets.push_back({value}); // duplicates must not change order or multiplicity
                }
            }
            for (auto loc : gals.accessibleLocations) {
                if ((!available && ctx.locations[loc.value].item) ||
                    std::find(targets.begin(), targets.end(), loc) != targets.end()) expected.push_back(loc);
            }
            comparisons = 0;
            auto actual = filter(gals, targets, &ctx, available);
            auto filterComparisons = comparisons;
            assert(actual == expected);
            assert(filterComparisons <= 4 * (gals.accessibleLocations.size() + targets.size()));
        }
    }
}
"""
with tempfile.TemporaryDirectory() as temp:
    cpp = Path(temp) / "test.cpp"
    cpp.write_text(program)
    exe = Path(temp) / "test"
    subprocess.run(["c++", "-std=c++20", "-O2", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("Reachability filter: ordered results, filled checks, tracker mode, duplicates, boundaries and linear work passed")
