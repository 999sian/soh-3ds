// Full active-backend draw/texture/source-binding checks with RGBA8 readback.
#include <3ds.h>
#include <citro3d.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <sys/stat.h>
#include <vector>
#include "gfx_citro3d.h"
#include "ctr_log.h"

namespace {
constexpr int W=16,H=16;
using Color=std::array<int,4>;
int byte(float value) { return std::clamp(int(value*255+0.5f),0,255); }
uint64_t formula(int a,int b,int c,int d) { return a|(b<<4)|(c<<8)|(d<<12); }
}
int main() {
    unsigned samples=0, failures=0;
    {
        Fast::GfxRenderingAPICitro3D api;
        api.Init(); consoleDebugInit(debugDevice_SVC);
        if(!api.IsInitialized()) { fprintf(stderr,"RENDER ERROR init\n"); return 1; }
        const int fb=api.CreateFramebuffer();
        api.UpdateFramebufferParameters(fb,W,H,0,false,true,false,false);
        auto* pixels=(u8*)linearAlloc(W*H*4);
        if(!pixels) return 1;
        mkdir("sdmc:/3ds",0777); mkdir("sdmc:/3ds/soh-tev",0777);
        FILE* csv=fopen("sdmc:/3ds/soh-tev/renderer-v2.csv","w");
        if(csv) fputs("mode,texture_alpha,expected_r,expected_g,expected_b,expected_a,r,g,b,a\n",csv);
        fprintf(stderr,"RENDER BEGIN v2 active platform/3ds RGBA8\n");
        const Color prim={201,73,149,255}, env={17,189,39,255}, fog={40,110,210,128};
        for(int mode=0;mode<10;++mode) for(int alpha : {7,8,48,49,128,255}) {
            if(!aptMainLoop()) goto finished;
            const Color tex={91,173,227,alpha};
            const bool hasFog=mode==3 || mode==6 || mode==9;
            const bool grayscale=mode>=8;
            const bool threshold=mode==4 || mode==6;
            const bool edge=mode==5;
            const bool blend=(mode==3 || mode==4 || mode==6);
            uint64_t rgb=(mode==0 || grayscale) ? formula(0,0,0,8) :
                         mode==2 ? formula(1,2,9,2) : formula(8,0,1,0);
            const uint64_t id=rgb|(formula(0,0,0,9)<<16)|
                (mode==7 ? (formula(13,0,2,0)<<32)|(formula(0,0,0,13)<<48) : 0);
            const uint64_t options=1|(hasFog ? 2 : 0)|(threshold ? 32 : 0)|(edge ? 4 : 0)|
                (mode==7 ? 16 : 0)|(grayscale ? 128 : 0);
            auto* program=api.CreateAndLoadNewShader(id,options);
            const auto texture=api.NewTexture(); api.SelectTexture(0,texture);
            std::array<uint8_t,8*8*4> data{};
            for(size_t i=0;i<data.size();++i) data[i]=tex[i%4];
            api.UploadTexture(data.data(),8,8); api.SetSamplerParameters(0,false,0,0);
            std::vector<float> vertices(6*program->strideFloats,0);
            const float xy[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
            for(int i=0;i<6;++i) {
                float* v=vertices.data()+i*program->strideFloats;
                v[0]=xy[i][0];v[1]=xy[i][1];v[2]=0;v[3]=1;
                v[program->textureOffsets[0]]=0.5f;v[program->textureOffsets[0]+1]=0.5f;
                for(int input=0;input<program->numInputs;++input)
                    for(int ch=0;ch<4;++ch) v[program->inputOffsets[input]+ch]=(input==0?prim:env)[ch]/255.0f;
                if(hasFog) for(int ch=0;ch<4;++ch) v[program->fogOffset+ch]=fog[ch]/255.0f;
                if(grayscale) for(int ch=0;ch<4;++ch) v[program->grayscaleOffset+ch]=1;
            }
            api.StartFrame(); api.StartDrawToFramebuffer(fb,0);
            api.SetViewport(0,0,W,H);api.SetScissor(0,0,W,H);
            api.SetDepthTestAndMask(false,false);api.SetUseAlpha(blend || edge);
            api.ClearFramebuffer(true,false);
            api.DrawTriangles(vertices.data(),vertices.size(),2);api.EndFrame();
            void* storage=api.GetFramebufferTextureId(fb);
            if(!storage) { fprintf(stderr,"RENDER ERROR framebuffer storage\n"); ++failures; continue; }
            C3D_SyncDisplayTransfer((u32*)storage,GX_BUFFER_DIM(W,H),(u32*)pixels,GX_BUFFER_DIM(W,H),
                GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8)|GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8));
            GSPGPU_InvalidateDataCache(pixels,W*H*4);
            const auto* p=pixels+4*(W*(H/2)+W/2);
            Color expected{};
            const bool discard=(threshold && alpha<8)||(edge && alpha<=48);
            for(int ch=0;ch<3;++ch) {
                float value=(mode==0 || grayscale) ? tex[ch]/255.0f : mode==2 ?
                    (prim[ch]*alpha+env[ch]*(255-alpha))/(255.0f*255.0f) : tex[ch]*prim[ch]/(255.0f*255.0f);
                if(hasFog) value=value*(1-fog[3]/255.0f)+fog[ch]/255.0f*(fog[3]/255.0f);
                if(mode==7) value*=env[ch]/255.0f;
                if(grayscale) {
                    value=(tex[0]+tex[1]+tex[2])/(3*255.0f);
                    if(hasFog) value=value*(1-fog[3]/255.0f)+
                        (fog[0]+fog[1]+fog[2])/(3*255.0f)*(fog[3]/255.0f);
                }
                if(blend) value*=alpha/255.0f;
                expected[ch]=discard ? 0 : byte(value);
            }
            // Both blend paths use As + Ad*(1-As); the opaque clear has Ad=1.
            // Alpha-test correctness is observed through RGB coverage instead.
            expected[3]=255;
            const Color actual={p[3],p[2],p[1],p[0]};
            bool ok=true;
            for(int ch=0;ch<4;++ch) ok &= std::abs(actual[ch]-expected[ch])<=2;
            if(discard) ok &= actual[0]==0 && actual[1]==0 && actual[2]==0;
            else ok &= actual[0]!=0 || actual[1]!=0 || actual[2]!=0;
            failures+=!ok; ++samples;
            if(csv) fprintf(csv,"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",mode,alpha,
                expected[0],expected[1],expected[2],expected[3],actual[0],actual[1],actual[2],actual[3]);
            fprintf(stderr,"RENDER mode=%d alpha=%d expected=%d,%d,%d,%d actual=%d,%d,%d,%d %s\n",
                mode,alpha,expected[0],expected[1],expected[2],expected[3],actual[0],actual[1],actual[2],actual[3],ok?"PASS":"FAIL");
            api.DeleteTexture(texture);
        }
finished:
        if(csv) fclose(csv);
        linearFree(pixels);
        fprintf(stderr,"RENDER END samples=%u failures=%u saved=%d\n",samples,failures,csv!=nullptr);
    }
    return samples==60 && failures==0 ? 0 : 1;
}
