#!/usr/bin/env python3
"""Automated tests for 60 FPS 3DS render policy optimizations."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

CPP_PROBE = r"""#include <platform/3ds/include/render_policy_3ds.hpp>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

int main() {
    using namespace mk64_3ds;

    // =========================================================================
    // 1. NormalizeRenderScale clamping (50..100) and 5% step normalization
    // =========================================================================
    // Compile-time constexpr checks
    static_assert(NormalizeRenderScale(0) == 50, "0 clamped to 50");
    static_assert(NormalizeRenderScale(49) == 50, "49 clamped to 50");
    static_assert(NormalizeRenderScale(50) == 50, "50 clamped/rounded to 50");
    static_assert(NormalizeRenderScale(52) == 50, "52 rounded to 50");
    static_assert(NormalizeRenderScale(53) == 55, "53 rounded to 55");
    static_assert(NormalizeRenderScale(75) == 75, "75 stays 75");
    static_assert(NormalizeRenderScale(97) == 95, "97 rounded to 95");
    static_assert(NormalizeRenderScale(98) == 100, "98 rounded to 100");
    static_assert(NormalizeRenderScale(100) == 100, "100 stays 100");
    static_assert(NormalizeRenderScale(101) == 100, "101 clamped to 100");
    static_assert(NormalizeRenderScale(200) == 100, "200 clamped to 100");

    // Clamping below 50
    assert(NormalizeRenderScale(-1000) == 50);
    assert(NormalizeRenderScale(-1) == 50);
    assert(NormalizeRenderScale(0) == 50);
    assert(NormalizeRenderScale(10) == 50);
    assert(NormalizeRenderScale(40) == 50);
    assert(NormalizeRenderScale(49) == 50);

    // Clamping above 100
    assert(NormalizeRenderScale(101) == 100);
    assert(NormalizeRenderScale(102) == 100);
    assert(NormalizeRenderScale(105) == 100);
    assert(NormalizeRenderScale(150) == 100);
    assert(NormalizeRenderScale(200) == 100);
    assert(NormalizeRenderScale(10000) == 100);

    // 5% step normalization in range [50..100]
    for (long percent = 50; percent <= 100; ++percent) {
        uint8_t normalized = NormalizeRenderScale(percent);
        assert(normalized >= 50 && normalized <= 100);
        assert(normalized % 5 == 0);
    }

    // Exact threshold transitions
    assert(NormalizeRenderScale(50) == 50);
    assert(NormalizeRenderScale(51) == 50);
    assert(NormalizeRenderScale(52) == 50);
    assert(NormalizeRenderScale(53) == 55);
    assert(NormalizeRenderScale(54) == 55);
    assert(NormalizeRenderScale(55) == 55);
    assert(NormalizeRenderScale(56) == 55);
    assert(NormalizeRenderScale(57) == 55);
    assert(NormalizeRenderScale(58) == 60);

    assert(NormalizeRenderScale(72) == 70);
    assert(NormalizeRenderScale(73) == 75);
    assert(NormalizeRenderScale(74) == 75);
    assert(NormalizeRenderScale(75) == 75);
    assert(NormalizeRenderScale(76) == 75);
    assert(NormalizeRenderScale(77) == 75);
    assert(NormalizeRenderScale(78) == 80);

    assert(NormalizeRenderScale(92) == 90);
    assert(NormalizeRenderScale(93) == 95);
    assert(NormalizeRenderScale(97) == 95);
    assert(NormalizeRenderScale(98) == 100);
    assert(NormalizeRenderScale(99) == 100);
    assert(NormalizeRenderScale(100) == 100);

    std::cout << "PASS: NormalizeRenderScale clamping and 5% steps\n";

    // =========================================================================
    // 2. RenderScaleFromTouch coordinate mapping
    // =========================================================================
    // Compile-time checks
    static_assert(RenderScaleFromTouch(0) == 50, "Left boundary clamp to 50");
    static_assert(RenderScaleFromTouch(146) == 50, "Touch min maps to 50");
    static_assert(RenderScaleFromTouch(196) == 75, "Touch mid maps to 75");
    static_assert(RenderScaleFromTouch(246) == 100, "Touch max maps to 100");
    static_assert(RenderScaleFromTouch(320) == 100, "Right boundary clamp to 100");

    // Clamping boundaries
    assert(RenderScaleFromTouch(-100) == 50);
    assert(RenderScaleFromTouch(0) == 50);
    assert(RenderScaleFromTouch(100) == 50);
    assert(RenderScaleFromTouch(145) == 50);
    assert(RenderScaleFromTouch(146) == 50);

    assert(RenderScaleFromTouch(246) == 100);
    assert(RenderScaleFromTouch(247) == 100);
    assert(RenderScaleFromTouch(280) == 100);
    assert(RenderScaleFromTouch(320) == 100);
    assert(RenderScaleFromTouch(1000) == 100);

    // Intermediate mappings across the slider
    assert(RenderScaleFromTouch(150) == 50);
    assert(RenderScaleFromTouch(151) == 55);
    assert(RenderScaleFromTouch(155) == 55);
    assert(RenderScaleFromTouch(156) == 55);
    assert(RenderScaleFromTouch(160) == 55);
    assert(RenderScaleFromTouch(161) == 60);
    assert(RenderScaleFromTouch(171) == 65);
    assert(RenderScaleFromTouch(181) == 70);
    assert(RenderScaleFromTouch(191) == 75);
    assert(RenderScaleFromTouch(196) == 75);
    assert(RenderScaleFromTouch(201) == 80);
    assert(RenderScaleFromTouch(211) == 85);
    assert(RenderScaleFromTouch(221) == 90);
    assert(RenderScaleFromTouch(231) == 95);
    assert(RenderScaleFromTouch(241) == 100);
    assert(RenderScaleFromTouch(246) == 100);

    // Every output must be in [50, 100] and multiple of 5
    for (int x = -50; x <= 400; ++x) {
        uint8_t scale = RenderScaleFromTouch(x);
        assert(scale >= 50 && scale <= 100);
        assert(scale % 5 == 0);
    }

    std::cout << "PASS: RenderScaleFromTouch coordinate mapping\n";

    // =========================================================================
    // 3. RenderDistanceEnd handling of normal values, NAN, INFINITY, and presets
    // =========================================================================
    constexpr float kEpsilon = 1e-5f;

    // Normal values with presets 0 and 1
    assert(std::fabs(RenderDistanceEnd(1000.0f, 0) - 500.0f) < kEpsilon); // preset 0 = 50%
    assert(std::fabs(RenderDistanceEnd(1000.0f, 1) - 750.0f) < kEpsilon); // preset 1 = 75%
    assert(std::fabs(RenderDistanceEnd(8000.0f, 0) - 4000.0f) < kEpsilon);
    assert(std::fabs(RenderDistanceEnd(8000.0f, 1) - 6000.0f) < kEpsilon);
    assert(std::fabs(RenderDistanceEnd(2500.0f, 0) - 1250.0f) < kEpsilon);
    assert(std::fabs(RenderDistanceEnd(2500.0f, 1) - 1875.0f) < kEpsilon);

    // Preset boundaries (< 0 or >= 2 -> 0.0f)
    assert(RenderDistanceEnd(1000.0f, -1) == 0.0f);
    assert(RenderDistanceEnd(1000.0f, -100) == 0.0f);
    assert(RenderDistanceEnd(1000.0f, 2) == 0.0f);
    assert(RenderDistanceEnd(1000.0f, 3) == 0.0f);
    assert(RenderDistanceEnd(1000.0f, 99) == 0.0f);

    // Non-positive courseFar (<= 0 -> 0.0f)
    assert(RenderDistanceEnd(0.0f, 0) == 0.0f);
    assert(RenderDistanceEnd(0.0f, 1) == 0.0f);
    assert(RenderDistanceEnd(-1.0f, 0) == 0.0f);
    assert(RenderDistanceEnd(-1000.0f, 1) == 0.0f);

    // NAN handling -> returns 0.0f
    const float qNaN = std::numeric_limits<float>::quiet_NaN();
    const float sNaN = std::numeric_limits<float>::signaling_NaN();
    assert(RenderDistanceEnd(qNaN, 0) == 0.0f);
    assert(RenderDistanceEnd(qNaN, 1) == 0.0f);
    assert(RenderDistanceEnd(qNaN, -1) == 0.0f);
    assert(RenderDistanceEnd(qNaN, 2) == 0.0f);
    assert(RenderDistanceEnd(sNaN, 0) == 0.0f);
    assert(RenderDistanceEnd(sNaN, 1) == 0.0f);

    // INFINITY handling -> returns 0.0f
    const float posInf = std::numeric_limits<float>::infinity();
    const float negInf = -std::numeric_limits<float>::infinity();
    assert(RenderDistanceEnd(posInf, 0) == 0.0f);
    assert(RenderDistanceEnd(posInf, 1) == 0.0f);
    assert(RenderDistanceEnd(posInf, -1) == 0.0f);
    assert(RenderDistanceEnd(posInf, 2) == 0.0f);
    assert(RenderDistanceEnd(negInf, 0) == 0.0f);
    assert(RenderDistanceEnd(negInf, 1) == 0.0f);

    std::cout << "PASS: RenderDistanceEnd normal values, NAN, INFINITY, presets\n";

    // =========================================================================
    // 4. CullDistantTriangle conditions
    // =========================================================================
    // Compile-time static checks
    static_assert(CullDistantTriangle(100.0f, 100.0f, 100.0f, 50.0f, false, true) == true,
                  "All vertices > end, depth test on -> culled");
    static_assert(CullDistantTriangle(50.0f, 100.0f, 100.0f, 50.0f, false, true) == false,
                  "Vertex a <= end -> kept");
    static_assert(CullDistantTriangle(100.0f, 50.0f, 100.0f, 50.0f, false, true) == false,
                  "Vertex b <= end -> kept");
    static_assert(CullDistantTriangle(100.0f, 100.0f, 50.0f, 50.0f, false, true) == false,
                  "Vertex c <= end -> kept");
    static_assert(CullDistantTriangle(100.0f, 100.0f, 100.0f, 50.0f, true, true) == false,
                  "rectangle == true -> kept");
    static_assert(CullDistantTriangle(100.0f, 100.0f, 100.0f, 50.0f, false, false) == false,
                  "depthTest == false -> kept");
    static_assert(CullDistantTriangle(100.0f, 100.0f, 100.0f, 0.0f, false, true) == false,
                  "end == 0 -> kept");

    // All 3 vertices exceed end and depth testing is true -> culled (returns true)
    assert(CullDistantTriangle(50.01f, 50.01f, 50.01f, 50.0f, false, true) == true);
    assert(CullDistantTriangle(100.0f, 200.0f, 300.0f, 50.0f, false, true) == true);
    assert(CullDistantTriangle(1000.0f, 1000.0f, 1000.0f, 500.0f, false, true) == true);

    // At least 1 vertex <= end -> kept (returns false)
    assert(CullDistantTriangle(50.0f, 100.0f, 100.0f, 50.0f, false, true) == false);
    assert(CullDistantTriangle(49.9f, 100.0f, 100.0f, 50.0f, false, true) == false);
    assert(CullDistantTriangle(0.0f, 100.0f, 100.0f, 50.0f, false, true) == false);
    assert(CullDistantTriangle(-10.0f, 100.0f, 100.0f, 50.0f, false, true) == false);

    assert(CullDistantTriangle(100.0f, 50.0f, 100.0f, 50.0f, false, true) == false);
    assert(CullDistantTriangle(100.0f, 49.9f, 100.0f, 50.0f, false, true) == false);
    assert(CullDistantTriangle(100.0f, 0.0f, 100.0f, 50.0f, false, true) == false);

    assert(CullDistantTriangle(100.0f, 100.0f, 50.0f, 50.0f, false, true) == false);
    assert(CullDistantTriangle(100.0f, 100.0f, 49.9f, 50.0f, false, true) == false);
    assert(CullDistantTriangle(100.0f, 100.0f, -5.0f, 50.0f, false, true) == false);

    // Multiple or all vertices <= end -> kept (returns false)
    assert(CullDistantTriangle(40.0f, 40.0f, 100.0f, 50.0f, false, true) == false);
    assert(CullDistantTriangle(40.0f, 100.0f, 40.0f, 50.0f, false, true) == false);
    assert(CullDistantTriangle(100.0f, 40.0f, 40.0f, 50.0f, false, true) == false);
    assert(CullDistantTriangle(40.0f, 40.0f, 40.0f, 50.0f, false, true) == false);
    assert(CullDistantTriangle(0.0f, 0.0f, 0.0f, 50.0f, false, true) == false);

    // 2D rectangles (rectangle == true) -> kept (returns false)
    assert(CullDistantTriangle(100.0f, 100.0f, 100.0f, 50.0f, true, true) == false);
    assert(CullDistantTriangle(500.0f, 500.0f, 500.0f, 50.0f, true, true) == false);
    assert(CullDistantTriangle(1000.0f, 1000.0f, 1000.0f, 10.0f, true, true) == false);

    // Depth test disabled (depthTest == false) -> kept (returns false)
    assert(CullDistantTriangle(100.0f, 100.0f, 100.0f, 50.0f, false, false) == false);
    assert(CullDistantTriangle(500.0f, 500.0f, 500.0f, 50.0f, false, false) == false);
    assert(CullDistantTriangle(1000.0f, 1000.0f, 1000.0f, 10.0f, false, false) == false);

    // end == 0 -> kept (returns false)
    assert(CullDistantTriangle(100.0f, 100.0f, 100.0f, 0.0f, false, true) == false);
    assert(CullDistantTriangle(1000.0f, 1000.0f, 1000.0f, 0.0f, false, true) == false);

    // Negative end -> kept (returns false)
    assert(CullDistantTriangle(100.0f, 100.0f, 100.0f, -10.0f, false, true) == false);

    std::cout << "PASS: CullDistantTriangle all conditions\n";

    std::cout << "render_policy_3ds_test: PASS\n";
    return 0;
}
"""


def main():
    cxx = os.environ.get("CXX", "g++")
    with tempfile.TemporaryDirectory(prefix="render-policy-") as temp_dir:
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
