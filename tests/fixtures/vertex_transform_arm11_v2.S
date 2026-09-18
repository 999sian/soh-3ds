/* ARM11/VFPv2 object-to-clip transform for a batch of F3DVtx records.
 * r0: 16-byte input records, signed XYZ halfwords at offsets 0/2/4
 * r1: 32-byte LoadedVertex records, writes only the first four floats
 * r2: matrix[4][4]; r3: count. Input, output and matrix must be disjoint.
 * Source needs two-byte alignment; destination/matrix need four-byte alignment.
 * Keep the matrix resident for the entire batch. Preserve the compiler's
 * non-fused evaluation and NaN operand order: (y*b + x*a) + z*c + translation.
 * s16-s31 are callee-saved; the 64-byte frame preserves stack alignment.
 */
.syntax unified
.arch armv6k
.fpu vfp
.arm
.section .text.Soh3dsTransformVerticesArm11,"ax",%progbits
.align 2
.global Soh3dsTransformVerticesArm11
.type Soh3dsTransformVerticesArm11,%function
Soh3dsTransformVerticesArm11:
    .fnstart
    cmp r3, #0
    bxeq lr
    .vsave {d8-d15}
    vpush {d8-d15}
    vldmia r2, {s16-s31}
.Lvertex:
    ldrsh r12, [r0]
    vmov s0, r12
    ldrsh r12, [r0, #2]
    vmov s1, r12
    ldrsh r12, [r0, #4]
    vmov s2, r12
    vcvt.f32.s32 s0, s0
    vcvt.f32.s32 s1, s1
    vcvt.f32.s32 s2, s2
    vmul.f32 s4, s1, s20
    vmul.f32 s5, s1, s21
    vmul.f32 s6, s1, s22
    vmul.f32 s7, s1, s23
    vmla.f32 s4, s0, s16
    vmla.f32 s5, s0, s17
    vmla.f32 s6, s0, s18
    vmla.f32 s7, s0, s19
    vmla.f32 s4, s2, s24
    vmla.f32 s5, s2, s25
    vmla.f32 s6, s2, s26
    vmla.f32 s7, s2, s27
    vadd.f32 s4, s4, s28
    vadd.f32 s5, s5, s29
    vadd.f32 s6, s6, s30
    vadd.f32 s7, s7, s31
    vstmia r1, {s4-s7}
    add r0, r0, #16
    add r1, r1, #32
    subs r3, r3, #1
    bne .Lvertex
    vpop {d8-d15}
    bx lr
    .fnend
.size Soh3dsTransformVerticesArm11, .-Soh3dsTransformVerticesArm11
.section .note.GNU-stack,"",%progbits
