#!/usr/bin/env python3
from pathlib import Path
import subprocess,hashlib,json
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'builds/dsp-synth-probe-20260909';src=(root/'third_party/shipwright/soh/soh/mixer.c').read_text()
a=src.index('static int16_t resample_table');table=src[a:src.index('};',a)+2]
a=src.index('void aResampleImpl(');body=src[a:src.index('\nvoid aEnvSetup1Impl',a)]
code='''#include <stdint.h>
#include <string.h>
typedef int16_t RESAMPLE_STATE[16];
static int16_t* refMemory;
static struct { uint16_t in,out,nbytes; } rspa;
#define A_INIT 1
#define SOH3DS_AUDIO_GUARD(a,b,c) 1
#define BUF_S16(a) (refMemory+(a)/2)
#define ROUND_UP_16(v) (((v)+15)&~15)
static int16_t clamp16(int32_t x){return x < -32768 ? -32768 : x > 32767 ? 32767 : (int16_t)x;}
'''+table+'\n'+body+'''
void resampleReference(int16_t* memory,uint16_t in,uint16_t out,uint16_t bytes,uint8_t flags,uint16_t pitch,int16_t* state){
refMemory=memory;rspa.in=in;rspa.out=out;rspa.nbytes=bytes;aResampleImpl(flags,pitch,state);
}
const int16_t* productionTable(void){return &resample_table[0][0];}
'''
(out/'resample_reference.c').write_text(code)
subprocess.run(['cc','-std=c11','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-c',str(out/'resample_reference.c'),'-o',str(out/'resample_reference.o')],check=True)
for stem in ('resample_probe','resample_buffer_probe','resample_state_probe','resample_full_state_probe'):
 subprocess.run(['c++','-std=c++17','-O2','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(root/'research/dsp/teakra/include'),'-I'+str(root/'research/dsp/teakra/src'),str(here/(stem+'.cpp')),str(out/'resample_reference.o'),str(out/'teakra-build/src/libteakra.a'),'-pthread','-o',str(out/stem)],check=True)
 result=subprocess.run([str(out/stem)],check=False,capture_output=True,text=True);print(result.stdout,end='',flush=True);print(result.stderr,end='',flush=True);result.check_returncode();(out/(stem+'-result.txt')).write_text(result.stdout)
(out/'resample-reference.json').write_text(json.dumps({'body_sha256':hashlib.sha256(body.encode()).hexdigest(),'table_sha256':hashlib.sha256(table.encode()).hexdigest()},indent=2)+'\n')
