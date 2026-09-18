#!/usr/bin/env python3
"""Exercise production resource manager retry after importer/heap failures."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'third_party/libultraship/src/ship/resource/ResourceManager.cpp').read_text()
a=s.index('std::shared_ptr<IResource> ResourceManager::LoadResourceProcess(const ResourceIdentifier&')
b=s.index('std::shared_ptr<IResource> ResourceManager::LoadResourceProcess(const std::string&',a)
code=r'''
#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <variant>
#define SPDLOG_TRACE(...) ((void)0)
#define SPDLOG_ERROR(...) ((void)0)
struct ResourceInitData{}; struct File{};
struct IResource{static inline std::string gAltAssetPrefix="alt/";};
struct ResourceIdentifier{std::string Path; int Owner=0,Parent=0; auto operator<=>(const ResourceIdentifier&) const = default;};
bool OtrSignatureCheck(const char*){return false;}
class ResourceManager {
public:
 enum class ResourceLoadError{NotCached,NotFound};
 using Line=std::variant<ResourceLoadError,std::shared_ptr<IResource>>;
 std::map<ResourceIdentifier,Line> mResourceCache;
 std::mutex mMutex; bool mAltAssetsEnabled=true,missing=false,throwFile=false,failImport=true; int imports=0;
 ResourceManager* mArchiveManager=this;
 std::shared_ptr<IResource> LoadResourceProcess(const ResourceIdentifier&,bool,std::shared_ptr<ResourceInitData>);
 Line CheckCache(const ResourceIdentifier& id,bool){auto it=mResourceCache.find(id);return it==mResourceCache.end()?Line(ResourceLoadError::NotCached):it->second;}
 std::shared_ptr<IResource> GetCachedResource(Line l){return std::holds_alternative<std::shared_ptr<IResource>>(l)?std::get<std::shared_ptr<IResource>>(l):nullptr;}
 std::shared_ptr<IResource> GetCachedResource(const ResourceIdentifier& id,bool exact){return GetCachedResource(CheckCache(id,exact));}
 std::shared_ptr<File> LoadFileProcess(const std::string&){if(throwFile)throw std::bad_alloc();return missing?nullptr:std::make_shared<File>();}
 bool HasFile(const std::string&){return false;}
 auto GetResourceLoader(){return this;}
 std::shared_ptr<IResource> LoadResource(const std::string&,std::shared_ptr<File>,std::shared_ptr<ResourceInitData>){++imports;return failImport?nullptr:std::make_shared<IResource>();}
};
'''+s[a:b]+r'''
int main(){
 ResourceManager rm;ResourceIdentifier id{"alt/audio/fonts/test"};
 assert(!rm.LoadResourceProcess(id,false,nullptr));
 assert(rm.imports==1);
 rm.failImport=false;
 assert(rm.LoadResourceProcess(id,false,nullptr) && "existing file must retry after importer failure");
 assert(rm.imports==2);
 rm.mResourceCache.clear();rm.throwFile=true;
 assert(!rm.LoadResourceProcess(id,false,nullptr));
 rm.throwFile=false;assert(rm.LoadResourceProcess(id,false,nullptr));
 rm.mResourceCache.clear();rm.missing=true;
 assert(!rm.LoadResourceProcess(id,false,nullptr));
 assert(std::get<ResourceManager::ResourceLoadError>(rm.mResourceCache.at(id))==ResourceManager::ResourceLoadError::NotFound);
}
'''
with tempfile.TemporaryDirectory(prefix='soh-import-retry-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++20','-D__3DS__','-fsanitize=address,undefined','-no-pie',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: import and allocation failure retries; missing files stay negatively cached')
