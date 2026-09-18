/* ARM11 translate + Z-rotation stage of Matrix_TranslateRotateZYX.
 *
 * r0 = MtxF* cmf, s0 = translation->x, s1 = translation->y,
 * s2 = translation->z, s3 = sin, s4 = cos.
 *
 * MtxF keeps each column contiguous, so the reference's four repeated blocks
 * are one operation across four lanes. Per lane, with t1 = group0[i] and
 * t2 = group1[i]:
 *   group3[i] += ((t1 * tx) + (t2 * ty)) + (group2[i] * tz)
 *   group0[i]  = (t1 * cos) + (t2 * sin)
 *   group1[i]  = (t2 * cos) - (t1 * sin)
 *
 * Association matches the reference exactly: the three products are summed
 * left to right and only then accumulated, so no VMLA is used. Both new
 * column values are computed from the original t1/t2 before either is stored,
 * which is what the reference's temp1/temp2 do.
 *
 * Only this stage is in assembly. The Y and X rotations in the reference are
 * conditional on rotation->y / rotation->x being non-zero and stay in C.
 *
 * s16-s31 are callee-saved (d8-d15) and are restored.
 */
.syntax unified
.arch armv6k
.fpu vfp
.arm
.section .text.Soh3dsMatrixTranslateRotateZArm11,"ax",%progbits
.align 2
.global Soh3dsMatrixTranslateRotateZArm11
.type Soh3dsMatrixTranslateRotateZArm11,%function
Soh3dsMatrixTranslateRotateZArm11:
    .fnstart
    .vsave {d8-d15}
    vpush {d8-d15}

    vldmia r0, {s16-s19}        /* group0: xx yx zx wx */
    add r1, r0, #16
    vldmia r1, {s20-s23}        /* group1: xy yy zy wy */
    add r2, r0, #32
    vldmia r2, {s24-s27}        /* group2: xz yz zz wz */
    add r3, r0, #48
    vldmia r3, {s28-s31}        /* group3: xw yw zw ww */

    /* lane 0 */
    vmul.f32 s5, s16, s0
    vmul.f32 s6, s20, s1
    vmul.f32 s7, s24, s2
    vadd.f32 s5, s5, s6
    vadd.f32 s5, s5, s7
    vadd.f32 s28, s28, s5
    vmul.f32 s8, s16, s4
    vmul.f32 s9, s20, s3
    vmul.f32 s10, s20, s4
    vmul.f32 s11, s16, s3
    vadd.f32 s16, s8, s9
    vsub.f32 s20, s10, s11

    /* lane 1 */
    vmul.f32 s5, s17, s0
    vmul.f32 s6, s21, s1
    vmul.f32 s7, s25, s2
    vadd.f32 s5, s5, s6
    vadd.f32 s5, s5, s7
    vadd.f32 s29, s29, s5
    vmul.f32 s8, s17, s4
    vmul.f32 s9, s21, s3
    vmul.f32 s10, s21, s4
    vmul.f32 s11, s17, s3
    vadd.f32 s17, s8, s9
    vsub.f32 s21, s10, s11

    /* lane 2 */
    vmul.f32 s5, s18, s0
    vmul.f32 s6, s22, s1
    vmul.f32 s7, s26, s2
    vadd.f32 s5, s5, s6
    vadd.f32 s5, s5, s7
    vadd.f32 s30, s30, s5
    vmul.f32 s8, s18, s4
    vmul.f32 s9, s22, s3
    vmul.f32 s10, s22, s4
    vmul.f32 s11, s18, s3
    vadd.f32 s18, s8, s9
    vsub.f32 s22, s10, s11

    /* lane 3 */
    vmul.f32 s5, s19, s0
    vmul.f32 s6, s23, s1
    vmul.f32 s7, s27, s2
    vadd.f32 s5, s5, s6
    vadd.f32 s5, s5, s7
    vadd.f32 s31, s31, s5
    vmul.f32 s8, s19, s4
    vmul.f32 s9, s23, s3
    vmul.f32 s10, s23, s4
    vmul.f32 s11, s19, s3
    vadd.f32 s19, s8, s9
    vsub.f32 s23, s10, s11

    vstmia r0, {s16-s19}
    vstmia r1, {s20-s23}
    vstmia r3, {s28-s31}
    vpop {d8-d15}
    bx lr
    .fnend
.size Soh3dsMatrixTranslateRotateZArm11, .-Soh3dsMatrixTranslateRotateZArm11
.section .note.GNU-stack,"",%progbits
