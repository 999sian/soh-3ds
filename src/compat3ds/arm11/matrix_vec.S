/* Column-major MtxF transform. Keep C's (a*x+b*y)+c*z, then translation
 * order using non-fused VFPv2 VMLA, including the C compiler's NaN operand
 * priority (y product in the accumulator). Fall back for overlapping writes.
 * AAPCS caller-saved s0-s14 only; no frame or callee-saved registers. */
.syntax unified
.arch armv6k
.fpu vfp
.arm
.section .text.Matrix_MultVec3fExt,"ax",%progbits
.align 2
.global Matrix_MultVec3fExt
.type Matrix_MultVec3fExt,%function
Matrix_MultVec3fExt:
    .fnstart
    sub r3, r1, r0
    add r3, r3, #11
    cmp r3, #23
    blo Soh3dsMatrixVecReference
    sub r3, r1, r2
    add r3, r3, #11
    cmp r3, #75
    blo Soh3dsMatrixVecReference
    vldmia r0, {s0-s2}
    // Load all three columns in one transfer. The unused w slots become
    // accumulators, allowing independent rows to hide VFP pipeline latency.
    vldmia r2, {s3-s14}
    vmul.f32 s6, s7, s1
    vmul.f32 s10, s8, s1
    vmul.f32 s14, s9, s1
    vmla.f32 s6, s3, s0
    vmla.f32 s10, s4, s0
    vmla.f32 s14, s5, s0
    vmla.f32 s6, s11, s2
    vmla.f32 s10, s12, s2
    vmla.f32 s14, s13, s2
    add r2, r2, #48
    vldmia r2, {s0-s2}
    vadd.f32 s0, s6, s0
    vadd.f32 s1, s10, s1
    vadd.f32 s2, s14, s2
    vstmia r1, {s0-s2}
    bx lr
    .fnend
.size Matrix_MultVec3fExt, .-Matrix_MultVec3fExt
.section .note.GNU-stack,"",%progbits
