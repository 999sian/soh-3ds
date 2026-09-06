#pragma once

#include <libultraship/libultra/gbi.h>

extern s32 gPauseFrameBuffer;
extern s32 gBlurFrameBuffer;
extern s32 gReusableFrameBuffer;
extern s32 gN64ResFrameBuffer;
#ifdef __3DS__
extern s32 gBottomScreenFrameBuffer;
// Same LCD; draws under this id get the Soh3dsSetBottomPassZoom transform.
extern s32 gBottomScreenZoomFrameBuffer;
// Nonzero while display-list code is drawing into the bottom screen
// (OTRGetAspectRatio() then reports 4:3 for edge anchoring).
extern int gSoh3dsBottomPass;
// Zoom the bottom pass about an NDC centre (renderer bridge; weak so probe
// binaries without the citro3d backend still link).
void Soh3dsSetBottomPassZoom(float scale, float ndcCenterX, float ndcCenterY) __attribute__((weak));
// Bottom-screen UI while playing (soh/Enhancements/dualscreen3ds/BottomScreen3DS.c).
enum Soh3dsBottomTab { SOH3DS_TAB_ITEMS, SOH3DS_TAB_GEAR, SOH3DS_TAB_MAP, SOH3DS_TAB_TRACKER, SOH3DS_TAB_SETTINGS, SOH3DS_TAB_COUNT };
int Soh3dsBottomScreen_Tab(void);
void Soh3dsBottomScreen_Draw(struct PlayState* play);
#endif

void FB_CreateFramebuffers(void);
void FB_CopyToFramebuffer(Gfx** gfxp, s32 fb_src, s32 fb_dest, u8 oncePerFrame, u8* hasCopied);
void FB_WriteFramebufferSliceToCPU(Gfx** gfxp, void* buffer, u8 byteSwap);
void FB_DrawFromFramebuffer(Gfx** gfxp, s32 fb, u8 alpha);
void FB_DrawFromFramebufferScaled(Gfx** gfxp, s32 fb, u8 alpha, float scaleX, float scaleY);
