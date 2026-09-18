// Standalone GPU characterization, deliberately independent of game assets.
// Records measurements rather than declaring the provisional host model true.
#include <3ds.h>
#include <citro3d.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include "ctr_log.h"

extern "C" const unsigned char tev_precision_shbin[];
extern "C" const unsigned int tev_precision_shbin_size;

namespace {
constexpr int W=16, H=16;
struct Vertex { float position[4], color[4]; };
u32 gray(int v) { return u32(v)*0x01010101U; }
int rounded(int n) { return std::clamp((std::max(0,n)+127)/255,0,255); }
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
    if(!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) { ctr_log("TEV ERROR init\n"); gfxExit(); return 1; }
    auto* shader=DVLB_ParseFile((u32*)tev_precision_shbin,tev_precision_shbin_size);
    shaderProgram_s program{};
    shaderProgramInit(&program);
    shaderProgramSetVsh(&program,&shader->DVLE[0]); C3D_BindProgram(&program);
    auto* target=C3D_RenderTargetCreate(W,H,GPU_RB_RGBA8,-1);
    auto* vertices=(Vertex*)linearAlloc(6*sizeof(Vertex));
    auto* pixels=(u8*)linearAlloc(W*H*4);
    if(!target || !vertices || !pixels) { ctr_log("TEV ERROR allocation\n"); return 1; }
    auto* attr=C3D_GetAttrInfo(); AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr,0,GPU_FLOAT,4); AttrInfo_AddLoader(attr,1,GPU_FLOAT,4);
    auto* buf=C3D_GetBufInfo(); BufInfo_Init(buf); BufInfo_Add(buf,vertices,sizeof(Vertex),2,0x10);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_ALL);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_TexEnvBufUpdate(C3D_Both,0);
    mkdir("sdmc:/3ds",0777); mkdir("sdmc:/3ds/soh-tev",0777);
    FILE* csv=fopen("sdmc:/3ds/soh-tev/precision-v1.csv","w");
    if(csv) fputs("mode,a,b,c,expected,r,g,b_out,alpha\n",csv);
    ctr_log("TEV BEGIN v1; RGBA8; measurements, not hardware assertions\n");
    const float xy[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    unsigned samples=0, differences=0, channelErrors=0; int maximum=0;
    for(int mode=0;mode<5;++mode)
    for(int a : {0,1,7,8,48,49,128,254,255})
    for(int b : {0,1,8,49,128,254,255})
    for(int c : {1,128,255}) {
        if(mode>=3 && (b!=0 || c!=1)) continue;
        if(!aptMainLoop()) goto finished;
        for(int i=0;i<6;++i) {
            vertices[i].position[0]=xy[i][0]; vertices[i].position[1]=xy[i][1];
            vertices[i].position[2]=-0.5f; vertices[i].position[3]=1;
            for(float& color : vertices[i].color) color=c/255.0f;
        }
        GSPGPU_FlushDataCache(vertices,6*sizeof(Vertex));
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C3D_RenderTargetClear(target,C3D_CLEAR_COLOR,0x3d3d3d3d,0);
        C3D_FrameDrawOn(target); C3D_SetViewport(0,0,W,H);
        C3D_AlphaTest(false,GPU_ALWAYS,0);
        for(int i=0;i<6;++i) stage(i,GPU_REPLACE,GPU_PREVIOUS,GPU_PREVIOUS,GPU_PREVIOUS,0);
        if(mode==0) { // A*B, both read from exact byte-valued constants.
            stage(0,GPU_REPLACE,GPU_CONSTANT,GPU_CONSTANT,GPU_CONSTANT,gray(a));
            stage(1,GPU_MODULATE,GPU_PREVIOUS,GPU_CONSTANT,GPU_CONSTANT,gray(b));
        } else if(mode==1) {
            stage(0,GPU_REPLACE,GPU_CONSTANT,GPU_CONSTANT,GPU_CONSTANT,gray(a));
            stage(1,GPU_INTERPOLATE,GPU_PREVIOUS,GPU_CONSTANT,GPU_PRIMARY_COLOR,gray(b));
        } else if(mode==2) {
            stage(0,GPU_MODULATE,GPU_CONSTANT,GPU_PRIMARY_COLOR,GPU_CONSTANT,gray(a));
            stage(1,GPU_MULTIPLY_ADD,GPU_CONSTANT,GPU_PRIMARY_COLOR,GPU_PREVIOUS,gray(b));
            C3D_TexEnvOpRgb(C3D_GetTexEnv(1),GPU_TEVOP_RGB_SRC_COLOR,
                GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR,GPU_TEVOP_RGB_SRC_COLOR);
            C3D_TexEnvOpAlpha(C3D_GetTexEnv(1),GPU_TEVOP_A_SRC_ALPHA,
                GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA,GPU_TEVOP_A_SRC_ALPHA);
        } else {
            stage(0,GPU_REPLACE,GPU_CONSTANT,GPU_CONSTANT,GPU_CONSTANT,gray(a));
            C3D_AlphaTest(true,mode==3 ? GPU_GEQUAL : GPU_GREATER,mode==3 ? 8 : 48);
        }
        C3D_DrawArrays(GPU_TRIANGLES,0,6); C3D_FrameEnd(0);
        C3D_SyncDisplayTransfer((u32*)target->frameBuf.colorBuf,GX_BUFFER_DIM(W,H),
            (u32*)pixels,GX_BUFFER_DIM(W,H),GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|
            GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_TILED(0));
        GSPGPU_InvalidateDataCache(pixels,W*H*4);
        const u8* p=pixels+4*(W*(H/2)+W/2);
        const int expected=mode==0 ? rounded(a*b) : mode==1 ? rounded(a*c+b*(255-c)) :
            mode==2 ? rounded(rounded(a*c)*255+b*(255-c)) :
            (mode==3 ? a>=8 : a>48) ? a : 61;
        const int delta=std::abs(int(p[3])-expected);
        maximum=std::max(maximum,delta); differences+=delta!=0;
        channelErrors += !(p[0]==p[1] && p[1]==p[2] && p[2]==p[3]); ++samples;
        if(csv) fprintf(csv,"%d,%d,%d,%d,%d,%u,%u,%u,%u\n",mode,a,b,c,expected,p[3],p[2],p[1],p[0]);
        ctr_log("TEV %d %d %d %d expected=%d rgba=%u,%u,%u,%u\n",mode,a,b,c,expected,p[3],p[2],p[1],p[0]);
    }
finished:
    if(csv) fclose(csv);
    ctr_log("TEV END samples=%u differences=%u max=%d channel_errors=%u saved=%d\n",
        samples,differences,maximum,channelErrors,csv!=nullptr);
    C3D_RenderTargetDelete(target); linearFree(vertices); linearFree(pixels);
    shaderProgramFree(&program); DVLB_Free(shader); C3D_Fini();
    for(int i=0;i<300 && aptMainLoop();++i) {
        hidScanInput(); if(hidKeysDown()&KEY_START) break; gspWaitForVBlank();
    }
    gfxExit(); return samples==585 && channelErrors==0 ? 0 : 1;
}
