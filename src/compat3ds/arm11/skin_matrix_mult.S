/* Exact column-major 4x4 product used by SkinMatrix_MtxFMtxFMult.
 *
 * The complete left matrix is loaded before the first store, preserving the
 * game's supported dest == mfA behavior.  VFP multiply/accumulate order
 * matches GCC's non-fast-math ARM11 output: (a0*b0 + a1*b1) + a2*b2 + a3*b3,
 * with the second product as the initial accumulator.  d8-d15 are preserved
 * as required by AAPCS; no VFPv3, NEON, fused operation, or reassociation is
 * used.  The C contract excludes dest == mfB.
 */
.syntax unified
.arch armv6k
.fpu vfp
.arm
.section .text.Soh3dsSkinMatrixMultArm11,"ax",%progbits
.align 2
.global Soh3dsSkinMatrixMultArm11
.type Soh3dsSkinMatrixMultArm11,%function
Soh3dsSkinMatrixMultArm11:
    .fnstart
    vpush {d8-d15}
    .save {d8-d15}
    vldmia r0, {s16-s31}

    .macro MULT_COLUMN
    vldmia r1!, {s0-s3}
    vmul.f32 s4, s20, s1
    vmul.f32 s5, s21, s1
    vmul.f32 s6, s22, s1
    vmul.f32 s7, s23, s1
    vmla.f32 s4, s16, s0
    vmla.f32 s5, s17, s0
    vmla.f32 s6, s18, s0
    vmla.f32 s7, s19, s0
    vmla.f32 s4, s24, s2
    vmla.f32 s5, s25, s2
    vmla.f32 s6, s26, s2
    vmla.f32 s7, s27, s2
    vmla.f32 s4, s28, s3
    vmla.f32 s5, s29, s3
    vmla.f32 s6, s30, s3
    vmla.f32 s7, s31, s3
    vstmia r2!, {s4-s7}
    .endm

    MULT_COLUMN
    MULT_COLUMN
    MULT_COLUMN
    MULT_COLUMN
    vpop {d8-d15}
    bx lr
    .fnend
.size Soh3dsSkinMatrixMultArm11, .-Soh3dsSkinMatrixMultArm11
.section .note.GNU-stack,"",%progbits
