// Exhaustive verification of the N64 -> PICA200 combiner lowering.
//
// Runs on the host. No 3DS toolchain, no hardware. Proves three things:
//
//   1. For the three special shapes (single / multiply / mix), the lowered TexEnv
//      configuration computes *exactly* the same value as libultraship's reference
//      formula, for every operand assignment tested.
//   2. For the general shape, the ONLY divergence is the documented clamp case
//      (A < B && D > 0 && C > 0). Anywhere else it is exact.
//   3. The shape classification matches the reference's precedence, so the 3DS
//      backend takes the same branch every other backend takes.
//
// It also reports how often each shape occurs across the whole combiner space,
// which is the desk-side half of gate A6 (the other half is instrumenting a real
// session to learn which combiners OoT actually emits).
//
// ponytail: assert-based, single translation unit, no test framework.

#include "../src/pica/combiner_lower.hpp"
#include "../src/pica/pica_texenv.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace pica;

namespace {

// The reference formula, transcribed from libultraship's append_formula
// (src/fast/backends/gfx_opengl.cpp). Float maths, no intermediate clamping —
// this is what every other backend computes.
float reference(const int c[4], const float v[16]) {
    const float a = v[c[0]], b = v[c[1]], cc = v[c[2]], d = v[c[3]];
    if (c[2] == SHADER_0) {
        return d; // do_single
    }
    if (c[1] == SHADER_0 && c[3] == SHADER_0) {
        return a * cc; // do_multiply
    }
    if (c[1] == c[3]) {
        return b + (a - b) * cc; // do_mix
    }
    return (a - b) * cc + d; // general
}

// Run a lowered plan through the PICA evaluator. Stage sources are the
// placeholders from combiner_lower.cpp, so bind them to the operand values here.
float simulate(const Plan& p, const int c[4], const float v[16]) {
    Inputs in;
    in.tex0 = v[c[0]];     // A
    in.tex1 = v[c[1]];     // B
    in.tex2 = v[c[2]];     // C
    in.constant = v[c[3]]; // D
    in.previous = 0.0f;

    float out = 0.0f;
    for (int i = 0; i < p.count; ++i) {
        out = eval(p.stage[i], in);
        in.previous = out; // chain into the next stage
    }
    return out;
}

bool near(float x, float y) {
    return std::fabs(x - y) < 1e-5f;
}

const char* shape_name(Shape s) {
    switch (s) {
        case Shape::Single:
            return "single  ";
        case Shape::Multiply:
            return "multiply";
        case Shape::Mix:
            return "mix     ";
        case Shape::General:
            return "general ";
    }
    return "?";
}

} // namespace

