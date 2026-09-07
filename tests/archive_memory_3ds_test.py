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
namespace Ship {
class Archive {
 std::string path;
 public: explicit Archive(const std::string& p):path(p){}
 const std::string& GetPath() const { return path; }
 void ReserveIndex(size_t){} void IndexFile(const char*){} void IndexFile(const std::string&){}
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
    code = source + r'''
#include <cassert>
#include <thread>
#include <atomic>
#include <malloc.h>
int main(int argc,char** argv) {
 Ship::O2rArchive a(argv[1]); assert(a.Open());
 const auto before=mallinfo2().uordblks;
 auto bg=a.LoadFile(std::string("house-background"));
 assert(bg && bg->Buffer->size()==65616 && bg->Buffer->at(0)=='H');
 const auto after=mallinfo2().uordblks;
 printf("First read retained allocation increase: %zu bytes\n", after-before); fflush(stdout);
 assert(after-before < 1024*1024 && "First read must not duplicate the 40k-entry ZIP index");
 std::vector<std::thread> threads;
 for(int t=0;t<4;t++) threads.emplace_back([&,t]{
   for(int n=0;n<200;n++) {
     char path[64]; int i=t*200+n; snprintf(path,sizeof(path),"resources/entry-%05d",i);
     auto f=a.LoadFile(std::string(path)); assert(f && static_cast<unsigned char>(f->Buffer->at(0))==i%251);
   }
 });
 for(auto& t:threads)t.join();
 assert(!a.LoadFile(std::string("missing")));
 assert(a.WriteFile("house-background",std::vector<uint8_t>(70000,'W')));
 bg=a.LoadFile(std::string("house-background")); assert(bg && bg->Buffer->size()==70000 && bg->Buffer->at(0)=='W');
 assert(a.Close()); assert(a.Close());
 assert(a.Open()); bg=a.LoadFile(std::string("house-background")); assert(bg && bg->Buffer->at(0)=='W');
 assert(a.Close());
 puts("Archive memory, concurrent reads, write/read and reopen passed");
}
'''
    (temp/'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','g++'), '-std=c++20','-O1','-g','-pthread','-D__3DS__', '-I'+str(temp), '-I'+str(ROOT/'third_party/libultraship/include'), str(temp/'test.cpp'), '-lzip','-o',str(temp/'test')],check=True)
    subprocess.run([str(temp/'test'),str(archive)],check=True,timeout=60)
