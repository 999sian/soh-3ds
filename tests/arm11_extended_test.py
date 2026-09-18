#!/usr/bin/env python3
"""ARM11 correctness harness shared with the second hardware benchmark."""
from pathlib import Path
import importlib.util,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
SDK=Path('/home/sian/dkp-root/opt/devkitpro/devkitARM/bin')
spec=importlib.util.spec_from_file_location('base',ROOT/'tests/arm11_kernels_test.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
def harness():
 matrix=(ROOT/'third_party/shipwright/soh/src/code/sys_matrix.c').read_text()
 return base.harness()+r'''
typedef struct { float x,y,z; } Vec3f;
void Matrix_MultVec3fExt(Vec3f*,Vec3f*,MtxF*);
__attribute__((noinline)) void Soh3dsMatrixVecReference(Vec3f* src,Vec3f* dest,MtxF* mf)
'''+base.body(matrix,'Matrix_MultVec3fExt')+r'''
__attribute__((noinline)) void mixReference(const int16_t* in,int16_t* out,int32_t gain,uint32_t count){
 while(count--){
  int32_t sample=gain==-32768 ? *out-*in : ((*out*0x7fff+*in*gain)+0x4000)>>15;
  *out++=sample<-32768?-32768:sample>32767?32767:sample;in++;
 }
}
__attribute__((noinline)) void addMixerReference(const int16_t* in,int16_t* out,uint32_t count){
 while(count--){
  int32_t sample=(int32_t)*out+(int32_t)*in++;
  *out++=sample<-32768?-32768:sample>32767?32767:sample;
 }
}
__attribute__((noinline)) void interleaveReference(const int16_t* l,const int16_t* r,int16_t* d,uint32_t groups){
 while(groups--){
  int16_t l0=*l++,l1=*l++,l2=*l++,l3=*l++;
  int16_t r0=*r++,r1=*r++,r2=*r++,r3=*r++;
  *d++=l0;*d++=r0;*d++=l1;*d++=r1;*d++=l2;*d++=r2;*d++=l3;*d++=r3;
 }
}
__attribute__((noinline)) void interlReference(const int16_t* in,int16_t* out,uint32_t groups){
 while(groups--){
  for(int k=0;k<8;k++){*out++=*in++;in++;}
 }
}
// Mirrors sys_matrix.c Matrix_Translate's MTXMODE_APPLY branch exactly,
// including the ((tx*x + ty*y) + xz*z) association.
__attribute__((noinline)) void translateReference(float* m,float x,float y,float z){
 for(int r=0;r<4;r++){
  float tx=m[r],ty=m[4+r],tz=m[8+r];
  m[12+r]+=tx*x+ty*y+tz*z;
 }
}
// Mirrors sys_matrix.c Matrix_Scale's MTXMODE_APPLY branch. All twelve
// multiplies are independent, so lane order is irrelevant; the w column is
// deliberately untouched.
__attribute__((noinline)) void scaleReference(float* m,float x,float y,float z){
 for(int r=0;r<4;r++){m[r]*=x;m[4+r]*=y;m[8+r]*=z;}
}
// Mirrors the translate + Z-rotation stage of sys_matrix.c
// Matrix_TranslateRotateZYX, including temp1/temp2 being the pre-rotation
// values and the ((t1*tx + t2*ty) + g2*tz) association.
__attribute__((noinline)) void trzReference(float* m,float tx,float ty,float tz,
                                            float sn,float cs){
 for(int r=0;r<4;r++){
  float t1=m[r],t2=m[4+r];
  m[12+r]+=t1*tx+t2*ty+m[8+r]*tz;
  m[r]=t1*cs+t2*sn;
  m[4+r]=t2*cs-t1*sn;
 }
}
// Force a random bit pattern to a finite float: clear the exponent if it is all
// ones (inf/NaN) so the value stays comparable bit-for-bit.
static uint32_t finiteBits(uint32_t bits){
 return ((bits&0x7F800000u)==0x7F800000u)?(bits&~0x00800000u):bits;
}
// Mirror the three rotation stages shared by Matrix_RotateZYX and
// Matrix_TranslateRotateZYX. Column pairs: Z=(0,1), Y=(0,2), X=(1,2).
__attribute__((noinline)) void rotZReference(float* m,float sn,float cs){
 for(int r=0;r<4;r++){float t1=m[r],t2=m[4+r];m[r]=t1*cs+t2*sn;m[4+r]=t2*cs-t1*sn;}
}
__attribute__((noinline)) void rotYReference(float* m,float sn,float cs){
 for(int r=0;r<4;r++){float t1=m[r],t2=m[8+r];m[r]=t1*cs-t2*sn;m[8+r]=t1*sn+t2*cs;}
}
__attribute__((noinline)) void rotXReference(float* m,float sn,float cs){
 for(int r=0;r<4;r++){float t1=m[4+r],t2=m[8+r];m[4+r]=t1*cs+t2*sn;m[8+r]=t2*cs-t1*sn;}
}
static uint32_t mtxfA[16],mtxfB[16];
static int32_t mtxA[16],mtxB[16];
__attribute__((noinline)) void mtxF2LReference(float mf[4][4],int32_t* m){
 unsigned r,c;int32_t tmp1,tmp2;int32_t* m1=m;int32_t* m2=m+8;
 for(r=0;r<4;r++){
  for(c=0;c<2;c++){
   tmp1=(int32_t)(mf[r][2*c]*65536.0f);
   tmp2=(int32_t)(mf[r][2*c+1]*65536.0f);
   *m1++=(tmp1&0xffff0000)|((tmp2>>0x10)&0xffff);
   *m2++=((tmp1<<0x10)&0xffff0000)|(tmp2&0xffff);
  }
 }
}
__attribute__((noinline)) void adpcmReference(int16_t* out,const int16_t ins[8],const int16_t tbl[2][8]){
 int16_t prev1=out[-1],prev2=out[-2];
 for(int j=0;j<8;j++){
  int32_t acc=tbl[0][j]*prev2+tbl[1][j]*prev1+(ins[j]<<11);
  for(int k=0;k<j;k++)acc+=tbl[1][j-k-1]*ins[k];
  acc>>=11;*out++=acc<-32768?-32768:acc>32767?32767:acc;
 }
}
__attribute__((noinline)) void envReference(const int16_t* in,int16_t* const dryIn[2],int16_t* const wetIn[2],uint32_t count,
 const uint16_t volsIn[3],const uint16_t rates[3],const int32_t negs[4],uint32_t swap){
 int16_t* dry[2]={dryIn[0],dryIn[1]},*wet[2]={wetIn[0],wetIn[1]};
 uint16_t vols[3]={volsIn[0],volsIn[1],volsIn[2]};
 do{
  for(int i=0;i<8;i++){
   int16_t samples[2]={*in,*in};in++;
   for(int j=0;j<2;j++) samples[j]=(samples[j]*vols[j]>>16)^negs[j];
   for(int j=0;j<2;j++){
    int v=*dry[j]+samples[j];*dry[j]++=v<-32768?-32768:v>32767?32767:v;
    v=*wet[j]+((samples[swap?1-j:j]*vols[2]>>16)^negs[2+j]);
    *wet[j]++=v<-32768?-32768:v>32767?32767:v;
   }
  }
  for(int j=0;j<3;j++)vols[j]+=rates[j];
  count-=8;
 }while(count);
}
static int16_t extA[4096],extB[4096],adpcmIns[8],adpcmTable[2][8];
static uint16_t envVols[3],envRates[3];static int32_t envNegs[4];
static void extendedCorrectness(void){
 for(int trial=0;trial<4096;trial++){
  for(unsigned j=0;j<4096;j++)extA[j]=extB[j]=(int16_t)rnd();
  for(int j=0;j<8;j++)adpcmIns[j]=(int16_t)rnd();
  for(int j=0;j<16;j++)((int16_t*)adpcmTable)[j]=(int16_t)rnd();
  adpcmReference(extA+2,adpcmIns,adpcmTable);Soh3dsAdpcmHalfArm11(extB+2,adpcmIns,adpcmTable);
  check(equal(extA,extB,sizeof(extA)));
  unsigned count=trial&511,offset=(trial&7);int gain=trial&1?-32768:(int16_t)rnd();
  mixReference(extA+4,extA+offset,gain,count);Soh3dsMixArm11(extB+4,extB+offset,gain,count);
  check(equal(extA,extB,sizeof(extA)));
  for(int j=0;j<3;j++){envVols[j]=rnd();envRates[j]=rnd();}
  envNegs[0]=trial&1?-1:0;envNegs[1]=trial&2?-1:0;envNegs[2]=trial&4?-4:0;envNegs[3]=trial&8?-2:0;
  // Include aliased dry/wet/input ranges as well as disjoint wave buffers.
  int16_t* da[2]={extA+512,extA+(trial&16?1024:513)};
  int16_t* wa[2]={extA+(trial&32?2048:512),extA+3072};
  int16_t* db[2]={extB+512,extB+(trial&16?1024:513)};
  int16_t* wb[2]={extB+(trial&32?2048:512),extB+3072};
  count=((trial&63)+1)*8;
  envReference(extA+(trial&64?0:511),da,wa,count,envVols,envRates,envNegs,trial&128);
  Soh3dsEnvMixerArm11(extB+(trial&64?0:511),db,wb,count,envVols,envRates,envNegs,trial&128);
  check(equal(extA,extB,sizeof(extA)));
  unsigned addCount=trial&511,inOff=(trial&3)*2,outOff=((trial>>2)&3)*2;
  addMixerReference(extA+inOff,extA+1024+outOff,addCount);
  Soh3dsAddMixerArm11(extB+inOff,extB+1024+outOff,addCount);
  check(equal(extA,extB,sizeof(extA)));
  // Groups of 4 frames; vary all three buffer alignments to cover the word
  // burst path and the halfword scalar path.
  unsigned ilGroups=(trial&63)+1,lOff=(trial&1),rOff=((trial>>1)&1),dOff=((trial>>2)&1);
  interleaveReference(extA+lOff,extA+256+rOff,extA+1024+dOff,ilGroups);
  Soh3dsInterleaveArm11(extB+lOff,extB+256+rOff,extB+1024+dOff,ilGroups);
  check(equal(extA,extB,sizeof(extA)));
  // aInterl: exercise the burst path, the halfword scalar path, exact aliasing
  // (out==in) and the forward-overlap case that must keep sequential order.
  unsigned ntGroups=(trial&31)+1,ntIn=(trial&1),ntOutSel=(trial>>1)&3;
  int16_t* ntInA=extA+64+ntIn; int16_t* ntInB=extB+64+ntIn;
  int16_t* ntOutA; int16_t* ntOutB;
  if(ntOutSel==0){ntOutA=extA+2048;ntOutB=extB+2048;}
  else if(ntOutSel==1){ntOutA=ntInA;ntOutB=ntInB;}
  else if(ntOutSel==2){ntOutA=extA+2048+1;ntOutB=extB+2048+1;}
  else {ntOutA=ntInA+3;ntOutB=ntInB+3;}
  interlReference(ntInA,ntOutA,ntGroups);
  Soh3dsInterlArm11(ntInB,ntOutB,ntGroups);
  check(equal(extA,extB,sizeof(extA)));
 }
 // Finite values, infinities, NaNs, denormals and aliasing. Set identical FPSCR
 // for both functions; no tolerance may hide changed float operation order.
 for(int trial=0;trial<8192;trial++){
  for(int j=0;j<64;j++)matrixA[j]=matrixB[j]=rnd();
  if(trial<4096)for(int j=0;j<64;j++){
   union {float f;uint32_t u;} v;v.f=(int32_t)(rnd()&65535)-32768;matrixA[j]=matrixB[j]=v.u;
  }
  unsigned src=32,dest=trial&1?48:32+((trial>>1)&3);
  if((trial&15)==0)dest=trial&15; // destination overlaps the matrix
  Soh3dsMatrixVecReference((Vec3f*)(matrixA+src),(Vec3f*)(matrixA+dest),(MtxF*)matrixA);
  Matrix_MultVec3fExt((Vec3f*)(matrixB+src),(Vec3f*)(matrixB+dest),(MtxF*)matrixB);
  check(equal(matrixA,matrixB,sizeof(matrixA)));
 }
 // guMtxF2L: random bit patterns plus the value classes that make the product
 // interesting - saturating magnitudes, sub-LSB fractions, denormals, NaN.
 for(int trial=0;trial<8192;trial++){
  union {float f;uint32_t u;} v;
  for(int j=0;j<16;j++){
   switch((trial+j)&7){
    case 0: v.f=(float)((int32_t)(rnd()&65535)-32768); break;
    case 1: v.f=(float)((int32_t)rnd())/65536.0f; break;
    case 2: v.f=1.0f/(float)((int32_t)(rnd()|1)); break;
    case 3: v.f=(float)(int32_t)rnd()*65536.0f; break;
    case 4: v.u=rnd()&0x807fffffu; break;
    case 5: v.u=0x7f800000u|(rnd()&0x807fffffu); break;
    default: v.u=rnd(); break;
   }
   mtxfA[j]=mtxfB[j]=v.u;
  }
  for(int j=0;j<16;j++)mtxA[j]=mtxB[j]=(int32_t)rnd();
  mtxF2LReference((float(*)[4])mtxfA,mtxA);
  Soh3dsMtxF2LArm11((float(*)[4])mtxfB,mtxB);
  check(equal(mtxfA,mtxfB,sizeof(mtxfA)));
  check(equal(mtxA,mtxB,sizeof(mtxA)));
  // Matrix_Translate APPLY: bit-exact against the reference association
  // ((t0+t1)+t2). Reuses the same adversarial float classes as guMtxF2L.
  union {float f;uint32_t u;} tv;
  float trans[3];
  for(int j=0;j<3;j++){
   switch((trial+j)&5){
    case 0: tv.f=(float)((int32_t)(rnd()&65535)-32768); break;
    case 1: tv.u=rnd()&0x807fffffu; break;
    case 2: tv.u=0x7f800000u|(rnd()&0x807fffffu); break;
    default: tv.u=rnd(); break;
   }
   trans[j]=tv.f;
  }
  for(int j=0;j<16;j++){tv.u=rnd();mtxfA[j]=mtxfB[j]=tv.u;}
  translateReference((float*)mtxfA,trans[0],trans[1],trans[2]);
  Soh3dsMatrixTranslateApplyArm11((float*)mtxfB,trans[0],trans[1],trans[2]);
  check(equal(mtxfA,mtxfB,sizeof(mtxfA)));
  // Matrix_Scale APPLY: same adversarial inputs; verifies the w column is
  // left alone as well as the scaled columns.
  for(int j=0;j<16;j++){tv.u=rnd();mtxfA[j]=mtxfB[j]=tv.u;}
  scaleReference((float*)mtxfA,trans[0],trans[1],trans[2]);
  Soh3dsMatrixScaleApplyArm11((float*)mtxfB,trans[0],trans[1],trans[2]);
  check(equal(mtxfA,mtxfB,sizeof(mtxfA)));
  // Translate + Z rotation stage. Inputs are finite here on purpose: sin/cos
  // reach this code from Math_SinS/Math_CosS, which read a sine table and
  // cannot return NaN. GCC compiles the reference to vmla/vnmls chains, which
  // associate identically to the separate VMUL/VADD used by the kernel (IEEE
  // addition is commutative, and VFP VMLA is unfused, so both round twice) but
  // differ in NaN payload/sign. The existing arm11_matrix_mult test documents
  // the same limitation as "NaN payload ignored".
  float sn,cs;
  sn=(float)((int32_t)(rnd()&65535)-32768)/32768.0f;
  cs=(float)((int32_t)(rnd()&65535)-32768)/32768.0f;
  for(int j=0;j<16;j++)mtxfA[j]=mtxfB[j]=finiteBits(rnd());
  float tfx=(float)((int32_t)rnd())/4096.0f;
  float tfy=(float)((int32_t)rnd())/4096.0f;
  float tfz=(float)((int32_t)rnd())/4096.0f;
  trzReference((float*)mtxfA,tfx,tfy,tfz,sn,cs);
  Soh3dsMatrixTranslateRotateZArm11((float*)mtxfB,tfx,tfy,tfz,sn,cs);
  check(equal(mtxfA,mtxfB,sizeof(mtxfA)));
  // The three rotation stages, finite inputs for the same NaN-payload reason.
  for(int j=0;j<16;j++)mtxfA[j]=mtxfB[j]=finiteBits(rnd());
  rotZReference((float*)mtxfA,sn,cs);
  Soh3dsMatrixRotateZStageArm11((float*)mtxfB,sn,cs);
  check(equal(mtxfA,mtxfB,sizeof(mtxfA)));
  for(int j=0;j<16;j++)mtxfA[j]=mtxfB[j]=finiteBits(rnd());
  rotYReference((float*)mtxfA,sn,cs);
  Soh3dsMatrixRotateYStageArm11((float*)mtxfB,sn,cs);
  check(equal(mtxfA,mtxfB,sizeof(mtxfA)));
  for(int j=0;j<16;j++)mtxfA[j]=mtxfB[j]=finiteBits(rnd());
  rotXReference((float*)mtxfA,sn,cs);
  Soh3dsMatrixRotateXStageArm11((float*)mtxfB,sn,cs);
  check(equal(mtxfA,mtxfB,sizeof(mtxfA)));
 }
}
'''
def main():
 with tempfile.TemporaryDirectory() as d:
  p=Path(d);(p/'test.c').write_text(harness()+r'''
void __aeabi_unwind_cpp_pr0(void){} void __aeabi_unwind_cpp_pr1(void){}
void _start(void){
 extendedCorrectness();
 register uint32_t r0 __asm__("r0")=1;register void* r1 __asm__("r1")=&failures;register uint32_t r2 __asm__("r2")=4;
 __asm__ volatile("mov r7,#4\nsvc #0":"+r"(r0):"r"(r1),"r"(r2):"r7","memory");
 r0=failures!=0;__asm__ volatile("mov r7,#1\nsvc #0"::"r"(r0):"r7","memory");__builtin_unreachable();}
''')
  asm=[str(ROOT/'src/compat3ds/arm11'/name) for name in ['resample.S','matrix_copy.S','audio_mix.S','matrix_vec.S','matrix_f2l.S','matrix_translate.S','matrix_trz.S','matrix_rot.S']]
  subprocess.run([str(SDK/'arm-none-eabi-gcc'),'-O2','-fwrapv','-fno-strict-aliasing','-march=armv6k','-mfpu=vfp','-mfloat-abi=hard','-fno-fast-math','-ffp-contract=off','-fno-builtin','-nostdlib','-Wl,-e,_start','-Wl,-Ttext=0x10000','-I'+str(ROOT/'src'),str(p/'test.c'),*asm,'-o',str(p/'test')],check=True)
  run=subprocess.run(['qemu-arm','-cpu','arm11mpcore',str(p/'test')],stdout=subprocess.PIPE)
  assert run.returncode==0 and run.stdout==bytes(4),(run.returncode,run.stdout.hex())
 print('PASS: 16384 audio kernel comparisons; 8192 game vector comparisons on ARM11')
if __name__=='__main__':main()
