#!/usr/bin/env python3
"""Exercise the real 3DS logger initializer: synchronous logging needs no queue."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'third_party/libultraship/src/ship/Context.cpp').read_text()
start = source.index('bool Context::InitLogging(')
end = source.index('\nbool Context::InitConfiguration()', start)
method = source[start:end]
preamble = r'''
#define __3DS__
#define SPDLOG_HEADER_ONLY
#define FMT_HEADER_ONLY
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <cassert>
#include <iostream>
class Context {
public:
    bool InitLogging(spdlog::level::level_enum, spdlog::level::level_enum);
    std::shared_ptr<spdlog::logger> GetLogger() { return mLogger; }
    std::string GetName() { return "test-3ds"; }
    std::shared_ptr<spdlog::logger> mLogger;
};
'''
main = r'''
int main() {
    assert(!spdlog::thread_pool());
    Context c;
    assert(c.InitLogging(spdlog::level::debug, spdlog::level::info));
    assert(c.GetLogger());
    assert(!spdlog::thread_pool());
    auto first = c.GetLogger();
    assert(c.InitLogging(spdlog::level::debug, spdlog::level::info));
    assert(c.GetLogger() == first);
    c.GetLogger()->info("synchronous logging preserved");
    spdlog::shutdown();
}
'''
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d / 'test.cpp').write_text(preamble + method + main)
    subprocess.run(['c++', '-std=c++17', '-pthread', str(d / 'test.cpp'), '-o', str(d / 'test')], check=True)
    result = subprocess.run([str(d / 'test')], check=True, capture_output=True, text=True)
    assert 'synchronous logging preserved' in result.stderr
print('3DS logger has no unused async queue; logging and repeated initialization pass')