int main() {
    // Operand tokens the lowering can see. SHADER_0 and SHADER_1 are the constants
    // the RDP special-cases; the rest are shading inputs and texels.
    const int tokens[] = { SHADER_0,      SHADER_1,      SHADER_INPUT_1, SHADER_INPUT_2,
                           SHADER_TEXEL0, SHADER_TEXEL1, SHADER_COMBINED };
    const int ntok = sizeof(tokens) / sizeof(tokens[0]);

    // Value assignments per token. SHADER_0 must be 0 and SHADER_1 must be 1 —
    // those are definitional. The others sweep a representative range including
    // the boundaries, which is where clamping bites.
    const float sweeps[][16] = {
        //  0    1(unused indices are padding)
        { 0.0f, 0.25f, 0.50f, 0.0f, 0.75f, 1.00f, 0.0f, 0.0f, 0.10f, 0.0f, 0.90f, 0.0f, 1.0f, 0.60f, 0.0f, 0.0f },
        { 0.0f, 1.00f, 0.00f, 0.0f, 1.00f, 0.00f, 0.0f, 0.0f, 1.00f, 0.0f, 0.00f, 0.0f, 1.0f, 0.50f, 0.0f, 0.0f },
        { 0.0f, 0.80f, 0.90f, 0.0f, 0.20f, 0.30f, 0.0f, 0.0f, 0.70f, 0.0f, 0.40f, 0.0f, 1.0f, 0.05f, 0.0f, 0.0f },
        { 0.0f, 0.50f, 1.00f, 0.0f, 0.00f, 0.50f, 0.0f, 0.0f, 0.25f, 0.0f, 1.00f, 0.0f, 1.0f, 0.95f, 0.0f, 0.0f },
    };
    const int nsweep = sizeof(sweeps) / sizeof(sweeps[0]);

    long total = 0, exact = 0, diverged = 0, unexpected = 0;
    long shape_count[4] = { 0, 0, 0, 0 };

    for (int ia = 0; ia < ntok; ++ia) {
        for (int ib = 0; ib < ntok; ++ib) {
            for (int ic = 0; ic < ntok; ++ic) {
                for (int id = 0; id < ntok; ++id) {
                    const int c[4] = { tokens[ia], tokens[ib], tokens[ic], tokens[id] };
                    const Plan p = lower_channel(c);

                    // Stage budget: never more than 2 per channel.
                    assert(p.count >= 1 && p.count <= 2);
                    // Only the general shape is allowed to need two stages.
                    assert(p.count == 1 || p.shape == Shape::General);

                    shape_count[static_cast<int>(p.shape)]++;

                    for (int s = 0; s < nsweep; ++s) {
                        const float* v = sweeps[s];
                        const float want = reference(c, v);
                        const float got = simulate(p, c, v);
                        total++;

                        if (near(clamp01(want), clamp01(got))) {
                            exact++;
                            continue;
                        }

                        // A mismatch is only permitted for the general shape, and
                        // only under the documented clamp condition.
                        const bool allowed =
                            p.shape == Shape::General &&
                            general_form_diverges(v[c[0]], v[c[1]], v[c[2]], v[c[3]]);
                        if (allowed) {
                            diverged++;
                            // The deviation is always *brighter*, never darker,
                            // and never a wrap or sign flip. Assert that.
                            assert(clamp01(got) >= clamp01(want) - 1e-5f);
                        } else {
                            unexpected++;
                            std::printf("UNEXPECTED  shape=%s c={%d,%d,%d,%d} sweep=%d "
                                        "want=%.4f got=%.4f\n",
                                        shape_name(p.shape), c[0], c[1], c[2], c[3], s, want, got);
                        }
                    }
                }
            }
        }
    }

    // --- Targeted checks on the cases the design leans on ---------------------

    // The D == B lerp idiom must be exact in one stage. This is the finding that
    // makes the 6-stage budget comfortable rather than tight.
    {
        const int c[4] = { SHADER_TEXEL0, SHADER_INPUT_1, SHADER_INPUT_2, SHADER_INPUT_1 };
        const Plan p = lower_channel(c);
        assert(p.shape == Shape::Mix);
        assert(p.count == 1);
        for (int s = 0; s < nsweep; ++s) {
            assert(near(clamp01(reference(c, sweeps[s])), clamp01(simulate(p, c, sweeps[s]))));
        }
    }

    // Precedence: C == 0 wins over B == D.
    {
        const int c[4] = { SHADER_TEXEL0, SHADER_INPUT_1, SHADER_0, SHADER_INPUT_1 };
        assert(lower_channel(c).shape == Shape::Single);
    }
    // Precedence: B == 0 && D == 0 wins over B == D (both are true when B=D=0).
    {
        const int c[4] = { SHADER_TEXEL0, SHADER_0, SHADER_INPUT_1, SHADER_0 };
        assert(lower_channel(c).shape == Shape::Multiply);
    }

    // Cycle-2 texel swap (DESIGN §3.5). Naive backends get this wrong.
    assert(source_to_tev(SHADER_TEXEL0, 0, -1) == Src::Texture0);
    assert(source_to_tev(SHADER_TEXEL0, 1, -1) == Src::Texture1);
    assert(source_to_tev(SHADER_TEXEL1, 0, -1) == Src::Texture1);
    assert(source_to_tev(SHADER_TEXEL1, 1, -1) == Src::Texture0);
    assert(source_to_tev(SHADER_COMBINED, 0, -1) == Src::Previous);
    // The one varying input rides vertex colour; everything else is a constant.
    assert(source_to_tev(SHADER_INPUT_1, 0, SHADER_INPUT_1) == Src::PrimaryColor);
    assert(source_to_tev(SHADER_INPUT_2, 0, SHADER_INPUT_1) == Src::Constant);

    // Stage-0 hazard: the lowering must never emit GPU_PREVIOUS in stage 0,
    // because stage 0 has no previous stage and the hardware substitutes another
    // source silently (DESIGN §3.3 rule 1, characterised by T5).
    for (int ia = 0; ia < ntok; ++ia)
        for (int ib = 0; ib < ntok; ++ib)
            for (int ic = 0; ic < ntok; ++ic)
                for (int id = 0; id < ntok; ++id) {
                    const int c[4] = { tokens[ia], tokens[ib], tokens[ic], tokens[id] };
                    const Plan p = lower_channel(c);
                    for (int k = 0; k < 3; ++k) {
                        assert(p.stage[0].src[k] != Src::Previous);
                    }
                }

    // --- Two-cycle stage budget ------------------------------------------------
    //
    // The load-bearing claim in DESIGN §3.4 is not "most channels are cheap" but
    // "the worst case still fits in six stages". A cycle costs max(rgb, alpha)
    // because the two channels share a stage, so two cycles must fit in 6 with
    // room left for the combiner-buffer latch.
    {
        int worst = 0;
        long over_four = 0;
        for (int ia = 0; ia < ntok; ++ia)
            for (int ib = 0; ib < ntok; ++ib)
                for (int ic = 0; ic < ntok; ++ic)
                    for (int id = 0; id < ntok; ++id) {
                        // Worst case is the same shape on both channels of both
                        // cycles; mixing shapes can only be cheaper, since a
                        // cycle's cost is a max over its channels, not a sum.
                        const int c[4] = { tokens[ia], tokens[ib], tokens[ic], tokens[id] };
                        const int per_cycle = lower_channel(c).count;
                        const int total_stages = per_cycle * 2;
                        if (total_stages > worst) {
                            worst = total_stages;
                        }
                        if (total_stages > 4) {
                            over_four++;
                        }
                    }
        std::printf("\ntwo-cycle stage budget: worst case %d of 6 stages (%ld combiners exceed 4)\n",
                    worst, over_four);
        assert(worst <= 4); // leaves >= 2 stages free for the buffer latch
        assert(over_four == 0);
    }

    // --- Report ---------------------------------------------------------------

    const long combos = shape_count[0] + shape_count[1] + shape_count[2] + shape_count[3];
    std::printf("\ncombiner lowering: %ld combinations x %d value sweeps = %ld evaluations\n",
                combos, nsweep, total);
    std::printf("  exact match vs reference : %ld (%.1f%%)\n", exact, 100.0 * exact / total);
    std::printf("  documented clamp deviation: %ld (%.1f%%)\n", diverged, 100.0 * diverged / total);
    std::printf("  UNEXPECTED divergence     : %ld\n", unexpected);

    std::printf("\nshape distribution over the combiner space (stage cost per channel):\n");
    const Shape shapes[] = { Shape::Single, Shape::Multiply, Shape::Mix, Shape::General };
    for (Shape s : shapes) {
        const long n = shape_count[static_cast<int>(s)];
        std::printf("  %s %6ld  (%5.1f%%)  %d stage%s\n", shape_name(s), n, 100.0 * n / combos,
                    s == Shape::General ? 2 : 1, s == Shape::General ? "s" : "");
    }
    const long one_stage = combos - shape_count[static_cast<int>(Shape::General)];
    std::printf("\n  1-stage channels: %.1f%%  -> worst-case 2-cycle cost fits in 6 stages\n",
                100.0 * one_stage / combos);

    if (unexpected != 0) {
        std::printf("\nFAIL: %ld unexpected divergences\n", unexpected);
        return 1;
    }
    std::printf("\nPASS\n");
    return 0;
}
