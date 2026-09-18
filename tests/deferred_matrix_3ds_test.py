#!/usr/bin/env python3
"""Exercise production matrix commands against eager evaluation."""
from pathlib import Path
import subprocess,tempfile,re
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'third_party/libultraship/src/fast/interpreter.cpp').read_text()
def fn(name):
 a=s.index('void Interpreter::'+name+'(');b=s.index('{',a);i=b+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
methods='\n'.join(fn(n) for n in ['MatrixMul','GfxSpMatrix','GfxSpPopMatrix','UpdateCombinedMatrix'])
methods=methods.replace('    float tmp[4][4];','    ++multiplies;\n    float tmp[4][4];')
code=r'''
#include <cstdint>
#include <cstring>
#include <map>
#include <algorithm>
#include <cassert>
#define GBI_FLOATS
constexpr int MTX_PROJECTION=1,MTX_LOAD=2,MTX_PUSH=4;
int get_attr(int v){return v;}
bool IsValidResolvedAddress(uintptr_t v){return v!=0;}
struct Mtx{};struct Replacement{float mf[4][4];};using MtxF=Replacement;
struct RSP{float modelview_matrix_stack[11][4][4]{};unsigned modelview_matrix_stack_size=1;float MP_matrix[4][4]{},P_matrix[4][4]{};bool lights_changed=false;};
static int multiplies;
struct Interpreter{
 RSP state{};RSP* mRsp=&state;bool mCombinedMatrixDirty=false;
 std::map<Mtx*,Replacement> replacements;decltype(replacements)* mCurMtxReplacements=&replacements;
 static void MatrixMul(float[4][4],const float[4][4],const float[4][4]);
 void GfxSpMatrix(uint8_t,const int32_t*);void GfxSpPopMatrix(uint32_t);void UpdateCombinedMatrix();
};
'''+methods+r'''
int main(){
 Interpreter cached,eager;
 cached.mCurMtxReplacements=nullptr; // Native path matches the original empty-map path.
 float identity[4][4]{};for(int j=0;j<4;j++)identity[j][j]=1;
 auto load=[&](Interpreter& x,int flags,float m[4][4]){x.GfxSpMatrix(flags,(int32_t*)m);};
 multiplies=0;
 for(int i=0;i<10;i++)load(cached,MTX_LOAD|(i&1?MTX_PROJECTION:0),identity);
 assert(multiplies==0);cached.UpdateCombinedMatrix();assert(multiplies==1);
 for(int i=0;i<10;i++)cached.UpdateCombinedMatrix();assert(multiplies==1);
 load(eager,MTX_LOAD|MTX_PROJECTION,identity);load(eager,MTX_LOAD,identity);eager.UpdateCombinedMatrix();
 uint32_t rng=1;
 for(int i=0;i<2000;i++){
  rng=rng*1664525+1013904223;unsigned op=rng%8;
  float m[4][4];memcpy(m,identity,64);m[3][0]=(int)(rng&31)-16;
  if(op<6){unsigned flags=(op&1?MTX_PROJECTION:0)|(op&2?MTX_LOAD:0)|(op&4?MTX_PUSH:0);load(cached,flags,m);load(eager,flags,m);}
  else{unsigned count=op==7?100:1;cached.GfxSpPopMatrix(count);eager.GfxSpPopMatrix(count);}
  // Model of original eager evaluation after every command.
  Interpreter::MatrixMul(eager.state.MP_matrix,eager.state.modelview_matrix_stack[eager.state.modelview_matrix_stack_size-1],eager.state.P_matrix);
  eager.mCombinedMatrixDirty=false;
  if(i%7==0){cached.UpdateCombinedMatrix();assert(!memcmp(cached.state.MP_matrix,eager.state.MP_matrix,64));}
 }
 cached.UpdateCombinedMatrix();assert(!memcmp(cached.state.MP_matrix,eager.state.MP_matrix,64));
 // A no-op pop must not dirty a clean result. Oversized pops are bounded.
 cached.GfxSpPopMatrix(0xffffffffu);cached.UpdateCombinedMatrix();int before=multiplies;
 cached.GfxSpPopMatrix(0);cached.UpdateCombinedMatrix();assert(before==multiplies);
 // New replacements retain their original fixed-point quantization. Old
 // renders the supplied native matrix even when that address has a replacement.
 float source[4][4];memcpy(source,identity,64);source[3][0]=3.25f;
 Replacement replacement{};memcpy(replacement.mf,identity,64);replacement.mf[3][0]=-7.123456f;
 eager.replacements[(Mtx*)source]=replacement;
 cached.replacements[(Mtx*)source]=replacement;
 load(eager,MTX_LOAD,source);load(cached,MTX_LOAD,source);
 assert(eager.state.modelview_matrix_stack[0][3][0]==(int)(replacement.mf[3][0]*65536.f)/65536.f);
 assert(cached.state.modelview_matrix_stack[0][3][0]==source[3][0]);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++20','-O2','-ffp-contract=off',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=5)
print('PASS: 2000 matrix command sequences match eager evaluation; redundant multiplies eliminated; native/null and New replacement paths verified')
