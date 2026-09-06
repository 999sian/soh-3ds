// PICA200 TexEnv model — host-testable reference evaluator.
//
// Provenance: enum values and the C3D_TexEnv field structure come from devkitPro's
// libctru (include/3ds/gpu/enums.h, zlib) and citro3d (include/c3d/texenv.h, Zlib).
// The arithmetic below is the *expected* behaviour documented in
// DESIGN-PICA-BACKEND.md §3.3, and is authoritative only once confirmed on hardware
// by the T1/T2/T3 characterization tests. This evaluator exists so the combiner
// lowering can be verified on the host before any 3DS toolchain is involved.
//
// ponytail: header-only, float-based, no allocation. Precision quirks (8-bit
// quantisation) are deliberately NOT modelled — T3 establishes them, and the
// lowering's correctness does not depend on them.

#pragma once

#include <algorithm>
#include <cstdint>

namespace pica {

// GPU_TEVSRC (libctru gpu/enums.h). Only the values a Fast3D backend can produce.
enum class Src : uint8_t {
    PrimaryColor = 0x00, // interpolated vertex colour
    Texture0 = 0x03,
    Texture1 = 0x04,
    Texture2 = 0x05,
    PreviousBuffer = 0x0D,
    Constant = 0x0E,
    Previous = 0x0F,
};

// GPU_COMBINEFUNC (libctru gpu/enums.h).
enum class Func : uint8_t {
    Replace = 0x00,
    Modulate = 0x01,
    Add = 0x02,
    AddSigned = 0x03,
    Interpolate = 0x04,
    Subtract = 0x05,
    Dot3Rgb = 0x06,
    Dot3Rgba = 0x07,
    MultiplyAdd = 0x08,
    AddMultiply = 0x09,
};

// GPU_TEVOP_RGB, subset. `1 - x` is free on every operand.
enum class Op : uint8_t {
    SrcColor = 0x00,
    OneMinusSrcColor = 0x01,
    SrcAlpha = 0x02,
    OneMinusSrcAlpha = 0x03,
    SrcR = 0x04,
};

// GPU_TEVSCALE.
enum class Scale : uint8_t { X1 = 0, X2 = 1, X4 = 2 };

struct Stage {
    Func func = Func::Replace;
    Src src[3] = { Src::Previous, Src::Previous, Src::Previous };
    Op op[3] = { Op::SrcColor, Op::SrcColor, Op::SrcColor };
    Scale scale = Scale::X1;

    // Which Fast3D token each source reads out of the per-stage constant
    // register, or -1 where the source is a texture / vertex colour / previous
    // stage and needs no constant. bind_sources() fills this in; the backend
    // uses it to decide what to load into the register before a draw, and to
    // detect the one case the hardware cannot serve: a single stage needing two
    // different constant values at once.
    int8_t constToken[3] = { -1, -1, -1 };
};

// Values visible to a stage. `constant` is per-stage on real hardware (six
// independent constants); the lowering only ever needs one per stage.
struct Inputs {
    float primary = 0.0f;
    float tex0 = 0.0f;
    float tex1 = 0.0f;
    float tex2 = 0.0f;
    float constant = 0.0f;
    float previous = 0.0f;
    float previous_buffer = 0.0f;
};

constexpr float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline float fetch(Src s, const Inputs& in) {
    switch (s) {
        case Src::PrimaryColor:
            return in.primary;
        case Src::Texture0:
            return in.tex0;
        case Src::Texture1:
            return in.tex1;
        case Src::Texture2:
            return in.tex2;
        case Src::PreviousBuffer:
            return in.previous_buffer;
        case Src::Constant:
            return in.constant;
        case Src::Previous:
            return in.previous;
    }
    return 0.0f;
}

inline float apply_op(Op o, float v) {
    switch (o) {
        case Op::SrcColor:
        case Op::SrcAlpha:
        case Op::SrcR:
            return v;
        case Op::OneMinusSrcColor:
        case Op::OneMinusSrcAlpha:
            return 1.0f - v;
    }
    return v;
}

// Evaluate one TexEnv stage. Every result is clamped inside the op and again
// after scale — this double clamp is the source of the only visual deviation the
// port knowingly accepts (DESIGN §3.7). T2 confirms it on hardware.
inline float eval(const Stage& st, const Inputs& in) {
    const float s0 = apply_op(st.op[0], fetch(st.src[0], in));
    const float s1 = apply_op(st.op[1], fetch(st.src[1], in));
    const float s2 = apply_op(st.op[2], fetch(st.src[2], in));

    float r;
    switch (st.func) {
        case Func::Replace:
            r = s0;
            break;
        case Func::Modulate:
            r = s0 * s1;
            break;
        case Func::Add:
            r = s0 + s1;
            break;
        case Func::AddSigned:
            r = s0 + s1 - 0.5f;
            break;
        case Func::Interpolate:
            r = s1 + (s0 - s1) * s2;
            break; // mix(s1, s0, s2)
        case Func::Subtract:
            r = s0 - s1;
            break;
        case Func::Dot3Rgb:
        case Func::Dot3Rgba:
            r = 4.0f * (s0 - 0.5f) * (s1 - 0.5f);
            break;
        case Func::MultiplyAdd:
            r = s0 * s1 + s2;
            break;
        case Func::AddMultiply:
            r = std::min(s0 + s1, 1.0f) * s2;
            break;
        default:
            r = s0;
            break;
    }

    const float mult = st.scale == Scale::X1 ? 1.0f : (st.scale == Scale::X2 ? 2.0f : 4.0f);
    return clamp01(clamp01(r) * mult);
}

} // namespace pica
