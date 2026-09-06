#!/usr/bin/env python3
"""Run actual binary loader and custom decoders with a throwing reader boundary.
XML/archive/Resource scaffolding is replaced; production audio code is compiled verbatim.
Requires a C++20 compiler, libvorbisfile development files, and ffmpeg.
"""
import pathlib, subprocess, tempfile, os
root=pathlib.Path(__file__).resolve().parents[1]
source=root/'third_party/shipwright/soh/soh/resource/importer/AudioSampleFactory.cpp'
with tempfile.TemporaryDirectory(prefix='audio-safety-') as directory:
    out=pathlib.Path(directory)
    factory=source.read_text()
    decoders=factory[factory.index('#define DR_WAV_IMPLEMENTATION'):factory.index('namespace SOH {')]
    decoders=decoders.replace('#include <tinyxml2.h>','')
    binary=factory[factory.index('namespace SOH {'):factory.index('std::shared_ptr<Ship::IResource>\nResourceFactoryXMLAudioSampleV0')]+ '\n}\n'
    scaffolding=r'''
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>
#include "soh/resource/type/AudioSample.h"
enum { CODEC_ADPCM, CODEC_S8, CODEC_S16_INMEMORY, CODEC_SMALL_ADPCM, CODEC_REVERB, CODEC_S16, CODEC_OPUS };
namespace Ship {
class BinaryReader {
    std::vector<char> data; size_t pos=0;
public:
    explicit BinaryReader(std::vector<char> bytes):data(std::move(bytes)){}
    uint8_t ReadUByte(){ if(pos==data.size()) throw std::runtime_error("truncated"); return data[pos++]; }
    uint32_t ReadUInt32(){ uint32_t v=0;for(int i=0;i<4;++i)v |= uint32_t(ReadUByte()) << (8*i);return v; }
    int32_t ReadInt32(){return ReadUInt32();}
    int16_t ReadInt16(){auto lo=ReadUByte();return lo | (uint16_t(ReadUByte())<<8);}
};
struct File { std::variant<std::shared_ptr<BinaryReader>> Reader; std::shared_ptr<std::vector<char>> Buffer; };
}
namespace SOH {
class ResourceFactoryBinaryAudioSampleV2 {
public:
    bool FileHasValidFormatAndReader(std::shared_ptr<Ship::File>,std::shared_ptr<Ship::ResourceInitData>){return true;}
    std::shared_ptr<Ship::IResource> ReadResource(std::shared_ptr<Ship::File>,std::shared_ptr<Ship::ResourceInitData>);
};
}
'''
    (out/'test.cpp').write_text(scaffolding+decoders+binary+(root/'tests/audio_safety.cpp').read_text())
    for fmt in ('wav','mp3','ogg','flac'):
        subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','sine=frequency=440:duration=0.03','-ar','32000','-ac','1',str(out/f'tone.{fmt}')],check=True)
    subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','sine=frequency=550:duration=0.03','-ar','44100','-ac','1',str(out/'changed.ogg')],check=True)
    (out/'chain-changed.ogg').write_bytes((out/'tone.ogg').read_bytes()+(out/'changed.ogg').read_bytes())
    subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','anullsrc=r=32000:cl=stereo','-t','140',str(out/'oversized.flac')],check=True)
    for platform in ('desktop','3ds'):
        flags=['-D__3DS__'] if platform=='3ds' else []
        if os.environ.get('AUDIO_SANITIZE'):flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
        subprocess.run(['c++','-std=c++20','-g',*flags,'-I'+str(root/'tests/audio_stubs'),'-I'+str(root/'third_party/shipwright/soh'),'-I'+str(root/'third_party/dr_libs'),str(out/'test.cpp'),str(root/'third_party/shipwright/soh/soh/resource/type/AudioSample.cpp'),'-lvorbisfile','-lvorbis','-logg','-o',str(out/'test')],check=True)
        subprocess.run([str(out/'test'),str(out)],check=True)
        subprocess.run(['c++','-std=c++20',*flags,'-I'+str(root/'tests/audio_stubs'),'-I'+str(root/'third_party/shipwright/soh'),str(root/'tests/audio_initialization.cpp'),str(root/'third_party/shipwright/soh/soh/resource/type/AudioSample.cpp'),'-o',str(out/'initialization')],check=True)
        subprocess.run([str(out/'initialization')],check=True)
