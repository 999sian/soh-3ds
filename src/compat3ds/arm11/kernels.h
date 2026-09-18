#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// All pointers are at least halfword-aligned; table is word-aligned.
// Writes count PCM samples, returns the advanced input, updates 16-bit phase.
// pitchStep is twice the unsigned 16-bit game pitch. count can be zero.
int16_t* Soh3dsResampleArm11(int16_t* input, int16_t* output, uint32_t* phase,
                           uint32_t pitchStep, uint32_t count, const int16_t table[64][4]);
// out[-2..-1] holds history; writes exactly eight samples. ins and table
// are disjoint from out; table has two rows of eight signed coefficients.
void Soh3dsAdpcmHalfArm11(int16_t* out, const int16_t ins[8], const int16_t table[2][8]);
void Soh3dsMixArm11(const int16_t* in, int16_t* out, int32_t gain, uint32_t count);
// In-place eight-tap FIR; count is a multiple of eight (zero is a no-op).
// Updates history with original input samples. The three ranges are disjoint
// and at least halfword-aligned. Coefficients are already averaged by mixer.c.
void Soh3dsFilterArm11(int16_t* buffer, int16_t history[8], const int16_t coefficients[8], uint32_t count);
void Soh3dsEnvMixerArm11(const int16_t* in, int16_t* const dry[2], int16_t* const wet[2],
                        uint32_t count, const uint16_t vols[3], const uint16_t rates[3],
                        const int32_t negs[4], uint32_t swap);
void Soh3dsAddMixerArm11(const int16_t* in, int16_t* out, uint32_t count);
void Soh3dsInterleaveArm11(const int16_t* left, const int16_t* right, int16_t* dest, uint32_t groups);
void Soh3dsMtxF2LArm11(const float mf[4][4], int32_t* m);
void Soh3dsInterlArm11(const int16_t* in, int16_t* out, uint32_t groups);
void Soh3dsMatrixTranslateApplyArm11(float* matrix, float x, float y, float z);
void Soh3dsMatrixScaleApplyArm11(float* matrix, float x, float y, float z);
void Soh3dsMatrixTranslateRotateZArm11(float* matrix, float tx, float ty, float tz,
                                       float sinZ, float cosZ);
void Soh3dsMatrixRotateZStageArm11(float* matrix, float sinZ, float cosZ);
void Soh3dsMatrixRotateYStageArm11(float* matrix, float sinY, float cosY);
void Soh3dsMatrixRotateXStageArm11(float* matrix, float sinX, float cosX);
#ifdef __cplusplus
}
#endif
