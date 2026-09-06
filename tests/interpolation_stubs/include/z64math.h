#pragma once
#include <cstdint>
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using s16 = int16_t;
using s32 = int32_t;
using f32 = float;
struct Mtx { int32_t words[16]; };
struct MtxF { float mf[4][4]; };
struct Vec3f { float x, y, z; };
struct Vec3s { s16 x, y, z; };
