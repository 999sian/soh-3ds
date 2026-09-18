#!/usr/bin/env python3
"""Run production O2rArchive lifecycle against real libzip with 3DS branches."""
import os, subprocess, tempfile, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'third_party/libultraship/src/ship/resource/archive/O2rArchive.cpp').read_text()
a = source.index('std::shared_ptr<File> O2rArchive::LoadFile(uint64_t hash)')
b = source.index('std::shared_ptr<File> O2rArchive::LoadFile(const std::string&', a)
source = source[:a] + source[b:]  # Hash lookup belongs to ArchiveManager, not this test.
with tempfile.TemporaryDirectory(prefix='soh-archive-memory-') as directory:
    temp = Path(directory)
    def stub(name, data):
        p = temp / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(data)
    stub('ship/resource/archive/Archive.h', r'''#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <unordered_map>
#include <ship/utils/StrHash64.h>
#include <ship/utils/glob.h>
namespace Ship {
class Archive {
 std::string path;
 std::shared_ptr<std::unordered_map<uint64_t,std::string>> index=std::make_shared<std::unordered_map<uint64_t,std::string>>();
 public: explicit Archive(const std::string& p):path(p){}
 const std::string& GetPath() const { return path; }
 void ReserveIndex(size_t n){index->reserve(n);} void IndexFile(const std::string& p){(*index)[CRC64(p.c_str())]=p;}
 virtual const std::string* HashToString(uint64_t h) const {auto i=index->find(h);return i==index->end()?nullptr:&i->second;}
 virtual bool HasFile(uint64_t h){return index->count(h);}
 bool HasFile(const std::string& p){return HasFile(CRC64(p.c_str()));}
 virtual std::shared_ptr<std::unordered_map<uint64_t,std::string>> ListFiles(){return index;}
 virtual std::shared_ptr<std::unordered_map<uint64_t,std::string>> ListFiles(const std::string& f){auto r=std::make_shared<std::unordered_map<uint64_t,std::string>>();for(auto& [h,p]:*index)if(glob_match(f.c_str(),p.c_str()))(*r)[h]=p;return r;}
};
}
''')
    stub('ship/resource/File.h', '#pragma once\n#include <memory>\n#include <vector>\nnamespace Ship { struct File { std::shared_ptr<std::vector<char>> Buffer; bool IsLoaded=false; }; }\n')
    for name in ('ship/resource/Resource.h', 'ship/Context.h', 'ship/window/Window.h'):
        stub(name, '')
    stub('spdlog/spdlog.h', '#define SPDLOG_TRACE(...) ((void)0)\n#define SPDLOG_ERROR(...) ((void)0)\n#define SPDLOG_INFO(...) ((void)0)\n')
    archive = temp / 'large.o2r'
    with zipfile.ZipFile(archive, 'w') as z:
        for i in range(40000): z.writestr(f'resources/entry-{i:05d}', bytes([i % 251]) * 16)
        z.writestr('house-background', b'H' * 65616)
        z.writestr('scenes/shared/demo/demo_room_1Tex', b'T' * 4096, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr('scenes/shared/demo/demo_room_1Data', b'D' * 4096)
        z.writestr('scenes/shared/demo/demo_room_10Tex', b'X' * 4096)
    code = source + r'''
#include <cassert>
#include <thread>
#include <atomic>
#include <malloc.h>
static size_t allocatedBytes() { auto m=mallinfo2(); return m.uordblks+m.hblkhd; }
int main(int argc,char** argv) {
 const auto openBefore=allocatedBytes();
 Ship::O2rArchive a(argv[1]); assert(a.Open());
 const auto openBytes=allocatedBytes()-openBefore;
 printf("Open retained allocation: %zu bytes\n", openBytes); fflush(stdout);
#ifdef TEST_COMPACT_ARCHIVE
 assert(openBytes < 6*1024*1024 && "Compact index must fit within 6 MiB for 40k entries");
#endif
 assert(a.HasFile(CRC64("house-background")));
 assert(!a.HasFile(CRC64("missing")));
 const auto* stable=a.HashToString(CRC64("house-background"));assert(stable && *stable=="house-background");
 { auto listing=a.ListFiles();assert(listing->size()==40004); }
 { auto listing=a.ListFiles("resources/entry-0000*");assert(listing->size()==10); }
 const auto before=allocatedBytes();
 auto bg=a.LoadFile(std::string("house-background"));
 assert(bg && bg->Buffer->size()==65616 && bg->Buffer->at(0)=='H');
 const auto after=allocatedBytes();
 printf("First read retained allocation increase: %zu bytes\n", after-before); fflush(stdout);
 assert(after-before < 1024*1024 && "First read must not duplicate the 40k-entry ZIP index");
 std::vector<std::thread> threads;
 for(int t=0;t<4;t++) threads.emplace_back([&,t]{
   for(int n=0;n<200;n++) {
     char path[64]; int i=t*200+n; snprintf(path,sizeof(path),"resources/entry-%05d",i);
     assert(a.HasFile(CRC64(path)));assert(*a.HashToString(CRC64(path))==path);
     auto f=a.LoadFile(std::string(path)); assert(f && static_cast<unsigned char>(f->Buffer->at(0))==i%251);
   }
 });
 for(auto& t:threads)t.join();
 assert(!a.LoadFile(std::string("missing")));
 // Cached compressed and stored entries must decode identically. Room 1 must
 // not prefetch room 10, and replacing the room must release its old cache.
 const auto used=a.PrefetchRoom("scenes/shared/demo/demo_room_1",16384);
 assert(used>0 && used<=16384);
 auto room=a.LoadFile(std::string("scenes/shared/demo/demo_room_1Tex"));
 assert(room && *room->Buffer==std::vector<char>(4096,'T'));
 room=a.LoadFile(std::string("scenes/shared/demo/demo_room_1Data"));
 assert(room && *room->Buffer==std::vector<char>(4096,'D'));
 assert(a.PrefetchHits()==2);
 room=a.LoadFile(std::string("scenes/shared/demo/demo_room_10Tex"));
 assert(room && room->Buffer->at(0)=='X' && a.PrefetchHits()==2);
 assert(a.WriteFile("scenes/shared/demo/demo_room_1Tex",std::vector<uint8_t>(4096,'U')));
 room=a.LoadFile(std::string("scenes/shared/demo/demo_room_1Tex"));
 assert(room && room->Buffer->at(0)=='U');
 assert(a.PrefetchRoom("scenes/shared/demo/demo_room_10",16384)>0);
 assert(a.PrefetchRoom("",0)==0);
 room=a.LoadFile(std::string("scenes/shared/demo/demo_room_10Tex"));assert(room && room->Buffer->at(0)=='X');
 assert(a.WriteFile("house-background",std::vector<uint8_t>(70000,'W')));
 bg=a.LoadFile(std::string("house-background")); assert(bg && bg->Buffer->size()==70000 && bg->Buffer->at(0)=='W');
 assert(stable==a.HashToString(CRC64("house-background")));
 assert(a.WriteFile("new-entry",std::vector<uint8_t>(32,'N')));
 assert(a.HasFile(CRC64("new-entry")));assert(*a.HashToString(CRC64("new-entry"))=="new-entry");
 assert(a.ListFiles()->size()==40005);
 assert(a.Close()); assert(a.Close());
 assert(a.Open()); bg=a.LoadFile(std::string("house-background")); assert(bg && bg->Buffer->at(0)=='W');
 assert(a.Close());
 puts("Archive memory, concurrent reads, write/read and reopen passed");
}
'''
    (temp/'test.cpp').write_text(code)
    extra = ['-DTEST_COMPACT_ARCHIVE', str(ROOT/'src/compat3ds/compact_zip.cpp')] if os.environ.get('SOH_TEST_COMPACT') else []
    subprocess.run(['cc', '-I'+str(ROOT/'third_party/libultraship/include'), '-c', str(ROOT/'third_party/libultraship/src/ship/utils/glob.c'), '-o', str(temp/'glob.o')],check=True)
    subprocess.run([os.environ.get('CXX','g++'), '-std=c++20','-O1','-g','-pthread','-D__3DS__', '-I'+str(temp), '-I'+str(ROOT/'third_party/libultraship/include'), str(temp/'test.cpp'), *extra, str(ROOT/'third_party/libultraship/src/ship/utils/StrHash64.cpp'), str(temp/'glob.o'), '-lzip','-lz','-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test'),str(archive)],check=True,timeout=60)
