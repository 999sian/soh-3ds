#!/usr/bin/env python3
"""Compile the real binary font importer; fail nested loads and truncated reads.
ASan checks ownership during unwinding. Successful retries and absent slots are
verified separately from unavailable required sample files.
"""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'third_party/shipwright/soh/soh/resource/importer/AudioSoundFontFactory.cpp').read_text()
binary=s[s.index('namespace SOH {'):s.index('int8_t ResourceFactoryXMLSoundFontV0::MediumStrToInt')]+ '\n}\n'
code=r'''
#include <cassert>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
#include "soh/resource/type/AudioSoundFont.h"
#define BE16SWAP(v) (v)
static int reads, failRead=-1, loads, failLoad=-1;
namespace Ship {
class BinaryReader {
 std::vector<unsigned char> bytes; size_t pos=0;
public:
 BinaryReader(std::vector<unsigned char> b):bytes(b){}
 uint8_t ReadUByte(){if(reads++==failRead)throw std::runtime_error("read failure"); if(pos==bytes.size())throw std::runtime_error("truncated");return bytes[pos++];}
 int8_t ReadInt8(){return ReadUByte();}
 uint16_t ReadUInt16(){auto a=ReadUByte();return a|(ReadUByte()<<8);}
 int16_t ReadInt16(){return ReadUInt16();}
 uint32_t ReadUInt32(){auto a=ReadUInt16();return a|(uint32_t(ReadUInt16())<<16);}
 int32_t ReadInt32(){return ReadUInt32();}
 float ReadFloat(){auto a=ReadUInt32();float f;memcpy(&f,&a,4);return f;}
 std::string ReadString(){auto n=ReadUInt32();std::string s;while(n--)s+=ReadUByte();return s;}
};
struct File { std::variant<std::shared_ptr<BinaryReader>> Reader; };
struct SampleResource : IResource { SOH::Sample sample{}; void* GetRawPointer() override{return &sample;} };
class Context {
public:
 static Context* GetRawInstance(){static Context c;return &c;}
 auto GetResourceManager(){return this;}
 std::shared_ptr<IResource> LoadResourceProcess(const char* path){
  if(!*path)return nullptr;
  if(loads++==failLoad)return nullptr;
  static auto sample=std::make_shared<SampleResource>();return sample;
 }
};
}
namespace SOH {
class ResourceFactoryBinaryAudioSoundFontV2 {
public:
 bool FileHasValidFormatAndReader(std::shared_ptr<Ship::File>,std::shared_ptr<Ship::ResourceInitData>){return true;}
 std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File>,std::shared_ptr<Ship::ResourceInitData>);
};
}
'''
code+=binary+r'''
std::vector<unsigned char> bytes;
void put(unsigned v,int n){while(n--){bytes.push_back(v&255);v>>=8;}}
void str(const char* s){put(strlen(s),4);for(;*s;s++)put(*s,1);}
void env(){put(1,4);put(1,2);put(100,2);}
void sound(bool present=true){put(1,1);str(present?"sample":"");put(0x3f800000,4);}
void fixture(bool present=true){
 bytes.clear();put(3,4);put(0,1);put(0,1);put(0,2);put(0,2);put(0,2);
 put(1,4);put(1,4);put(2,4);
 put(0,1);put(64,1);put(0,1);env();sound(present);
 put(1,1);put(0,1);put(0,1);put(127,1);put(0,1);env();
 put(0,1);put(1,1);sound(present);put(0,1); // optional low/high slots
 put(1,1);sound(present);put(0,1); // real and absent sfx slots
}
std::shared_ptr<SOH::AudioSoundFont> load(){
 reads=loads=0;
 auto f=std::make_shared<Ship::File>();f->Reader=std::make_shared<Ship::BinaryReader>(bytes);
 return std::dynamic_pointer_cast<SOH::AudioSoundFont>(SOH::ResourceFactoryBinaryAudioSoundFontV2().ReadResource(f,std::make_shared<Ship::ResourceInitData>()));
}
int main(){
 fixture();
 auto sf=load();assert(sf && sf->instrumentAddresses[0]->normalNotesSound.sample);
 assert(!sf->instrumentAddresses[0]->lowNotesSound.sample && !sf->instrumentAddresses[0]->highNotesSound.sample);
 assert(sf->soundEffects[1].sample==nullptr && sf->soundEffects[1].tuning==0);
 int totalReads=reads;sf.reset();
 for(failLoad=0;failLoad<3;failLoad++){
  bool rejected=false;try{rejected=!load();}catch(const std::exception&){rejected=true;}
  assert(rejected && "must not publish a partially loaded font");
 }
 failLoad=-1;assert(load()->instrumentAddresses[0]->normalNotesSound.sample);
 for(failRead=0;failRead<totalReads;failRead++){
  bool rejected=false;try{rejected=!load();}catch(const std::exception&){rejected=true;}
  assert(rejected);
 }
 failRead=-1;fixture(false);sf=load();assert(sf && !sf->drumAddresses[0] && !sf->soundEffects[0].sample);
}
'''
with tempfile.TemporaryDirectory(prefix='soh-font-failure-') as d:
 p=Path(d);(p/'ship/resource').mkdir(parents=True)
 stub=(root/'tests/audio_stubs/ship/resource/Resource.h').read_text().replace('virtual ~IResource() = default;', 'virtual ~IResource() = default; virtual void* GetRawPointer(){return nullptr;}')
 (p/'libultraship/libultra').mkdir(parents=True)
 (p/'libultraship/libultra/types.h').write_text((root/'tests/audio_stubs/libultraship/libultra/types.h').read_text()+'\ntypedef float f32; typedef uint16_t u16;\n')
 (p/'ship/resource/Resource.h').write_text(stub);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++20','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I'+str(p),'-I'+str(root/'tests/audio_stubs'),'-I'+str(root/'third_party/shipwright/soh'),str(p/'test.cpp'),str(root/'third_party/shipwright/soh/soh/resource/type/AudioSoundFont.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: font sample failure, retry, optional slots, and exception ownership')
