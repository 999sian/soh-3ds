#!/usr/bin/env python3
"""Compare the old-model ZIP reader against libzip, including corrupt payloads."""
import os, subprocess, tempfile, zipfile, struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='soh-compact-zip-') as d:
    tmp=Path(d)
    for mode in (zipfile.ZIP_STORED,zipfile.ZIP_DEFLATED):
        with zipfile.ZipFile(tmp/f'{mode}.zip','w',compression=mode) as z:
            z.comment=b'central directory comment'
            for name,data in [('empty',b''),('folder/',b''),('Case',b'UP'),('case',b'low'),('utf8/é',bytes(range(256))*1000),('big',b'a'*200000),('long/'+600*'x',b'long-name')]:z.writestr(name,data)
    damaged=bytearray((tmp/'0.zip').read_bytes())
    with zipfile.ZipFile(tmp/'0.zip') as z:
        i=z.getinfo('Case');off=i.header_offset;n,e=struct.unpack_from('<HH',damaged,off+26);damaged[off+30+n+e]^=1
    (tmp/'corrupt.zip').write_bytes(damaged)
    (tmp/'truncated.zip').write_bytes(damaged[:50])
    (tmp/'test.cpp').write_text(r'''
#include <ship/resource/archive/CompactZip.h>
#include <zip.h>
#include <cassert>
#include <cstdio>
#include <cstring>
int main(int argc,char**argv) {
 auto* api=Soh3dsGetCompactZipApi();
 for(int a=1;a<argc-2;a++) {
  void* h=api->open(argv[a]); assert(h);
  zip_t* z=zip_open(argv[a],ZIP_RDONLY,nullptr); assert(z);
  assert(api->count(h)==static_cast<size_t>(zip_get_num_entries(z,0)));
  if(a>=3) api->prefetchRoom(h,"scenes/shared/link_home_scene/link_home_room_0",2*1024*1024);
  for(size_t i=0;i<api->count(h);i++) {
   const auto name=api->name(h,i); assert(name==zip_get_name(z,i,0));
   if(name.ends_with('/')) continue;
   zip_stat_t st;assert(zip_stat_index(z,i,0,&st)==0);
   if(a<3) assert(api->prefetchRoom(h,name,16384)<=16384);
   auto got=api->read(h,name);assert(got && got->size()==st.size);
   std::vector<char> expected(st.size);auto*f=zip_fopen_index(z,i,0);assert(f);
   assert(zip_fread(f,expected.data(),expected.size())==static_cast<zip_int64_t>(expected.size()));
   assert(zip_fclose(f)==0);assert(expected==*got);
  }
  assert(!api->read(h,"missing-file"));
  api->close(h);zip_close(z);printf("Verified all entries: %s\n",argv[a]);fflush(stdout);
 }
 void*h=api->open(argv[argc-2]);assert(h);assert(!api->read(h,"Case"));
 assert(api->prefetchRoom(h,"Case",16384)>0);assert(!api->read(h,"Case"));api->close(h);
 assert(!api->open(argv[argc-1]));puts("CRC failure and truncated archive rejected");
}
''')
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++20','-O1','-I'+str(ROOT/'third_party/libultraship/include'),str(tmp/'test.cpp'),str(ROOT/'src/compat3ds/compact_zip.cpp'),'-lzip','-lz','-o',str(tmp/'test')],check=True)
    archives=[str(tmp/'0.zip'),str(tmp/'8.zip')]
    if os.environ.get('SOH_TEST_REAL_ARCHIVES'):
        archives += [str(ROOT/'third_party/shipwright/soh.o2r'),'/home/sian/.local/share/azahar-emu/sdmc/3ds/soh/oot.o2r']
    subprocess.run([str(tmp/'test'),*archives,str(tmp/'corrupt.zip'),str(tmp/'truncated.zip')],check=True,timeout=120)
