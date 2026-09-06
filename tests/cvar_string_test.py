#!/usr/bin/env python3
"""Execute production CVar methods through the spoiler-import ownership cases."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'third_party/libultraship/src/ship/config/ConsoleVariable.cpp').read_text()


def method(signature):
    start = source.index(signature)
    end = source.index('\n}', start) + 2
    return source[start:end]


code = r'''
#include "ship/config/ConsoleVariable.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cstdio>
bool failAllocation = false;
char* testStrdup(const char* value) {
    return failAllocation ? nullptr : ::strdup(value);
}
namespace Ship {
// Only context-backed startup/shutdown are replaced; CVar's real destructor runs.
ConsoleVariable::ConsoleVariable() {}
ConsoleVariable::~ConsoleVariable() {}
#define strdup testStrdup
''' + '\n'.join(method(signature) for signature in (
    'const CVar* ConsoleVariable::Peek(',
    'int32_t ConsoleVariable::GetInteger(',
    'const char* ConsoleVariable::GetString(',
    'void ConsoleVariable::SetInteger(',
    'void ConsoleVariable::SetFloat(',
    'void ConsoleVariable::SetColor(',
    'void ConsoleVariable::SetColor24(',
    'void ConsoleVariable::SetString(',
)) + r'''
#undef strdup
}
int main() {
    Ship::ConsoleVariable cv;
    const char* key = "gGeneral.SpoilerLog";
    const char* path = "./Randomizer/24-67-88-11-74.json";
    cv.SetString(key, path);
    const char* borrowed = cv.GetString(key, "");
    // File select borrows a path, import sets that same path and saves JSON,
    // then file select may use the borrowed path to remove the spoiler file.
    cv.SetString(key, borrowed);
    assert(cv.GetString(key, "") == borrowed);
    assert(!std::strcmp(borrowed, path));
    nlohmann::json config;
    config["CVars"][key] = cv.GetString(key, "");
    assert(nlohmann::json::parse(config.dump())["CVars"][key] == path);

    cv.SetString(key, cv.GetString(key, "") + 2);
    assert(!std::strcmp(cv.GetString(key, ""), path + 2));
    cv.SetString("copy", cv.GetString(key, ""));
    cv.SetString(key, "replacement");
    assert(!std::strcmp(cv.GetString("copy", ""), path + 2));
    cv.SetString(key, "");
    assert(!std::strcmp(cv.GetString(key, "fallback"), ""));

    cv.SetInteger("integer", 42);
    cv.SetFloat("float", 1.5f);
    cv.SetColor("color", {1, 2, 3, 4});
    cv.SetColor24("color24", {5, 6, 7});
    for (const char* name : {"integer", "float", "color", "color24"}) {
        cv.SetString(name, "converted");
        assert(!std::strcmp(cv.GetString(name, ""), "converted"));
    }

    borrowed = cv.GetString("copy", "");
    cv.SetInteger("unchanged", 123);
    failAllocation = true;
    cv.SetString("copy", "must not replace old allocation");
    cv.SetString("unchanged", "must not change type");
    failAllocation = false;
    assert(cv.GetString("copy", "") == borrowed);
    assert(!std::strcmp(borrowed, path + 2));
    assert(cv.GetInteger("unchanged", 0) == 123);
    std::puts("CVar string self-assignment, interior aliases, typed replacement, JSON and allocation failure pass");
}
'''
with tempfile.TemporaryDirectory(prefix='soh-cvar-string-') as directory:
    temp = Path(directory)
    (temp / 'test.cpp').write_text(code)
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if os.getenv('SANITIZE') else []
    subprocess.run([os.getenv('CXX', 'g++'), '-std=c++20', '-O1', '-g', *flags,
                    '-I' + str(ROOT / 'third_party/libultraship/include'),
                    str(temp / 'test.cpp'), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test')], check=True)
