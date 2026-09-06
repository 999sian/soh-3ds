#!/usr/bin/env python3
"""Exercise the real native settings font packing, layout and draw geometry."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
ui=root/'third_party/shipwright/soh/soh/Enhancements/dualscreen3ds'
s=(ui/'BottomScreen3DS.c').read_text()
start=s.index('extern const u8 default_font_bin[];');end=s.index('// Greedy word wrap',start)
font=[0]*2048
for c in range(33,127):font[c*8:c*8+8]=[0x70,0x50,0x50,0x70,0x50,0x50,0x50,0]
font[ord('I')*8:ord('I')*8+8]=[0x20]*7+[0]
font[ord('W')*8:ord('W')*8+8]=[0xFE]*7+[0]
font[ord('.')*8:ord('.')*8+8]=[0]*6+[0x20,0]
cpp=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "BitmapFont3DS.h"
using u8=uint8_t;
struct Gfx{};
struct PlayState{struct{void* gfxCtx;}state;};
Gfx commands[4096];Gfx* POLY_OPA_DISP=commands;
#define OPEN_DISPS(x) { (void)(x);
#define CLOSE_DISPS(x) }
constexpr int G_TF_POINT=0,G_TF_BILERP=1;
int filter=-1;const u8* texture;
struct Quad{int x,y;const u8* data;};std::vector<Quad> quads;
#define gDPPipeSync(g) ((void)(g))
#define gDPSetTextureFilter(g,f) ((void)(g),filter=f)
#define gDPSetPrimColor(cmd,m,l,r,g,b,a) ((void)(cmd),(void)(r),(void)(g),(void)(b))
// N64 tiles have 64-bit row strides. The interpreter derives image width
// from that stride: tightly packed 8x8 I4 would be imported as 16x4.
void Load(const u8* t,int bits,int w,int h) {
 const int lineBytes=((w*bits+63)/64)*8;
 assert(lineBytes*8/bits==w);assert(w==8&&h==8);texture=t;
}
#define G_IM_SIZ_8b 1
#define gDPLoadTextureBlock_4b(g,t,fmt,w,h,...) ((void)(g),Load(t,4,w,h))
#define gDPLoadTextureBlock(g,t,fmt,size,w,h,...) ((void)(g),assert(size==G_IM_SIZ_8b),Load(t,8,w,h))
void Rect(Gfx*,int x,int y,int right,int bottom,int,int,int,int ds,int dt){
 assert(filter==G_TF_POINT);assert(right-x==32&&bottom-y==32);assert(ds==1024&&dt==1024);
 assert(x%4==0&&y%4==0);quads.push_back({x/4,y/4,texture});
}
#define G_TX_RENDERTILE 0
#define gSPTextureRectangle(...) Rect(__VA_ARGS__)
const u8 default_font_bin[2048]={
'''+','.join(map(str,font))+'};\n'+s[start:end]+r'''
void Within(int left,int right) {
 for(const auto& q:quads)for(int y=0;y<8;++y)for(int x=0;x<8;++x){
  int a=q.data[y*8+x];
  assert(a==0||a==255);
  if(a)assert(q.x+x>=left&&q.x+x<right);
 }
}
int main(){
 // An asymmetric source retains its exact row/column pattern after cropping.
 uint8_t rows[8]={0x10,0x30,0x70,0x50,0x10,0,0,0},image[64];
 assert(BsFont_Expand(rows,image)==4);
 const uint8_t expected[8]={0x20,0x60,0xE0,0xA0,0x20,0,0,0};
 for(int y=0;y<8;++y)for(int x=0;x<8;++x){
  const bool on=image[y*8+x]==255;
  assert(on==((expected[y]&(0x80>>x))!=0));
 }
 Bs_InitSettingsFont();assert(Bs_TextWidth("I W",3)==14);
 assert(Bs_TextWidth("\xc3\xa9",2)==Bs_TextWidth("?",1));
 PlayState p{};Bs_SettingsText(&p,"I W",14,42,255,255,255,14);
 assert(quads.size()==2);assert(quads[0].x==14&&quads[1].x==20);Within(14,28);
 assert(filter==G_TF_BILERP);
 quads.clear();Bs_SettingsText(&p,"WWWWWWWWWWWW",196,42,255,255,255,24);
 assert(quads.size()==5);Within(196,220); // two W glyphs followed by ellipsis
 quads.clear();Bs_SettingsText(&p,"WWW",14,42,255,255,255,1);Within(14,15);
 puts("native settings font: binary pixels, exact texel size, spacing, UTF-8 fallback and bounded labels pass");
}
'''
with tempfile.TemporaryDirectory(prefix='soh-settings-font-') as d:
 p=Path(d);(p/'test.cpp').write_text(cpp)
 subprocess.run([os.environ.get('CXX','g++'),'-std=c++17','-O1','-Wall','-Wextra','-I',str(ui),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
