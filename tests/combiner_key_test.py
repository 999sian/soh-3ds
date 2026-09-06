#!/usr/bin/env python3
"""Check actual combiner-key construction and cache identity under poisoned locals."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'third_party/libultraship/src/fast/interpreter.cpp').read_text()
header = (root / 'third_party/libultraship/include/fast/interpreter.h').read_text()
start = header.index('struct ColorCombinerKey {')
key_type = header[start:header.index('\n};', start)+3]
shader_shift = re.search(r'constexpr size_t SHADER_ID_SHIFT = (\d+);', header).group(1)
start = source.index('    ColorCombinerKey key', source.index('void Interpreter::DeriveTriState()'))
construction = source[start:source.index('\n    ColorCombiner* comb', start)]
start = source.index('ColorCombiner* Interpreter::LookupOrCreateColorCombiner(')
lookup = source[start:source.index('\n}', start)+2]
code = r'''
#include <cassert>
#include <compare>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>
#ifdef KEY_INIT_ONLY
#include <sanitizer/msan_interface.h>
#endif
''' + key_type + r'''
struct RDP { uint64_t combine_mode; };
__attribute__((noinline)) ColorCombinerKey makeKey(uint64_t combine, uint64_t options) {
    RDP rdp{combine};
    RDP* mRdp = &rdp;
    uint64_t cc_options = options;
''' + construction + r'''
    return key;
}
struct ColorCombiner { uint64_t mode = 0, options = 0; };
struct Interpreter {
    std::map<ColorCombinerKey, ColorCombiner> mColorCombinerPool;
    decltype(mColorCombinerPool)::iterator mPrevCombiner = mColorCombinerPool.end();
    unsigned flushes = 0, builds = 0;
    void Flush() { ++flushes; }
    void GenerateCC(ColorCombiner* comb, const ColorCombinerKey& key) {
        ++builds; comb->mode = key.combine_mode; comb->options = key.options;
    }
    ColorCombiner* LookupOrCreateColorCombiner(const ColorCombinerKey& key);
};
''' + lookup + r'''
int main() {
    Interpreter interpreter;
    auto key = makeKey(0x123456789ABCDEFull, 0xFEDCBA9876543210ull);
#ifdef KEY_INIT_ONLY
    __msan_check_mem_is_initialized(&key, sizeof(key));
#endif
    assert(key.combine_mode == 0x123456789ABCDEFull);
    assert(key.options == 0xFEDCBA9876543210ull);
    assert(key.shader_id == 0); // Unused in GenerateCC, but included in map comparison.
#ifdef KEY_INIT_ONLY
    std::puts("Combiner key initialization passed MSan; cache coverage runs separately with ASan/UBSan");
#else
    const auto* original = interpreter.LookupOrCreateColorCombiner(key);
    for (unsigned i = 0; i < 10000; ++i) {
        auto same = makeKey(key.combine_mode, key.options);
        assert(interpreter.LookupOrCreateColorCombiner(same) == original);
    }
    const auto* differentMode = interpreter.LookupOrCreateColorCombiner(makeKey(key.combine_mode ^ 1, key.options));
    const auto* differentOptions = interpreter.LookupOrCreateColorCombiner(makeKey(key.combine_mode, key.options ^ 1));
    assert(differentMode != original && differentOptions != original && differentMode != differentOptions);
    const auto* differentShader = interpreter.LookupOrCreateColorCombiner(
        makeKey(key.combine_mode, key.options ^ (uint64_t{1} << SHADER_SHIFT)));
    assert(differentShader != original && differentShader != differentMode && differentShader != differentOptions);
    assert(interpreter.LookupOrCreateColorCombiner(makeKey(key.combine_mode, key.options)) == original);
    assert(interpreter.mColorCombinerPool.size() == 4);
    assert(interpreter.builds == 4 && interpreter.flushes == 4);
    std::puts("Combiner key fully initialized; identical states reuse cache; mode/options remain distinct");
#endif
}
'''.replace('SHADER_SHIFT', shader_shift)
with tempfile.TemporaryDirectory(prefix='soh-combiner-key-') as directory:
    p = Path(directory)
    (p / 'test.cpp').write_text(code)
    if os.getenv('MSAN'):
        compiler = os.getenv('CXX', 'clang++')
        # Host libstdc++ tree-rebalance code is not MSan-instrumented; even a
        # fully initialized std::map<unsigned, unsigned> control reports there.
        # Check all key bytes with MSan; use ASan/UBSan for the full cache path.
        flags = ['-DKEY_INIT_ONLY', '-fsanitize=memory', '-fno-omit-frame-pointer', '-fPIE', '-pie']
    else:
        compiler = os.getenv('CXX', 'g++')
        # Deterministic poison exposes omitted initialization even without MSan.
        flags = ['-ftrivial-auto-var-init=pattern']
        if os.getenv('SANITIZE'):
            flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
    subprocess.run([compiler, '-std=c++20', '-O1', '-g', *flags,
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
