/* ARM11 translate-apply for Matrix_Translate(MTXMODE_APPLY).
 *
 * r0 = MtxF* cmf, s0 = x, s1 = y, s2 = z  (AAPCS-VFP passes the floats in s0-s2).
 *
 * MtxF stores each column contiguously:
 *   0..12  xx yx zx wx   (the four values scaled by x)
 *   16..28 xy yy zy wy   (scaled by y)
 *   32..44 xz yz zz wz   (scaled by z)
 *   48..60 xw yw zw ww   (accumulated into)
 *
 * so the reference
 *   cmf->xw += tx * x + ty * y + cmf->xz * z;   (tx = xx, ty = xy)
 * is one elementwise operation across four lanes:
 *   group3[i] += ((group0[i] * x) + (group1[i] * y)) + (group2[i] * z)
 *
 * The multiplies and adds are kept as separate VMUL/VADD in exactly the
 * reference's association: ((t0 + t1) + t2) then accumulate. A VMLA chain would
 * compute ((xw + t0) + t1) + t2 instead, which associates differently and is not
 * bit-exact, so it is deliberately not used here.
 *
 * s16-s19 are callee-saved in AAPCS-VFP and are restored; s0-s15 are
 * caller-saved. No branches, no stack frame beyond the VFP save.
 */
.syntax unified
.arch armv6k
.fpu vfp
.arm
.section .text.Soh3dsMatrixTranslateApplyArm11,"ax",%progbits
.align 2
.global Soh3dsMatrixTranslateApplyArm11
.type Soh3dsMatrixTranslateApplyArm11,%function
Soh3dsMatrixTranslateApplyArm11:
    .fnstart
    /* s16-s19 is d8-d9; ARM unwind tables only describe VFP saves in double
     * registers, so the save/restore is expressed that way. */
    .vsave {d8-d9}
    vpush {d8-d9}

    vldmia r0, {s4-s7}          /* group0: xx yx zx wx */
    add r1, r0, #16
    vldmia r1, {s8-s11}         /* group1: xy yy zy wy */
    add r1, r0, #32
    vldmia r1, {s12-s15}        /* group2: xz yz zz wz */
    add r1, r0, #48
    vldmia r1, {s16-s19}        /* group3: xw yw zw ww */

    vmul.f32 s4, s4, s0
    vmul.f32 s5, s5, s0
    vmul.f32 s6, s6, s0
    vmul.f32 s7, s7, s0

    vmul.f32 s8, s8, s1
    vmul.f32 s9, s9, s1
    vmul.f32 s10, s10, s1
    vmul.f32 s11, s11, s1

    vmul.f32 s12, s12, s2
    vmul.f32 s13, s13, s2
    vmul.f32 s14, s14, s2
    vmul.f32 s15, s15, s2

    /* (t0 + t1) */
    vadd.f32 s4, s4, s8
    vadd.f32 s5, s5, s9
    vadd.f32 s6, s6, s10
    vadd.f32 s7, s7, s11

    /* + t2 */
    vadd.f32 s4, s4, s12
    vadd.f32 s5, s5, s13
    vadd.f32 s6, s6, s14
    vadd.f32 s7, s7, s15

    /* accumulate into the w column */
    vadd.f32 s16, s16, s4
    vadd.f32 s17, s17, s5
    vadd.f32 s18, s18, s6
    vadd.f32 s19, s19, s7

    vstmia r1, {s16-s19}
    vpop {d8-d9}
    bx lr
    .fnend
.size Soh3dsMatrixTranslateApplyArm11, .-Soh3dsMatrixTranslateApplyArm11

/* ARM11 scale-apply for Matrix_Scale(MTXMODE_APPLY).
 *
 * r0 = MtxF* cmf, s0 = x, s1 = y, s2 = z.
 *
 * The reference multiplies the x column by x, the y column by y and the z
 * column by z (xx,yx,zx,wx by x, and so on). All twelve multiplies are
 * independent, so lane order cannot change the result and this is bit-exact by
 * construction. The w column is untouched.
 *
 * Only caller-saved s0-s15 are used, so there is no VFP save.
 */
.section .text.Soh3dsMatrixScaleApplyArm11,"ax",%progbits
.align 2
.global Soh3dsMatrixScaleApplyArm11
.type Soh3dsMatrixScaleApplyArm11,%function
Soh3dsMatrixScaleApplyArm11:
    .fnstart
    vldmia r0, {s4-s7}          /* xx yx zx wx */
    add r1, r0, #16
    vldmia r1, {s8-s11}         /* xy yy zy wy */
    add r2, r0, #32
    vldmia r2, {s12-s15}        /* xz yz zz wz */

    vmul.f32 s4, s4, s0
    vmul.f32 s5, s5, s0
    vmul.f32 s6, s6, s0
    vmul.f32 s7, s7, s0

    vmul.f32 s8, s8, s1
    vmul.f32 s9, s9, s1
    vmul.f32 s10, s10, s1
    vmul.f32 s11, s11, s1

    vmul.f32 s12, s12, s2
    vmul.f32 s13, s13, s2
    vmul.f32 s14, s14, s2
    vmul.f32 s15, s15, s2

    vstmia r0, {s4-s7}
    vstmia r1, {s8-s11}
    vstmia r2, {s12-s15}
    bx lr
    .fnend
.size Soh3dsMatrixScaleApplyArm11, .-Soh3dsMatrixScaleApplyArm11
.section .note.GNU-stack,"",%progbits
