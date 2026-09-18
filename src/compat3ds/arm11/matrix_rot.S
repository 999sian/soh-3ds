/* ARM11 rotation stages shared by Matrix_RotateZYX and Matrix_TranslateRotateZYX.
 *
 * Each stage rewrites two of the four contiguous MtxF columns, so it is one
 * operation across four lanes. In every case t1/t2 are the values read before
 * either column is written, matching the reference's temp1/temp2.
 *
 *   Z: colA = col0, colB = col1   colA' = t1*cos + t2*sin   colB' = t2*cos - t1*sin
 *   Y: colA = col0, colB = col2   colA' = t1*cos - t2*sin   colB' = t1*sin + t2*cos
 *   X: colA = col1, colB = col2   colA' = t1*cos + t2*sin   colB' = t2*cos - t1*sin
 *
 * r0 = MtxF* cmf, s0 = sin, s1 = cos. Products are kept as separate VMUL and
 * the combining VADD/VSUB follows the reference, so no VMLA is used. Only
 * caller-saved s0-s15 are touched, so there is no VFP save.
 *
 * The Z stage here is the rotation-only form used by Matrix_RotateZYX;
 * Matrix_TranslateRotateZYX's combined translate+Z stage lives in matrix_trz.S.
 */
.syntax unified
.arch armv6k
.fpu vfp
.arm

.section .text.Soh3dsMatrixRotateZStageArm11,"ax",%progbits
.align 2
.global Soh3dsMatrixRotateZStageArm11
.type Soh3dsMatrixRotateZStageArm11,%function
Soh3dsMatrixRotateZStageArm11:
    .fnstart
    add r1, r0, #0
    add r2, r0, #16
    vldmia r1, {s4-s7}          /* colA: t1 lanes */
    vldmia r2, {s8-s11}         /* colB: t2 lanes */

    /* lane 0 */
    vmul.f32 s12, s4, s1      /* t1*cos */
    vmul.f32 s13, s8, s0      /* t2*sin */
    vmul.f32 s14, s4, s0      /* t1*sin */
    vmul.f32 s15, s8, s1      /* t2*cos */
    vadd.f32 s4, s12, s13
    vsub.f32 s8, s15, s14

    /* lane 1 */
    vmul.f32 s12, s5, s1      /* t1*cos */
    vmul.f32 s13, s9, s0      /* t2*sin */
    vmul.f32 s14, s5, s0      /* t1*sin */
    vmul.f32 s15, s9, s1      /* t2*cos */
    vadd.f32 s5, s12, s13
    vsub.f32 s9, s15, s14

    /* lane 2 */
    vmul.f32 s12, s6, s1      /* t1*cos */
    vmul.f32 s13, s10, s0      /* t2*sin */
    vmul.f32 s14, s6, s0      /* t1*sin */
    vmul.f32 s15, s10, s1      /* t2*cos */
    vadd.f32 s6, s12, s13
    vsub.f32 s10, s15, s14

    /* lane 3 */
    vmul.f32 s12, s7, s1      /* t1*cos */
    vmul.f32 s13, s11, s0      /* t2*sin */
    vmul.f32 s14, s7, s0      /* t1*sin */
    vmul.f32 s15, s11, s1      /* t2*cos */
    vadd.f32 s7, s12, s13
    vsub.f32 s11, s15, s14

    vstmia r1, {s4-s7}
    vstmia r2, {s8-s11}
    bx lr
    .fnend
.size Soh3dsMatrixRotateZStageArm11, .-Soh3dsMatrixRotateZStageArm11

