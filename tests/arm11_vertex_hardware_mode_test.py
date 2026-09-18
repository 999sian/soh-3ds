#!/usr/bin/env python3
"""Native benchmark must never disable ARM11's supported RunFast controls.

QEMU implements underflow that real VFP11 delegates to an exception handler.
Audit actual FPSCR writes in the shared native correctness path to catch a
repeat of the hardware dump-21 underflow; still execute the real instructions.
"""
import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('vertex', ROOT / 'tests/arm11_vertex_test.py')
vertex = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vertex)


def main():
    code = vertex.harness()
    # Observe each attempted control-register change without emulating or
    # replacing it. The negative regression is the real shared harness's old
    # sweep through FZ=0 / DN=0 / directed-rounding combinations.
    code = code.replace('static void setFpscr(uint32_t v) {', r'''
static unsigned unsafeModes;
static void setFpscr(uint32_t v) {
    if ((v & 0x07f79f00u) != 0x03000000u) ++unsafeModes;
''')
    code += r'''
void __aeabi_unwind_cpp_pr0(void){}
void __aeabi_unwind_cpp_pr1(void){}
void _start(void) {
    const uint32_t initial=0x03000010u; // FZ/DN, nearest; an existing IXC flag
    __asm__ volatile("vmsr fpscr,%0"::"r"(initial):"memory");
    vertexCorrectness();
    // VFP comparison condition flags are caller-saved under AAPCS; preserve
    // the controls and pre-existing cumulative flags, not final NZCV.
    check((fpscr() & 0x0fffffffu)==initial);
    // A native launch with incompatible inherited controls must report one
    // failure and return before the arithmetic sweep, without trying new modes.
    unsigned savedFailures=failures, savedChecks=comparisons;
    const uint32_t unsupported=0;
    __asm__ volatile("vmsr fpscr,%0"::"r"(unsupported):"memory");
    vertexCorrectness();
    int rejected=failures==savedFailures+1 && comparisons==savedChecks+1 && unsafeModes==0;
    failures=savedFailures;
    check(rejected);
    __asm__ volatile("vmsr fpscr,%0"::"r"(initial):"memory");
    uint32_t result[3]={unsafeModes,failures,comparisons};
    register uint32_t r0 __asm__("r0")=1;
    register void* r1 __asm__("r1")=result;
    register uint32_t r2 __asm__("r2")=sizeof(result);
    __asm__ volatile("mov r7,#4\nsvc #0":"+r"(r0):"r"(r1),"r"(r2):"r7","memory");
    r0=failures!=0 || unsafeModes!=0;
    __asm__ volatile("mov r7,#1\nsvc #0"::"r"(r0):"r7","memory");
    __builtin_unreachable();
}
'''
    with tempfile.TemporaryDirectory(prefix='soh-vertex-native-mode-') as temp:
        out = Path(temp)
        (out / 'test.c').write_text(code)
        subprocess.run([str(vertex.DKP / 'devkitARM/bin/arm-none-eabi-gcc'), *vertex.FLAGS,
                        '-DSOH3DS_VERTEX_HARDWARE_BENCHMARK=1', '-fno-strict-aliasing', '-fno-builtin',
                        '-I'+str(ROOT / 'third_party/libultraship/include'), '-nostdlib',
                        '-Wl,-e,_start', '-Wl,-Ttext=0x10000', str(out / 'test.c'),
                        str(vertex.ASM), '-o', str(out / 'test')], check=True)
        run = subprocess.run(['qemu-arm', '-cpu', 'arm11mpcore', str(out / 'test')],
                             capture_output=True, timeout=60)
        assert len(run.stdout) == 12, (run.returncode, run.stderr.decode())
        unsafe, failures, checks = struct.unpack('<III', run.stdout)
        assert unsafe == 0, f'native harness attempted {unsafe} unsupported FPSCR changes'
        assert run.returncode == 0 and failures == 0, (failures, checks)
        print(f'PASS: native benchmark keeps RunFast FPSCR controls; {checks} exact checks')


if __name__ == '__main__':
    main()
