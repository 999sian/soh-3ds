#!/usr/bin/env python3
"""Run the production audio clamp on ARM11 emulation against scalar semantics."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'third_party/shipwright/soh/soh/mixer.c').read_text()
a=s.index('static inline int16_t clamp16(');b=s.index('\nstatic inline int32_t clamp32',a)
code='#include <stdint.h>\n#include <arm_acle.h>\n'+s[a:b]+r'''
__attribute__((noinline)) int16_t actual(int32_t x) { return clamp16(x); }
static int check(int32_t x) {
 int32_t want=x < -32768 ? -32768 : (x > 32767 ? 32767 : x);
 return actual(x) != want;
}
void _start(void) {
 int fail=0;
 int32_t edges[]={(-2147483647-1),2147483647,-32769,-32768,-32767,-1,0,1,32766,32767,32768};
 for(unsigned i=0;i<sizeof(edges)/sizeof(edges[0]);i++) fail |= check(edges[i]);
 for(int x=-100000;x<=100000;x++) fail |= check(x);
 uint32_t state=123;
 for(unsigned i=0;i<1000000;i++) { state=state*1664525u+1013904223u; fail |= check((int32_t)state); }
 register int result __asm__("r0")=fail;
 __asm__ volatile("mov r7,#1\nsvc #0"::"r"(result):"r7","memory");
 __builtin_unreachable();
}
'''
sdk=Path('/home/sian/dkp-root/opt/devkitpro/devkitARM/bin')
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run([str(sdk/'arm-none-eabi-gcc'),'-Os','-D__3DS__','-march=armv6k','-mfloat-abi=soft','-nostdlib','-Wl,-e,_start','-Wl,-Ttext=0x10000',str(p/'test.c'),'-o',str(p/'test')],check=True)
 asm=subprocess.check_output([str(sdk/'arm-none-eabi-objdump'),'-d',str(p/'test')],text=True)
 assert '\tssat\t' in asm,'audio clamp still lacks ARM saturation instruction'
 subprocess.run(['qemu-arm','-cpu','arm11mpcore',str(p/'test')],check=True)
print('ARM11 SSAT exactly matches scalar clamp: boundaries, 200001 consecutive and 1000000 random inputs')
