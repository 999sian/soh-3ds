#!/usr/bin/env python3
"""Exercise production archive-manager filtering with temporary lazy snapshots."""
import os, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
s = (ROOT/'third_party/libultraship/src/ship/resource/archive/ArchiveManager.cpp').read_text()
s = s[s.index('std::shared_ptr<std::vector<std::string>> ArchiveManager::ListFiles('):s.index('std::shared_ptr<std::vector<std::string>> ArchiveManager::ListDirectories(')]
code = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <ship/utils/glob.h>
namespace Ship {
struct Archive {
 std::unordered_map<uint64_t,std::string> files;
 int full=0,filtered=0;
 auto ListFiles(){++full;return std::make_shared<decltype(files)>(files);}
 auto ListFiles(const std::string& f){++filtered;auto r=std::make_shared<decltype(files)>();for(auto& [h,p]:files)if(glob_match(f.c_str(),p.c_str()))(*r)[h]=p;return r;}
};
struct ArchiveManager {
 std::vector<std::shared_ptr<Archive>> mArchives;
 auto ResolveArchive(uint64_t h){for(auto i=mArchives.rbegin();i!=mArchives.rend();++i)if(*i && (*i)->files.count(h))return *i;return std::shared_ptr<Archive>{};}
 std::shared_ptr<std::vector<std::string>> ListFiles(const std::string&);
 std::shared_ptr<std::vector<std::string>> ListFiles(const std::list<std::string>&,const std::list<std::string>&);
};
''' + s + r'''
}
int main(){
 auto a=std::make_shared<Ship::Archive>(),b=std::make_shared<Ship::Archive>();
 a->files={{1,"scenes/house/bg"},{2,"scenes/house/room"},{3,"audio/sample"}};
 b->files={{1,"scenes/house/bg"},{4,"objects/link"}};
 Ship::ArchiveManager m{{a,nullptr,b}};
 auto one=m.ListFiles("scenes/house/*"); assert(one->size()==2);
 assert(a->full==0 && b->full==0 && "Filtered request must not materialize the entire archive");
 assert(a->filtered==1 && b->filtered==1);
 auto multi=m.ListFiles({"scenes/*","objects/*"},{"*/room"});assert(multi->size()==2);
 auto all=m.ListFiles("");assert(all->size()==4);
 auto none=m.ListFiles("missing/*");assert(none->empty());
 auto excluded=m.ListFiles({}, {"scenes/*"});assert(excluded->size()==2);
}
'''
with tempfile.TemporaryDirectory(prefix='soh-listing-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 inc='-I'+str(ROOT/'third_party/libultraship/include')
 subprocess.run(['cc',inc,'-c',str(ROOT/'third_party/libultraship/src/ship/utils/glob.c'),'-o',str(p/'glob.o')],check=True)
 subprocess.run([os.environ.get('CXX','g++'),'-std=c++20','-O1','-g','-fsanitize=address,undefined',inc,str(p/'test.cpp'),str(p/'glob.o'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
