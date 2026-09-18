// Standalone GPU characterization, deliberately independent of game assets.
// Records measurements rather than declaring the provisional host model true.
#include <3ds.h>
#include <citro3d.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <sys/stat.h>
#include "ctr_log.h"

extern "C" const unsigned char gpu_lighting_shbin[];
extern "C" const unsigned int gpu_lighting_shbin_size;

namespace {
constexpr int W=16, H=16;
struct Vertex { float position[4], color[4]; };
void uniform24(int loc,float x,float y,float z,float w) {
    u32 a=f32tof24(w), b=f32tof24(z), c=f32tof24(y), d=f32tof24(x);
    u32 packed[3]={(a<<8)|(b>>16),(b<<16)|(c>>8),(c<<24)|d};
    GPUCMD_AddWrite(GPUREG_VSH_FLOATUNIFORM_CONFIG,loc);
    GPUCMD_AddWrites(GPUREG_VSH_FLOATUNIFORM_DATA,packed,3);
}
void stage(int index, GPU_COMBINEFUNC function, GPU_TEVSRC a,
           GPU_TEVSRC b, GPU_TEVSRC c, u32 constant) {
    auto* env=C3D_GetTexEnv(index);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env,C3D_Both,a,b,c);
    C3D_TexEnvFunc(env,C3D_Both,function);
    C3D_TexEnvColor(env,constant);
}
}

