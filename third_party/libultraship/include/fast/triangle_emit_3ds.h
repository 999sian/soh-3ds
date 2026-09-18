#pragma once
#include <stdint.h>

// Whole-triangle kernel for shade RGBA, optional uniform RGBA input, and
// optional texture 0 without clamp attributes. All buffers are word aligned
// and disjoint. vertices addresses three actual 32-byte LoadedVertex records.
// Flags: texture=1, uniform=2, invertY=4, convertZ=8, rectangle=16.
struct Soh3dsTriangleEmitParams {
    uint32_t flags;
    float uMul, vMul, uAdd, vAdd, halfU, halfV;
    float uniform[4];
};
// Stock fog layout: two UV sets, fog RGBA, shade RGB, uniform RGB.
// Flags: alpha=1, invertY=4, convertZ=8, rectangle=16. Output is 18 words
// per opaque vertex or 20 with the two precomputed alpha attributes enabled.
struct Soh3dsFogEmitParams {
    uint32_t flags;
    struct Texture { float uMul, vMul, uAdd, vAdd, halfU, halfV; } texture[2];
    float fog[3];
    float uniform[3];
    float alpha[2];
};
#ifdef __cplusplus
extern "C" {
#endif
float* Soh3dsEmitTriangleArm11(float* output, const void* const vertices[3],
                              const struct Soh3dsTriangleEmitParams* params);
float* Soh3dsEmitFogTriangleArm11(float* output, const void* const vertices[3],
                                      const struct Soh3dsFogEmitParams* params);
#ifdef __cplusplus
}
#endif
