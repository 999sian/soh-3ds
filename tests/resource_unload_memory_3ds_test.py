#!/usr/bin/env python3
"""Run production bulk unload with archive-list allocation failing and reentrant destructors."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'third_party/libultraship/src/ship/resource/ResourceManager.cpp').read_text()
a=s.index('void ResourceManager::UnloadResourcesProcess(');b=s.index('\nstd::shared_ptr<ArchiveManager>',a)
code=r'''
#include <cassert>
#include <map>
#include <set>
#include <list>
#include <vector>
#include <memory>
#include <mutex>
#include <string>
#include <algorithm>
#include <functional>
#include <tuple>
#include "ship/utils/glob.h"
struct ResourceIdentifier {
 std::string Path;unsigned Owner=0;std::shared_ptr<int> Parent=nullptr;
 bool operator<(const ResourceIdentifier& o)const{return std::tie(Path,Owner,Parent)<std::tie(o.Path,o.Owner,o.Parent);}
};
struct ResourceFilter {std::list<std::string> IncludeMasks,ExcludeMasks;};
struct Resource {std::function<void()> onDestroy;~Resource(){if(onDestroy)onDestroy();}};
struct ArchiveManager {
 std::set<std::string> files;
 bool HasFile(const std::string& p){return files.count(p);}
 std::shared_ptr<std::vector<std::string>> ListFiles(const std::list<std::string>&,const std::list<std::string>&){throw std::bad_alloc();}
};
class ResourceManager {
public:
 std::mutex mMutex;unsigned mDefaultCacheOwner=0;std::shared_ptr<int> mDefaultCacheArchive=nullptr;
 std::map<ResourceIdentifier,std::shared_ptr<Resource>> mResourceCache;
 std::shared_ptr<ArchiveManager> archive=std::make_shared<ArchiveManager>();
 auto GetArchiveManager(){return archive;}
 void UnloadResource(ResourceIdentifier id){mResourceCache.erase(id);}
 void UnloadResourcesProcess(const ResourceFilter& filter);
};
'''+s[a:b]+r'''
int main(){
 ResourceManager m;int destroyed=0;
 auto add=[&](std::string name,unsigned owner=0,bool listed=true){
  auto r=std::make_shared<Resource>();r->onDestroy=[&]{std::lock_guard lock(m.mMutex);++destroyed;};
  m.mResourceCache[{name,owner,nullptr}]=r;if(listed)m.archive->files.insert(name);
 };
 for(int i=0;i<1200;i++)add("scenes/old/texture"+std::to_string(i));
 add("scenes/old/keep");add("scenes/new/data");add("scenes/old/other-owner",7);
 add("scenes/old/external",0,false); // preserve non-archive resources, as before
 add("alias",0,false);m.archive->files.insert("alias.meta");
 m.UnloadResourcesProcess({{"scenes/old/*"},{"*/keep"}});
 assert(destroyed==1200);assert(m.mResourceCache.size()==5);
 m.UnloadResourcesProcess({{"alias.meta"},{}});assert(destroyed==1201);
 assert(!m.mResourceCache.count({"alias",0,nullptr}));
 // Empty includes means all listed paths, with exclusion still taking priority.
 m.UnloadResourcesProcess({{},{"scenes/new/*"}});assert(destroyed==1202);
 assert(m.mResourceCache.size()==3);
 // A destructor may re-enter and change the cache; no iterator survives unlock.
 auto r=std::make_shared<Resource>();r->onDestroy=[&]{m.UnloadResource({"scenes/old/external",0,nullptr});};
 m.mResourceCache[{"reentrant",0,nullptr}]=r;m.archive->files.insert("reentrant");r.reset();
 m.UnloadResourcesProcess({{"reentrant"},{}});assert(m.mResourceCache.size()==2);
 m.mResourceCache.clear();
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.cpp').write_text(code)
 subprocess.run(['cc','-I'+str(root/'third_party/libultraship/include'),'-c',str(root/'third_party/libultraship/src/ship/utils/glob.c'),'-o',str(p/'glob.o')],check=True)
 subprocess.run(['c++','-std=c++20','-D__3DS__','-I'+str(root/'third_party/libultraship/include'),str(p/'test.cpp'),str(p/'glob.o'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=10)
print('Bulk unload: no archive list needed; glob exclusions, metadata aliases, scope and reentrant destruction pass')