int main() {
    gfxInitDefault(); ctr_log_init();
    if(!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) { ctr_log("LIGHT ERROR init\n"); gfxExit(); return 1; }
    auto* shader=DVLB_ParseFile((u32*)gpu_lighting_shbin,gpu_lighting_shbin_size);
    shaderProgram_s program{};
    shaderProgramInit(&program);
    shaderProgramSetVsh(&program,&shader->DVLE[0]); C3D_BindProgram(&program);
    auto* target=C3D_RenderTargetCreate(W,H,GPU_RB_RGBA8,-1);
    auto* vertices=(Vertex*)linearAlloc(6*sizeof(Vertex));
    auto* pixels=(u8*)linearAlloc(W*H*4);
    if(!target || !vertices || !pixels) { ctr_log("LIGHT ERROR allocation\n"); return 1; }
    auto* attr=C3D_GetAttrInfo(); AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr,0,GPU_FLOAT,4); AttrInfo_AddLoader(attr,1,GPU_FLOAT,4);
    auto* buf=C3D_GetBufInfo(); BufInfo_Init(buf); BufInfo_Add(buf,vertices,sizeof(Vertex),2,0x10);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_ALL);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_TexEnvBufUpdate(C3D_Both,0);
    mkdir("sdmc:/3ds",0777); mkdir("sdmc:/3ds/soh-lighting",0777);
    FILE* csv=fopen("sdmc:/3ds/soh-lighting/precision.csv","w");
    if(csv) fputs("upload,normal,ambient,light,k,offset,cx,intensity,expected,r,g,b,alpha\n",csv);
    ctr_log("LIGHT BEGIN v1; RGBA8; measurements, not hardware assertions\n");
    const float xy[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    unsigned samples=0, differences=0, channelErrors=0; int maximum=0;
    int coeffLoc=shaderInstanceGetUniformLocation(program.vertexShader,"coeff");
    int ambLoc=shaderInstanceGetUniformLocation(program.vertexShader,"ambient");
    int lightLoc=shaderInstanceGetUniformLocation(program.vertexShader,"light");
    int scaleLoc=shaderInstanceGetUniformLocation(program.vertexShader,"scales");
    // Neighbours around integer contribution thresholds, normalized direction vectors.
    for(int upload : {32,24})
    for(int ni : {-128,-127,-1,0,1,63,127})
    for(int a=0;a<256;++a)
    for(int b : {1,101,255})
    for(int k : {0,1,50,100})
    for(int offset : {-1,0,1}) {
        if(k==0 ? !(ni==0 && b==1 && offset==0) : !(a==0 || a==80 || a==254 || a==255)) continue;
        int ny=k==50 ? -63 : 0, nz=k==50 ? 127 : 0;
        float cx=float(k)/float(b);
        if(cx>1) continue;
        if(offset) cx=std::nextafter(cx,offset<0 ? 0.0f : 2.0f);
        float cy=std::sqrt(std::max(0.0f,1-cx*cx))*0.6f;
        float cz=std::sqrt(std::max(0.0f,1-cx*cx))*0.8f;
        float intensity=0;
        intensity+=ni*cx; intensity+=ny*cy; intensity+=nz*cz;
        intensity*=1.0f/127.0f;
        int expected=a; if(intensity>0) expected+=intensity*b;
        expected=std::min(expected,255);
        if(!aptMainLoop()) goto finished;
        for(int i=0;i<6;++i) {
            vertices[i].position[0]=xy[i][0]; vertices[i].position[1]=xy[i][1];
            vertices[i].position[2]=-0.5f; vertices[i].position[3]=1;
            vertices[i].color[0]=ni; vertices[i].color[1]=ny; vertices[i].color[2]=nz; vertices[i].color[3]=1;
        }
        GSPGPU_FlushDataCache(vertices,6*sizeof(Vertex));
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C3D_RenderTargetClear(target,C3D_CLEAR_COLOR,0x3d3d3d3d,0);
        C3D_FrameDrawOn(target); C3D_SetViewport(0,0,W,H);
        C3D_AlphaTest(false,GPU_ALWAYS,0);
        for(int i=0;i<6;++i) stage(i,GPU_REPLACE,i==0 ? GPU_PRIMARY_COLOR : GPU_PREVIOUS,GPU_PREVIOUS,GPU_PREVIOUS,0);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,coeffLoc,cx,cy,cz,0);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,ambLoc,a,a,a,0);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,lightLoc,b,b,b,0);
        C3D_FVUnifSet(GPU_VERTEX_SHADER,scaleLoc,0,1.0f/127,1.0f/255,255);
        if(upload==24) {
            C3D_UpdateUniforms(GPU_VERTEX_SHADER);
            uniform24(coeffLoc,cx,cy,cz,0);
            uniform24(scaleLoc,0,1.0f/127,1.0f/255,255);
        }
        C3D_DrawArrays(GPU_TRIANGLES,0,6); C3D_FrameEnd(0);
        C3D_SyncDisplayTransfer((u32*)target->frameBuf.colorBuf,GX_BUFFER_DIM(W,H),
            (u32*)pixels,GX_BUFFER_DIM(W,H),GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|
            GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_TILED(0));
        GSPGPU_InvalidateDataCache(pixels,W*H*4);
        const u8* p=pixels+4*(W*(H/2)+W/2);
        const int delta=std::abs(int(p[3])-expected);
        maximum=std::max(maximum,delta); differences+=delta!=0;
        channelErrors += !(p[0]==255 && p[1]==p[2] && p[2]==p[3]); ++samples;
        if(csv) fprintf(csv,"%d,%d,%d,%d,%d,%d,%.9g,%.9g,%d,%u,%u,%u,%u\n",upload,ni,a,b,k,offset,cx,intensity,expected,p[3],p[2],p[1],p[0]);
        if(delta || samples<5) ctr_log("LIGHT upload=%d n=%d a=%d light=%d k=%d offset=%d cx=%.9g intensity=%.9g expected=%d rgba=%u,%u,%u,%u\n",upload,ni,a,b,k,offset,cx,intensity,expected,p[3],p[2],p[1],p[0]);
    }
finished:
    if(csv) fclose(csv);
    ctr_log("LIGHT END samples=%u differences=%u max=%d channel_errors=%u saved=%d\n",
        samples,differences,maximum,channelErrors,csv!=nullptr);
    C3D_RenderTargetDelete(target); linearFree(vertices); linearFree(pixels);
    shaderProgramFree(&program); DVLB_Free(shader); C3D_Fini();
    for(int i=0;i<300 && aptMainLoop();++i) {
        hidScanInput(); if(hidKeysDown()&KEY_START) break; gspWaitForVBlank();
    }
    gfxExit(); return differences==0 && channelErrors==0 ? 0 : 1;
}
