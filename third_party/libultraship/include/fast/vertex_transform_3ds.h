#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Input: 16-byte F3DVtx records with signed XYZ halfwords at offsets 0/2/4.
// Output: 32-byte LoadedVertex records; only x/y/z/w are written.
// Source is halfword-aligned, matrix/output word-aligned. Output must be
// disjoint from source and matrix. A zero count permits null pointers.
void Soh3dsTransformVerticesArm11(const void* input, void* output,
                                const float matrix[4][4], uint32_t count);
#ifdef __cplusplus
}
#endif
