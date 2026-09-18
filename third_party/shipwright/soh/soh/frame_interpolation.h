#pragma once

#include "frame_interpolation_guard.h"

#include "include/z64math.h"

#ifdef __cplusplus

#include <unordered_map>

std::unordered_map<Mtx*, MtxF> FrameInterpolation_Interpolate(float step);

extern "C" {

#endif

void FrameInterpolation_StartRecord(void);

// SoH-3DS: release the retained recording trees (see frame_interpolation.cpp).
// Recent nodes are reused across ticks and obsolete generations are reclaimed
// incrementally. Call this at a scene transition to release both trees at once.
void FrameInterpolation_ShrinkRecording(void);

void FrameInterpolation_StopRecord(void);

void FrameInterpolation_DontInterpolateCamera(void);

int FrameInterpolation_GetCameraEpoch(void);

void FrameInterpolation_RecordActorPosRotMatrix(void);

void FrameInterpolation_RecordMatrixPush(void);

void FrameInterpolation_RecordMatrixPop(void);

void FrameInterpolation_RecordMatrixPut(MtxF* src);

void FrameInterpolation_RecordMatrixMult(MtxF* mf, u8 mode);

void FrameInterpolation_RecordMatrixTranslate(f32 x, f32 y, f32 z, u8 mode);

void FrameInterpolation_RecordMatrixScale(f32 x, f32 y, f32 z, u8 mode);

void FrameInterpolation_RecordMatrixRotate1Coord(u32 coord, f32 value, u8 mode);

void FrameInterpolation_RecordMatrixRotateZYX(s16 x, s16 y, s16 z, u8 mode);

void FrameInterpolation_RecordMatrixTranslateRotateZYX(Vec3f* translation, Vec3s* rotation);

void FrameInterpolation_RecordMatrixSetTranslateRotateYXZ(f32 translateX, f32 translateY, f32 translateZ, Vec3s* rot);

void FrameInterpolation_RecordMatrixMtxFToMtx(MtxF* src, Mtx* dest);

void FrameInterpolation_RecordMatrixToMtx(Mtx* dest, char* file, s32 line);

void FrameInterpolation_RecordMatrixReplaceRotation(MtxF* mf);

void FrameInterpolation_RecordMatrixRotateAxis(f32 angle, Vec3f* axis, u8 mode);

void FrameInterpolation_RecordSkinMatrixMtxFToMtx(MtxF* src, Mtx* dest);

#ifdef __cplusplus
}
#endif

#if defined(__3DS__) && !defined(SOH_FRAME_INTERPOLATION_IMPLEMENTATION)
#define FrameInterpolation_StartRecord(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_StartRecord, __VA_ARGS__)
#define FrameInterpolation_ShrinkRecording(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_ShrinkRecording, __VA_ARGS__)
#define FrameInterpolation_StopRecord(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_StopRecord, __VA_ARGS__)
#define FrameInterpolation_DontInterpolateCamera(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_DontInterpolateCamera, __VA_ARGS__)
#define FrameInterpolation_RecordActorPosRotMatrix(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordActorPosRotMatrix, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixPush(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixPush, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixPop(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixPop, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixPut(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixPut, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixMult(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixMult, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixTranslate(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixTranslate, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixScale(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixScale, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixRotate1Coord(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixRotate1Coord, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixRotateZYX(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixRotateZYX, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixTranslateRotateZYX(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixTranslateRotateZYX, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixSetTranslateRotateYXZ(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixSetTranslateRotateYXZ, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixMtxFToMtx(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixMtxFToMtx, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixToMtx(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixToMtx, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixReplaceRotation(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixReplaceRotation, __VA_ARGS__)
#define FrameInterpolation_RecordMatrixRotateAxis(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordMatrixRotateAxis, __VA_ARGS__)
#define FrameInterpolation_RecordSkinMatrixMtxFToMtx(...) SOH_FRAME_INTERPOLATION_CALL(FrameInterpolation_RecordSkinMatrixMtxFToMtx, __VA_ARGS__)
#define FrameInterpolation_GetCameraEpoch() (gSoh3dsFrameInterpolationEnabled ? (FrameInterpolation_GetCameraEpoch)() : 0)
#endif
