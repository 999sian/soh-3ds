#!/usr/bin/env python3
"""Room selection and aggregate heap reserve must survive MQ/alternate paths."""
import os, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
    p=Path(d)
    (p/'test.cpp').write_text(r'''
#include "ship/resource/archive/RoomPrefetchPolicy3DS.h"
#include <cassert>
int main() {
 using namespace Ship;
 constexpr size_t MiB=1024*1024;
 assert(RoomPrefetchBudget3DS(5*MiB,true)==0);
 assert(RoomPrefetchBudget3DS(8*MiB,true)==0);
 assert(RoomPrefetchBudget3DS(8*MiB+65536,true)==65536);
 assert(RoomPrefetchBudget3DS(30*MiB,true)<=128*1024);
 assert(RoomPrefetchBudget3DS(12*MiB,false)==0);
 assert(RoomPrefetchBudget3DS(13*MiB,false)==MiB);
 assert(RoomPrefetchBudget3DS(30*MiB,false)==2*MiB);
 assert(RoomPrefetchBudget3DS(30*MiB,false,true)==0);
 assert(RoomPrefetchBudget3DS(30*MiB,true,true)==0);
 assert(RoomPrefix3DS("__OTR__scenes/nonmq/demo/demo_room_1Set_000123",true)=="scenes/mq/demo/demo_room_1");
 assert(RoomPrefix3DS("alt/scenes/shared/demo/demo_room_10",false)=="scenes/shared/demo/demo_room_10");
 assert(RoomPrefix3DS("objects/demo_room_1",false).empty());
 assert(RoomPrefix3DS("scenes/shared/demo/demo_room_bad",false).empty());
 assert(RoomPrefix3DS("scenes/shared/demo/demo_scene",false).empty());
}
''')
    subprocess.run([os.environ.get('CXX','g++'),'-std=c++20','-I'+str(ROOT/'third_party/libultraship/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