.section .text.Soh3dsMatrixRotateYStageArm11,"ax",%progbits
.align 2
.global Soh3dsMatrixRotateYStageArm11
.type Soh3dsMatrixRotateYStageArm11,%function
Soh3dsMatrixRotateYStageArm11:
    .fnstart
    add r1, r0, #0
    add r2, r0, #32
    vldmia r1, {s4-s7}          /* colA: t1 lanes */
    vldmia r2, {s8-s11}         /* colB: t2 lanes */

    /* lane 0 */
    vmul.f32 s12, s4, s1      /* t1*cos */
    vmul.f32 s13, s8, s0      /* t2*sin */
    vmul.f32 s14, s4, s0      /* t1*sin */
    vmul.f32 s15, s8, s1      /* t2*cos */
    vsub.f32 s4, s12, s13
    vadd.f32 s8, s14, s15

    /* lane 1 */
    vmul.f32 s12, s5, s1      /* t1*cos */
    vmul.f32 s13, s9, s0      /* t2*sin */
    vmul.f32 s14, s5, s0      /* t1*sin */
    vmul.f32 s15, s9, s1      /* t2*cos */
    vsub.f32 s5, s12, s13
    vadd.f32 s9, s14, s15

    /* lane 2 */
    vmul.f32 s12, s6, s1      /* t1*cos */
    vmul.f32 s13, s10, s0      /* t2*sin */
    vmul.f32 s14, s6, s0      /* t1*sin */
    vmul.f32 s15, s10, s1      /* t2*cos */
    vsub.f32 s6, s12, s13
    vadd.f32 s10, s14, s15

    /* lane 3 */
    vmul.f32 s12, s7, s1      /* t1*cos */
    vmul.f32 s13, s11, s0      /* t2*sin */
    vmul.f32 s14, s7, s0      /* t1*sin */
    vmul.f32 s15, s11, s1      /* t2*cos */
    vsub.f32 s7, s12, s13
    vadd.f32 s11, s14, s15

    vstmia r1, {s4-s7}
    vstmia r2, {s8-s11}
    bx lr
    .fnend
.size Soh3dsMatrixRotateYStageArm11, .-Soh3dsMatrixRotateYStageArm11

.section .text.Soh3dsMatrixRotateXStageArm11,"ax",%progbits
.align 2
.global Soh3dsMatrixRotateXStageArm11
.type Soh3dsMatrixRotateXStageArm11,%function
Soh3dsMatrixRotateXStageArm11:
    .fnstart
    add r1, r0, #16
    add r2, r0, #32
    vldmia r1, {s4-s7}          /* colA: t1 lanes */
    vldmia r2, {s8-s11}         /* colB: t2 lanes */

    /* lane 0 */
    vmul.f32 s12, s4, s1      /* t1*cos */
    vmul.f32 s13, s8, s0      /* t2*sin */
    vmul.f32 s14, s4, s0      /* t1*sin */
    vmul.f32 s15, s8, s1      /* t2*cos */
    vadd.f32 s4, s12, s13
    vsub.f32 s8, s15, s14

    /* lane 1 */
    vmul.f32 s12, s5, s1      /* t1*cos */
    vmul.f32 s13, s9, s0      /* t2*sin */
    vmul.f32 s14, s5, s0      /* t1*sin */
    vmul.f32 s15, s9, s1      /* t2*cos */
    vadd.f32 s5, s12, s13
    vsub.f32 s9, s15, s14

    /* lane 2 */
    vmul.f32 s12, s6, s1      /* t1*cos */
    vmul.f32 s13, s10, s0      /* t2*sin */
    vmul.f32 s14, s6, s0      /* t1*sin */
    vmul.f32 s15, s10, s1      /* t2*cos */
    vadd.f32 s6, s12, s13
    vsub.f32 s10, s15, s14

    /* lane 3 */
    vmul.f32 s12, s7, s1      /* t1*cos */
    vmul.f32 s13, s11, s0      /* t2*sin */
    vmul.f32 s14, s7, s0      /* t1*sin */
    vmul.f32 s15, s11, s1      /* t2*cos */
    vadd.f32 s7, s12, s13
    vsub.f32 s11, s15, s14

    vstmia r1, {s4-s7}
    vstmia r2, {s8-s11}
    bx lr
    .fnend
.size Soh3dsMatrixRotateXStageArm11, .-Soh3dsMatrixRotateXStageArm11
.section .note.GNU-stack,"",%progbits
