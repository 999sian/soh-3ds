#!/usr/bin/env python3
"""Verify font-ID-only startup and fallback without eagerly loading samples."""
from pathlib import Path
import subprocess, tempfile, os
root=Path(__file__).resolve().parents[1]
s=(root/'third_party/shipwright/soh/soh/ResourceManagerHelpers.cpp').read_text()
a=s.index('extern "C" int ResourceMgr_GetAudioSoundFontIndex(');b=s.index('\nextern "C" int ResourceMgr_OTRSigCheck',a)
code=r'''
#include <audio_font_metadata_3ds.h>
#include <cassert>
#include <memory>
#include <string>
#include <vector>
static bool oldModel=true,alt=false,meta=false;
static int loads=0;
bool Soh3dsUsesOldProfile(){return oldModel;}
struct SoundFont{int fntIndex=17;};
SoundFont* ResourceMgr_LoadAudioSoundFontByName(const char*){++loads;static SoundFont font;return &font;}
struct File{std::shared_ptr<std::vector<char>> Buffer=std::make_shared<std::vector<char>>(68);};
static auto file=std::make_shared<File>();
namespace Ship {struct Context {
 static Context* GetRawInstance(){static Context c;return &c;}
 auto GetResourceManager(){return this;}
 auto GetArchiveManager(){return this;}
 bool IsAltAssetsEnabled(){return alt;}
 bool HasFile(const std::string&){return meta;}
 auto LoadFileProcess(const char*){return file;}
};}
''' + s[a:b] + r'''
int main(){
 auto& d=*file->Buffer;
 for(int big=0;big<2;big++) {
  d.assign(68,0);d[0]=big;
  auto put=[&](int offset,uint32_t value){for(int i=0;i<4;i++)d[offset+i]=(value>>(8*(big?3-i:i)))&255;};
  put(4,0x4f534654);put(8,2);put(64,37);
  assert(Soh3dsReadFontIndex((uint8_t*)d.data(),d.size())==37);
  assert(Soh3dsReadFontIndex((uint8_t*)d.data(),67)==-1);
  loads=0;assert(ResourceMgr_GetAudioSoundFontIndex("font")==37 && loads==0);
  put(8,3);assert(ResourceMgr_GetAudioSoundFontIndex("font")==17 && loads==1);
  put(8,2);put(64,0xffffffff);assert(Soh3dsReadFontIndex((uint8_t*)d.data(),68)==-1);
  put(64,37);
 }
 oldModel=false;loads=0;assert(ResourceMgr_GetAudioSoundFontIndex("font")==17 && loads==1);
 oldModel=true;alt=true;loads=0;assert(ResourceMgr_GetAudioSoundFontIndex("font")==17 && loads==1);
 alt=false;meta=true;loads=0;assert(ResourceMgr_GetAudioSoundFontIndex("font")==17 && loads==1);
 assert(ResourceMgr_GetAudioSoundFontIndex(nullptr)==-1);
 assert(Soh3dsReadFontIndex(nullptr,68)==-1);
}
'''
with tempfile.TemporaryDirectory(prefix='soh-font-meta-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run([os.environ.get('CXX','g++'),'-std=c++20','-D__3DS__','-I'+str(root/'src/compat3ds'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
