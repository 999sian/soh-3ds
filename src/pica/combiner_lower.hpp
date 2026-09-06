// N64 colour-combiner -> PICA200 TexEnv lowering.
//
// Fast3D hands the backend a 128-bit shader id; gfx_cc_get_features() decodes it
// into CCFeatures, whose c[cycle][channel][term] holds the four RDP terms A,B,C,D
// of the combiner form (A - B) * C + D.
//
// libultraship's reference OpenGL backend (src/fast/backends/gfx_opengl.cpp,
// append_formula) emits exactly four cases, in this precedence order:
//
//   do_single   (C == 0)            ->  D
//   do_multiply (B == 0 && D == 0)  ->  A * C
//   do_mix      (B == D)            ->  mix(B, A, C)  ==  B + (A - B) * C
//   otherwise                       ->  (A - B) * C + D
//
// This lowering mirrors that precedence exactly, so the 3DS backend computes the
// same value as every other backend wherever the hardware allows.
//
// Provenance: derived from libultraship (MIT) and the PICA200 function semantics
// in DESIGN-PICA-BACKEND.md §3.3. No third-party 3DS port was consulted.

#pragma once

#include "pica_texenv.hpp"
#include <cstddef>

namespace pica {

// Fast3D shader-input tokens (libultraship include/fast/interpreter.h).
enum {
    SHADER_0 = 0,
    SHADER_INPUT_1,
    SHADER_INPUT_2,
    SHADER_INPUT_3,
    SHADER_INPUT_4,
    SHADER_INPUT_5,
    SHADER_INPUT_6,
    SHADER_INPUT_7,
    SHADER_TEXEL0,
    SHADER_TEXEL0A,
    SHADER_TEXEL1,
    SHADER_TEXEL1A,
    SHADER_1,
    SHADER_COMBINED,
    SHADER_NOISE,
};

// Which of the four reference cases a channel falls into. Exposed so callers can
// report coverage: the proportion of real combiners hitting the 1-stage cases is
// what gate A6 measures.
enum class Shape { Single, Multiply, Mix, General };

struct Plan {
    Stage stage[2];
    int count = 0;
    Shape shape = Shape::General;
};

// Map a Fast3D combiner token to a PICA TexEnv source.
//
// `cycle` matters: the RDP swaps the texel inputs between cycles in two-cycle
// mode, so TEXEL0 in cycle 1 samples the tile that TEXEL1 named in cycle 0.
// Getting this backwards yields subtly wrong two-cycle materials.
//
// Exactly one shading input can ride the vertex-colour attribute; `varying`
// names it. Everything else folds into the per-stage constant, of which the
// hardware has six (one per stage).
inline Src source_to_tev(int token, int cycle, int varying) {
    switch (token) {
        case SHADER_TEXEL0:
        case SHADER_TEXEL0A:
            return cycle == 0 ? Src::Texture0 : Src::Texture1;
        case SHADER_TEXEL1:
        case SHADER_TEXEL1A:
            return cycle == 0 ? Src::Texture1 : Src::Texture0;
        case SHADER_COMBINED:
            return Src::Previous;
        default:
            return token == varying ? Src::PrimaryColor : Src::Constant;
    }
}

// Lower one channel (colour or alpha) of one cycle.
//
// Returns 1 stage for the three special shapes, 2 for the general form. The
// general form is the only lossy case: PICA clamps GPU_SUBTRACT to >= 0, so where
// A < B and D > 0 the result is brighter than the N64's. See DESIGN §3.7.
Plan lower_channel(const int c[4]);

// Bind a freshly lowered Plan's operand sentinels to real TexEnv sources.
//
// lower_channel() picks the *shape* using placeholders that stand for the RDP
// terms A,B,C,D; this resolves them to the sources those terms actually name.
// The split exists so the host tests can drive the shape logic with plain
// operand values, but it means a backend MUST call this before touching
// hardware - a Plan straight out of lower_channel() names textures that have
// nothing to do with the combiner.
//
// PICA has one constant register per stage and the operand modifier yields two
// values from it: with the register at zero, SrcColor reads 0 and
// OneMinusSrcColor reads 1, so SHADER_0 and SHADER_1 cost nothing.
//
// `varying` names the one shading input carried by the vertex-colour attribute.
// Which input that is cannot be known until draw time: Fast3D assigns SHADE to
// whichever slot is free, so it is INPUT_1 only when PRIM/ENV did not take it.
void bind_sources(Plan& p, const int c[4], int cycle, int varying);

// What one hardware stage needs loaded into its constant register.
//
// A TexEnv stage has a single constant register shared by its colour and alpha
// halves, so a caller must accumulate across *both* lowered plans before
// deciding what to load - an alpha operand reading a shading input constrains
// the register just as much as a colour operand does.
struct ConstantNeed {
    int token = -1;        // shading input whose colour the register must hold
    bool zero = false;     // some operand reads 0 (SHADER_0) or 1 (SHADER_1) off it
    bool conflict = false; // needs two different values at once: unsatisfiable

    void add(const Stage& s) {
        for (int k = 0; k < 3; ++k) {
            const int t = s.constToken[k];
            if (t < 0) {
                continue;
            }
            if (t == SHADER_0 || t == SHADER_1) {
                zero = true;
            } else if (token >= 0 && token != t) {
                conflict = true;
            } else {
                token = t;
            }
        }
        if (zero && token >= 0) {
            conflict = true;
        }
    }
};

// True where the 2-stage general lowering diverges from the N64 reference for the
// given operand values. Used by the test suite to prove the deviation is confined
// to exactly the documented condition.
inline bool general_form_diverges(float a, float b, float c_, float d) {
    return a < b && d > 0.0f && c_ > 0.0f;
}

} // namespace pica
