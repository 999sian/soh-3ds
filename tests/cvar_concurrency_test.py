#!/usr/bin/env python3
"""Exercise actual CVar access during audio/menu overlap, including map growth."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'third_party/libultraship/src/ship/config/ConsoleVariable.cpp').read_text()

def method(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]

code = r'''
#include "ship/config/ConsoleVariable.h"
#include <atomic>
#include <thread>
#include <vector>
#include <cassert>
#include <cstdio>
namespace Ship {
ConsoleVariable::ConsoleVariable() {}
ConsoleVariable::~ConsoleVariable() {}
''' + '\n'.join(method(s) for s in (
    'const CVar* ConsoleVariable::Peek(',
    'std::shared_ptr<CVar> ConsoleVariable::Get(',
    'int32_t ConsoleVariable::GetInteger(',
    'float ConsoleVariable::GetFloat(',
    'Color_RGBA8 ConsoleVariable::GetColor(',
    'Color_RGB8 ConsoleVariable::GetColor24(',
    'void ConsoleVariable::SetInteger(',
    'void ConsoleVariable::SetFloat(',
    'void ConsoleVariable::SetColor(',
    'void ConsoleVariable::SetColor24(',
    'void ConsoleVariable::RegisterInteger(',
)) + r'''
}
int main() {
    Ship::ConsoleVariable cv;
    cv.SetInteger("gEnhancements.MirroredWorld", 1);
    std::atomic<bool> go{false}, done{false};
    std::atomic<unsigned> reads{0};
    std::vector<std::thread> readers;
    for (int n = 0; n < 4; ++n) readers.emplace_back([&] {
        while (!go.load()) std::this_thread::yield();
        do {
            // The setting and fallback are both 1: a different value proves
            // a corrupt lookup, not a legitimate update interleaving.
            assert(cv.GetInteger("gEnhancements.MirroredWorld", 1) == 1);
            const auto i = cv.GetInteger("changing", -1);
            assert(i == -1 || i == 12345);
            const auto f = cv.GetFloat("changing", -1);
            assert(f == -1 || f == 1.5f);
            const auto c = cv.GetColor("changing", {9, 9, 9, 9});
            assert((c.r == 9 && c.g == 9 && c.b == 9 && c.a == 9) ||
                   (c.r == 1 && c.g == 2 && c.b == 3 && c.a == 4) ||
                   (c.r == 5 && c.g == 6 && c.b == 7 && c.a == 255));
            const auto rgb = cv.GetColor24("changing", {9, 9, 9});
            assert((rgb.r == 9 && rgb.g == 9 && rgb.b == 9) ||
                   (rgb.r == 1 && rgb.g == 2 && rgb.b == 3) ||
                   (rgb.r == 5 && rgb.g == 6 && rgb.b == 7));
            ++reads;
        } while (!done.load());
    });
    go = true;
    for (int n = 0; n < 40000; ++n) {
        cv.SetInteger(("menu.lazy." + std::to_string(n)).c_str(), n);
        cv.SetInteger("changing", 12345);
        cv.SetFloat("changing", 1.5f);
        cv.SetColor("changing", {1, 2, 3, 4});
        cv.SetColor24("changing", {5, 6, 7});
        cv.RegisterInteger("gEnhancements.MirroredWorld", 0);
    }
    done = true;
    for (auto& thread : readers) thread.join();
    assert(reads > 0);
    std::printf("CVar concurrent growth and typed updates: %u reader iterations passed\n", reads.load());
}
'''
with tempfile.TemporaryDirectory(prefix='soh-cvar-concurrency-') as directory:
    temp = Path(directory)
    (temp / 'test.cpp').write_text(code)
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if os.getenv('SANITIZE') else []
    subprocess.run([os.getenv('CXX', 'g++'), '-std=c++20', '-O1', '-g', '-pthread', *flags,
                    '-I' + str(ROOT / 'third_party/libultraship/include'),
                    str(temp / 'test.cpp'), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test')], check=True, timeout=60)
