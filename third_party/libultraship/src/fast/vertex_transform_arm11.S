/* ARM11/VFPv2 object-to-clip transform for a batch of F3DVtx records.
 * r0: 16-byte input records, signed XYZ halfwords at offsets 0/2/4
 * r1: 32-byte LoadedVertex records, writes only the first four floats
 * r2: matrix[4][4]; r3: count. Input, output and matrix must be disjoint.
 * Source needs two-byte alignment; destination/matrix need four-byte alignment.
 * Keep the matrix resident for the entire batch. Preserve the compiler's
 * non-fused evaluation and NaN operand order: (y*b + x*a) + z*c + translation.
 * Matrix: s6-s21; x/z: s0/s1; outputs: s2-s5. The y input shares s5
 * with the final w result. Only d8-d10 need saving (24-byte aligned frame).
 * Separate integer-to-VFP transfers/conversions with useful arithmetic,
 * following the faster disjoint C schedule measured on physical Old 3DS.
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
    .vsave {d8-d10}
    vpush {d8-d10}
    vldmia r2, {s6-s21}
.Lvertex:
    ldrsh r12, [r0, #2]
    vmov s5, r12
    ldrsh r12, [r0]
    vcvt.f32.s32 s5, s5
    vmov s0, r12
    ldrsh r12, [r0, #4]
    vcvt.f32.s32 s0, s0
    vmul.f32 s2, s5, s10
    vmul.f32 s3, s5, s11
    vmul.f32 s4, s5, s12
    vmov s1, r12
    vmul.f32 s5, s5, s13
    vcvt.f32.s32 s1, s1
    vmla.f32 s2, s0, s6
    vmla.f32 s3, s0, s7
    vmla.f32 s4, s0, s8
    vmla.f32 s5, s0, s9
    vmla.f32 s2, s1, s14
    vmla.f32 s3, s1, s15
    vmla.f32 s4, s1, s16
    vmla.f32 s5, s1, s17
    vadd.f32 s2, s2, s18
    vadd.f32 s3, s3, s19
    vadd.f32 s4, s4, s20
    vadd.f32 s5, s5, s21
#if SOH3DS_VERTEX_SCALAR_STORES
    // Benchmark control: isolate VSTMIA's dependency/transfer cost on VFP11.
    vstr s2, [r1]
    vstr s3, [r1, #4]
    vstr s4, [r1, #8]
    vstr s5, [r1, #12]
#else
    vstmia r1, {s2-s5}
#endif
    add r0, r0, #16
    add r1, r1, #32
    subs r3, r3, #1
    bne .Lvertex
    vpop {d8-d10}
    bx lr
    .fnend
.size Soh3dsTransformVerticesArm11, .-Soh3dsTransformVerticesArm11
.section .note.GNU-stack,"",%progbits
