#!/usr/bin/env python3
"""Automated tests for 3DS GPU command budget safeguards."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

CPP_PROBE = r"""#include <platform/3ds/include/gpu_command_budget_3ds.hpp>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

int main() {
    using namespace mk64_3ds;

    // 1. Verify constants
    static_assert(kGpuCommandBufferBytes == 1048576, "kGpuCommandBufferBytes must be 1048576");
    static_assert(kGpuCommandTailWords == 65536, "kGpuCommandTailWords must be 65536");
    static_assert(kGpuCommandDrawWords == 4096, "kGpuCommandDrawWords must be 4096");

    assert(kGpuCommandBufferBytes == 1048576);
    assert(kGpuCommandTailWords == 65536);
    assert(kGpuCommandDrawWords == 4096);

    // 2. Invariants of GpuCommandRoom
    assert(GpuCommandRoom(100, 70, 30) == true);
    assert(GpuCommandRoom(100, 70, 31) == false);
    assert(GpuCommandRoom(100, 101, 0) == false);

    // Additional boundary tests
    assert(GpuCommandRoom(100, 100, 0) == true);
    assert(GpuCommandRoom(100, 100, 1) == false);
    assert(GpuCommandRoom(0, 0, 0) == true);
    assert(GpuCommandRoom(0, 0, 1) == false);
    assert(GpuCommandRoom(0, 1, 0) == false);

    // Overflow protection tests
    constexpr uint32_t u32Max = std::numeric_limits<uint32_t>::max();
    assert(GpuCommandRoom(100, 70, u32Max) == false);
    assert(GpuCommandRoom(100, 70, u32Max - 10) == false);
    assert(GpuCommandRoom(100, u32Max, 0) == false);
    assert(GpuCommandRoom(100, u32Max, 1) == false);
    assert(GpuCommandRoom(100, u32Max, u32Max) == false);
    assert(GpuCommandRoom(u32Max, u32Max - 10, 10) == true);
    assert(GpuCommandRoom(u32Max, u32Max - 10, 11) == false);
    assert(GpuCommandRoom(u32Max, 0, u32Max) == true);
    assert(GpuCommandRoom(u32Max, 1, u32Max) == false);

    // 3. Exception handling: throwing GpuCommandPressure and catching it as std::length_error and std::exception
    static_assert(std::is_base_of<std::length_error, GpuCommandPressure>::value,
                  "GpuCommandPressure must derive from std::length_error");
    static_assert(std::is_base_of<std::exception, GpuCommandPressure>::value,
                  "GpuCommandPressure must derive from std::exception");

    bool caughtLengthError = false;
    try {
        throw GpuCommandPressure();
    } catch (const std::length_error& e) {
        caughtLengthError = true;
        assert(std::string(e.what()).find("command buffer pressure") != std::string::npos);
    } catch (...) {
        assert(false && "Failed to catch GpuCommandPressure as std::length_error");
    }
    assert(caughtLengthError);

    bool caughtStdException = false;
    try {
        throw GpuCommandPressure();
    } catch (const std::exception& e) {
        caughtStdException = true;
        assert(std::string(e.what()).find("command buffer pressure") != std::string::npos);
    } catch (...) {
        assert(false && "Failed to catch GpuCommandPressure as std::exception");
    }
    assert(caughtStdException);

    std::cout << "gpu_command_budget_3ds_test: PASS\n";
    return 0;
}
"""


def main():
    cxx = os.environ.get("CXX", "g++")
    with tempfile.TemporaryDirectory(prefix="gpu-cmd-budget-") as temp_dir:
        temp_path = Path(temp_dir)
        source_file = temp_path / "probe.cpp"
        binary_file = temp_path / "probe"

        source_file.write_text(CPP_PROBE)

        compile_cmd = [
            cxx,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{ROOT}",
            str(source_file),
            "-o",
            str(binary_file),
        ]
        subprocess.run(compile_cmd, check=True)
        subprocess.run([str(binary_file)], check=True)


if __name__ == "__main__":
    main()
