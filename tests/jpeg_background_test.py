#!/usr/bin/env python3
"""Exercise production JPEG conversion with decoder success and failure."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
src=(root/'third_party/shipwright/soh/soh/ResourceManagerHelpers.cpp').read_text()
fn=src[src.index('extern "C" char* ResourceMgr_LoadJPEG('):src.index('extern "C" char* ResourceMgr_LoadTexOrDListByName(')]
code=r'''
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <climits>
#define STBI_rgb 3
#define STBI_rgb_alpha 4
static bool fail;
static int frees;
static unsigned char* stbi_load_from_memory(const unsigned char*, int, int* w, int* h, int*, int channels) {
 *w=2; *h=1;
 if(fail) return nullptr;
 auto p=(unsigned char*)malloc(2*channels);
 for(int i=0;i<2;i++){p[i*channels]=255;p[i*channels+1]=0;p[i*channels+2]=0;if(channels==4)p[i*channels+3]=255;}
 return p;
}
static const char* stbi_failure_reason(){return "injected failure";}
static void stbi_image_free(void* p){++frees;free(p);}
'''+fn+r'''
int main(){
 char input[4]{};
 fail=true; assert(ResourceMgr_LoadJPEG(input,4)==nullptr);
 fail=false;
 char* result=ResourceMgr_LoadJPEG(input,4); assert(result);
 assert((unsigned char)result[0]==0xf8 && (unsigned char)result[1]==1);
 free(result);
 assert(ResourceMgr_LoadJPEG(input,2)==nullptr); assert(frees==1);
 assert(ResourceMgr_LoadJPEG(nullptr,4)==nullptr);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17','-O0',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
