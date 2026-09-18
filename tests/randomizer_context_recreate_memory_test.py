#!/usr/bin/env python3
"""Production Context factory must release expired make_shared storage before replacement."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'third_party/shipwright/soh/soh/Enhancements/randomizer/SeedContext.cpp').read_text()
a=s.index('std::shared_ptr<Context> Context::CreateInstance()');b=s.index('\nstd::shared_ptr<Context> Context::CreateDetachedInstance()',a)
code=r'''
#include <memory>
#include <cassert>
#include <cstdlib>
#include <new>
static void* largeBlock=nullptr;
void* operator new(std::size_t n) {
 if(n>=512*1024 && largeBlock)throw std::bad_alloc();
 void* p=std::malloc(n);if(!p)throw std::bad_alloc();
 if(n>=512*1024)largeBlock=p;return p;
}
void operator delete(void* p) noexcept {if(p==largeBlock)largeBlock=nullptr;std::free(p);}
void operator delete(void* p,std::size_t) noexcept {::operator delete(p);}
class Context {
 char contents[512*1024]{};
 struct Logic {void SetContext(std::shared_ptr<Context>) {}} logic;
public:
 static std::weak_ptr<Context> mContext;
 static std::shared_ptr<Context> CreateInstance();
 static std::shared_ptr<Context> GetInstance(){return mContext.lock();}
 Logic* GetLogic(){return &logic;}
};
std::weak_ptr<Context> Context::mContext;
'''+s[a:b]+r'''
int main(){
 for(int i=0;i<20;i++){
  auto c=Context::CreateInstance();assert(c);
  assert(Context::CreateInstance()==c); // existing instance is unchanged
  c.reset();assert(Context::mContext.expired());
 }
 Context::mContext.reset();assert(!largeBlock);
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++17',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Context factory: 20 recreations within one large-allocation budget, existing instance preserved')
