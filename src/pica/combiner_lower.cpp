#include "combiner_lower.hpp"

namespace pica {

namespace {

Stage make(Func f, Src s0, Src s1, Src s2) {
    Stage st;
    st.func = f;
    st.src[0] = s0;
    st.src[1] = s1;
    st.src[2] = s2;
    return st;
}

// Placeholder sources; the caller rewrites these through source_to_tev() once it
// knows the cycle and which input is varying. Keeping the shape decision separate
// from source binding is what lets the host tests drive the lowering directly.
constexpr Src A = Src::Texture0;
constexpr Src B = Src::Texture1;
constexpr Src C = Src::Texture2;
constexpr Src D = Src::Constant;

} // namespace

Plan lower_channel(const int c[4]) {
    Plan p;

    // Precedence must match libultraship's append_formula. Where several
    // conditions hold at once the cases agree mathematically, but following the
    // same order keeps this backend bit-identical to the reference and picks the
    // cheaper stage count.
    if (c[2] == SHADER_0) { // C == 0  ->  D
        p.shape = Shape::Single;
        p.stage[0] = make(Func::Replace, D, D, D);
        p.count = 1;
        return p;
    }
    if (c[1] == SHADER_0 && c[3] == SHADER_0) { // B == 0 && D == 0  ->  A * C
        p.shape = Shape::Multiply;
        p.stage[0] = make(Func::Modulate, A, C, C);
        p.count = 1;
        return p;
    }
    if (c[1] == c[3]) { // B == D  ->  B + (A - B) * C, exact via INTERPOLATE
        p.shape = Shape::Mix;
        p.stage[0] = make(Func::Interpolate, A, B, C);
        p.count = 1;
        return p;
    }

    // General: (A - B) * C + D.
    //
    // MULTIPLY_ADD alone cannot express this when B != 0 — fma() has no subtract
    // slot, and the operand modifiers only offer `1 - x`, i.e. subtraction from
    // the constant 1 rather than from an arbitrary source. ADD_MULTIPLY is the
    // wrong shape and its inner min() prevents reconstructing A - B. So two
    // stages, and PICA's clamp on the first is the accepted deviation.
    p.shape = Shape::General;
    p.stage[0] = make(Func::Subtract, A, B, B);
    p.stage[1] = make(Func::MultiplyAdd, Src::Previous, C, D);
    p.count = 2;
    return p;
}

namespace {

// Recover which RDP term a placeholder stands for. Src::Previous is a real
// source that lower_channel() emits deliberately, so it maps to no term.
int sentinel_term(Src s) {
    switch (s) {
        case A:
            return 0;
        case B:
            return 1;
        case C:
            return 2;
        case D:
            return 3;
        default:
            return -1;
    }
}

} // namespace

void bind_sources(Plan& p, const int c[4], int cycle, int varying) {
    for (int i = 0; i < p.count; ++i) {
        Stage& st = p.stage[i];
        for (int k = 0; k < 3; ++k) {
            const int term = sentinel_term(st.src[k]);
            if (term < 0) {
                continue; // already a real source
            }
            const int token = c[term];
            st.src[k] = source_to_tev(token, cycle, varying);
            // Operand selection is token-specific:
            //   SHADER_1     -> 1 - 0 off the zeroed constant register
            //   TEXEL0A/1A   -> the texture's *alpha* used as an RGB operand;
            //                   selecting colour here renders the texture's RGB
            //                   wherever the combiner asked for its alpha
            //   everything else reads the source directly
            if (token == SHADER_1) {
                st.op[k] = Op::OneMinusSrcColor;
            } else if (token == SHADER_TEXEL0A || token == SHADER_TEXEL1A) {
                st.op[k] = Op::SrcAlpha;
            } else {
                st.op[k] = Op::SrcColor;
            }
            st.constToken[k] = (st.src[k] == Src::Constant) ? static_cast<int8_t>(token) : int8_t{ -1 };
        }
    }
}


} // namespace pica
