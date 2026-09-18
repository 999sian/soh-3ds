#!/usr/bin/env python3
"""Build the Old/New 3DS vertex benchmark, sharing the ARM11 correctness suite."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('vertex', ROOT / 'tests/arm11_vertex_test.py')
vertex = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vertex)


def main():
    out = ROOT / 'builds/arm11-vertex-benchmark'
    out.mkdir(parents=True, exist_ok=True)
    # Compile references independently so normal production aliasing applies;
    # the test's deliberate byte-layout probes need aliasing disabled only in
    # the harness translation unit. Also time compiler-optimized, disjoint C.
    shared = vertex.harness()
    start = shared.index('__attribute__((noinline)) void vertexReference')
    end = shared.index('static uint32_t rng=', start)
    reference = shared[start:end]
    prefix = shared[:start]
    hoisted = reference.replace('vertexReference(', 'vertexDisjointReference(')
    hoisted = hoisted.replace('const void* input,void* output,',
                              'const void* restrict input,void* restrict output,')
    hoisted = hoisted.replace('const float matrix[4][4]', 'const float matrix[restrict 4][4]')
    (out / 'reference.c').write_text(prefix + reference + hoisted)
    declaration = 'void vertexReference(const void*,void*,const float[4][4],uint32_t);\n'
    declaration += 'void vertexDisjointReference(const void*,void*,const float[4][4],uint32_t);\n'
    declaration += 'void Soh3dsVertexV2Arm11(const void*,void*,const float[4][4],uint32_t);\n'
    declaration += 'void Soh3dsVertexScalarStoresArm11(const void*,void*,const float[4][4],uint32_t);\n'
    shared = shared[:start] + declaration + shared[end:]
    shared = shared.replace('static void vertexCorrectness(void)', 'static void vertexCorrectness(Transform kernel)')
    shared = shared.replace('Soh3dsTransformVerticesArm11(', 'kernel(')
    code = r'''
#define SOH3DS_VERTEX_HARDWARE_BENCHMARK 1
#include <3ds.h>
#include <stdio.h>
#include <sys/stat.h>
typedef void (*Transform)(const void*,void*,const float[4][4],uint32_t);
''' + shared + r'''
static volatile uint32_t checksum;
static uint64_t measure(Transform volatile fn,unsigned count,unsigned offset,unsigned repeats) {
    const void* src=(const uint8_t*)inputs+2*offset;
    void* dst=(uint8_t*)actual+4*offset;
    uint64_t start=svcGetSystemTick();
    for(unsigned i=0;i<repeats;++i)fn(src,dst,matrix,count);
    uint64_t elapsed=svcGetSystemTick()-start;
    union {float f;uint32_t u;} v;v.f=actual[0].x;checksum^=v.u;
    return elapsed;
}
int main(void) {
    gfxInitDefault();consoleInit(GFX_TOP,NULL);
    bool newModel=false;APT_CheckNew3DS(&newModel);osSetSpeedupEnable(newModel);
    const uint32_t initialFpscr=fpscr();
    printf("SoH ARM11 vertex benchmark v3\n%s 3DS / FPSCR %08lx\nChecking exact results...\n",
           newModel?"New":"Old",(unsigned long)initialFpscr);
    gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
    mkdir("sdmc:/3ds",0777);mkdir("sdmc:/3ds/soh",0777);
    FILE* file=fopen("sdmc:/3ds/soh/arm11-vertex-benchmark.csv","w");
    if(file) {
        fprintf(file,"# benchmark=vertex-v3 fpscr=%08lx stage=correctness\n",(unsigned long)initialFpscr);
        fflush(file);
    }
    const Transform variants[]={Soh3dsVertexV2Arm11,Soh3dsTransformVerticesArm11,Soh3dsVertexScalarStoresArm11};
    for(unsigned i=0;i<3 && !failures;++i) {
        rng=0x3d511;
        vertexCorrectness(variants[i]);
    }
    // Check the disjoint optimized C control too; it is used only on buffers
    // satisfying its restrict contract. Compare full records and guards.
    for(unsigned count=0;!failures && count<=68;++count) {
        fill(expected,sizeof(expected));copyBytes(actual,expected,sizeof(actual));
        vertexReference(inputs,expected,matrix,count);
        vertexDisjointReference(inputs,actual,matrix,count);
        check(same(expected,actual,sizeof(actual)));
    }
    printf("Correctness: %lu failures / %lu checks\n",(unsigned long)failures,(unsigned long)comparisons);
    if((initialFpscr & 0x07f79f00u) != 0x03000000u)
        printf("Unsupported inherited FPSCR; native checks skipped.\n");
    if(!file)printf("ERROR: cannot save CSV. Check SD card.\n");
    if(file) {
        fprintf(file,"# model=%s correctness_failures=%lu checks=%lu\n",newModel?"new":"old",
                (unsigned long)failures,(unsigned long)comparisons);
        fprintf(file,"vertices,alignment_case,round,iterations,c_ticks,c_disjoint_ticks,asm_v2_ticks,asm_block_ticks,asm_scalar_ticks\n");
    }
    if(file && !failures) {
        // Time finite transforms representative of positions, not exceptional
        // float slow paths. Printing and SD writes stay outside each interval.
        for(unsigned i=0;i<16;++i)((float*)matrix)[i]=(int)(i%7)/16.0f-0.125f;
        fill(inputs,sizeof(inputs));
        const unsigned counts[]={1,2,4,8,16,32,64,68};
        for(unsigned c=0;c<8;++c)for(unsigned offset=0;offset<2;++offset) {
            unsigned count=counts[c],repeats=131072/count;
            const Transform paths[]={vertexReference,vertexDisjointReference,Soh3dsVertexV2Arm11,
                                     Soh3dsTransformVerticesArm11,Soh3dsVertexScalarStoresArm11};
            for(unsigned i=0;i<5;++i)measure(paths[i],count,offset,256);
            for(unsigned round=0;round<5;++round) {
                uint64_t ticks[5];
                for(unsigned slot=0;slot<5;++slot) {
                    unsigned i=round&1?4-slot:slot;
                    ticks[i]=measure(paths[i],count,offset,repeats);
                }
                fprintf(file,"%u,%u,%u,%u,%llu,%llu,%llu,%llu,%llu\n",count,offset,round,repeats,
                        (unsigned long long)ticks[0],(unsigned long long)ticks[1],
                        (unsigned long long)ticks[2],(unsigned long long)ticks[3],(unsigned long long)ticks[4]);
            }
            fflush(file);
            printf("%u vertices / alignment %u complete\n",count,offset);
            gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
        }
    }
    if(file)fclose(file);
    printf("\n%s\n/3ds/soh/arm11-vertex-benchmark.csv\nSTART to exit\n",
           failures?"FAILED correctness; no timings":file?"Done":"FAILED to save results");
    while(aptMainLoop()) {
        hidScanInput();if(hidKeysDown()&KEY_START)break;
        gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
    }
    gfxExit();return failures || !file;
}
'''
    (out / 'benchmark.c').write_text(code)
    sdk = vertex.DKP / 'devkitARM/bin'
    flags = [*vertex.FLAGS, '-mtp=soft', '-mword-relocations', '-D__3DS__', '-DARM11',
             '-I'+str(ROOT / 'third_party/libultraship/include'),
             '-I'+str(vertex.DKP / 'libctru/include')]
    subprocess.run([str(sdk / 'arm-none-eabi-gcc'), *flags, '-c', str(out / 'reference.c'),
                    '-o', str(out / 'reference.o')], check=True)
    old_asm = ROOT / 'tests/fixtures/vertex_transform_arm11_v2.S'
    subprocess.run([str(sdk / 'arm-none-eabi-gcc'), *flags,
                    '-DSoh3dsTransformVerticesArm11=Soh3dsVertexV2Arm11',
                    '-c', str(old_asm), '-o', str(out / 'vertex-v2.o')], check=True)
    subprocess.run([str(sdk / 'arm-none-eabi-gcc'), *flags,
                    '-DSoh3dsTransformVerticesArm11=Soh3dsVertexScalarStoresArm11',
                    '-DSOH3DS_VERTEX_SCALAR_STORES=1', '-c', str(vertex.ASM),
                    '-o', str(out / 'vertex-scalar.o')], check=True)
    elf = out / 'soh-arm11-vertex-benchmark.elf'
    subprocess.run([str(sdk / 'arm-none-eabi-gcc'), *flags, '-fno-strict-aliasing',
                    '-specs='+str(vertex.DKP / 'devkitARM/arm-none-eabi/lib/3dsx.specs'),
                    str(out / 'benchmark.c'), str(out / 'reference.o'), str(vertex.ASM),
                    str(out / 'vertex-v2.o'), str(out / 'vertex-scalar.o'),
                    '-L'+str(vertex.DKP / 'libctru/lib'), '-lctru', '-lm', '-o', str(elf)], check=True)
    binary = elf.with_suffix('.3dsx')
    subprocess.run([str(vertex.DKP / 'tools/bin/3dsxtool'), str(elf), str(binary)], check=True)
    disasm = subprocess.run([str(sdk / 'arm-none-eabi-objdump'), '-d', str(elf)],
                            capture_output=True, text=True, check=True)
    (out / 'benchmark.disasm').write_text(disasm.stdout)
    manifest = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                for path in out.iterdir() if path.suffix in ('.c', '.elf', '.3dsx')}
    manifest['assembly_sha256'] = hashlib.sha256(vertex.ASM.read_bytes()).hexdigest()
    manifest['assembly_v2_sha256'] = hashlib.sha256(old_asm.read_bytes()).hexdigest()
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(binary)


if __name__ == '__main__':
    main()
