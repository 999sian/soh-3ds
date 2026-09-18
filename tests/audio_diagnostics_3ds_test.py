#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
code=r'''
#include "ship/utils/audio_diagnostics_3ds.h"
#include <cassert>
#include <cstring>
unsigned flags=0;
extern "C" unsigned Soh3dsLoggingFlags() { return flags; }
int main() {
 Soh3dsWriteAudioDiagnostic("disabled\n");
 assert(fopen("audio-perf.log","rb")==nullptr);
 flags=SOH3DS_LOG_GENERAL;
 Soh3dsWriteAudioDiagnostic("synth\n");
 Soh3dsWriteAudioDiagnostic("queue\n");
 auto* f=fopen("audio-perf.log","rb");assert(f);
 char data[64]={};fread(data,1,sizeof(data)-1,f);fclose(f);
 assert(strcmp(data,"synth\nqueue\n")==0);
 flags=0;Soh3dsWriteAudioDiagnostic("disabled\n");
 f=fopen("audio-perf.log","rb");fseek(f,0,SEEK_END);assert(ftell(f)==12);fclose(f);
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-I'+str(root/'third_party/libultraship/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=p,check=True)
print('Audio capture: disabled creates no file; enabled summaries append; disabling stops writes')
