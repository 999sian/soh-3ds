/* ARM11 float-to-N64-fixed matrix conversion for guMtxF2L.
 *
 * r0 = const float mf[4][4], r1 = s32* m (Mtx, 16 words).
 *
 * Per pair of floats the reference computes:
 *   tmp1 = (s32)(mf[i]   * 65536.0f)
 *   tmp2 = (s32)(mf[i+1] * 65536.0f)
 *   m[k]     = (tmp1 & 0xffff0000) | ((tmp2 >> 16) & 0xffff)
 *   m[8 + k] = ((tmp1 << 16) & 0xffff0000) | (tmp2 & 0xffff)
 *
 * PKHTB Rd,Rn,Rm,ASR #16 takes Rn[31:16] and Rm[31:16]; PKHBT Rd,Rn,Rm,LSL #16
 * takes Rn[15:0] and Rm[15:0]. That is exactly the integer/fraction split, so
 * the six mask/shift/or operations per pair collapse to two instructions.
 *
 * The float multiply is kept as a separate VMUL rather than folded into a
 * fixed-point VCVT with 16 fractional bits: the reference rounds the product to
 * a float first, and a scaling VCVT would be exact, so the two can differ by
 * one LSB once the product needs more than 24 mantissa bits. VCVT.S32.F32
 * truncates toward zero and saturates, matching the C cast on this target.
 *
 * Fully unrolled: no loop counter register, one literal load, two burst stores.
 * mf and m are word-aligned (float[4][4] and s32[4][4]). Overlapping mf and m
 * is not supported, matching the reference's interleaved read/write order.
 * s0-s15 are caller-saved in AAPCS-VFP.
 */
.syntax unified
.arch armv6k
.fpu vfp
.arm
.section .text.Soh3dsMtxF2LArm11,"ax",%progbits
.align 2
.global Soh3dsMtxF2LArm11
.type Soh3dsMtxF2LArm11,%function
Soh3dsMtxF2LArm11:
    .fnstart
    .save {r4-r11,lr}
    push {r4-r11,lr}
    vldr s8, .Lf2l_scale
    add ip, r1, #32             /* fraction half: m[8..15] */

    /* ---- rows 0-1: mf[0..7] -> m[0..3] and m[8..11] ---- */
    vldmia r0!, {s0-s7}
    vmul.f32 s0, s0, s8
    vmul.f32 s1, s1, s8
    vmul.f32 s2, s2, s8
    vmul.f32 s3, s3, s8
    vmul.f32 s4, s4, s8
    vmul.f32 s5, s5, s8
    vmul.f32 s6, s6, s8
    vmul.f32 s7, s7, s8
    vcvt.s32.f32 s0, s0
    vcvt.s32.f32 s1, s1
    vcvt.s32.f32 s2, s2
    vcvt.s32.f32 s3, s3
    vcvt.s32.f32 s4, s4
    vcvt.s32.f32 s5, s5
    vcvt.s32.f32 s6, s6
    vcvt.s32.f32 s7, s7
    vmov r2, s0
    vmov r3, s1
    pkhtb r4, r2, r3, asr #16
    pkhbt r8, r3, r2, lsl #16
    vmov r2, s2
    vmov r3, s3
    pkhtb r5, r2, r3, asr #16
    pkhbt r9, r3, r2, lsl #16
    vmov r2, s4
    vmov r3, s5
    pkhtb r6, r2, r3, asr #16
    pkhbt r10, r3, r2, lsl #16
    vmov r2, s6
    vmov r3, s7
    pkhtb r7, r2, r3, asr #16
    pkhbt r11, r3, r2, lsl #16
    stmia r1!, {r4-r7}
    stmia ip!, {r8-r11}

    /* ---- rows 2-3: mf[8..15] -> m[4..7] and m[12..15] ---- */
    vldmia r0!, {s0-s7}
    vmul.f32 s0, s0, s8
    vmul.f32 s1, s1, s8
    vmul.f32 s2, s2, s8
    vmul.f32 s3, s3, s8
    vmul.f32 s4, s4, s8
    vmul.f32 s5, s5, s8
    vmul.f32 s6, s6, s8
    vmul.f32 s7, s7, s8
    vcvt.s32.f32 s0, s0
    vcvt.s32.f32 s1, s1
    vcvt.s32.f32 s2, s2
    vcvt.s32.f32 s3, s3
    vcvt.s32.f32 s4, s4
    vcvt.s32.f32 s5, s5
    vcvt.s32.f32 s6, s6
    vcvt.s32.f32 s7, s7
    vmov r2, s0
    vmov r3, s1
    pkhtb r4, r2, r3, asr #16
    pkhbt r8, r3, r2, lsl #16
    vmov r2, s2
    vmov r3, s3
    pkhtb r5, r2, r3, asr #16
    pkhbt r9, r3, r2, lsl #16
    vmov r2, s4
    vmov r3, s5
    pkhtb r6, r2, r3, asr #16
    pkhbt r10, r3, r2, lsl #16
    vmov r2, s6
    vmov r3, s7
    pkhtb r7, r2, r3, asr #16
    pkhbt r11, r3, r2, lsl #16
    stmia r1!, {r4-r7}
    stmia ip!, {r8-r11}

    pop {r4-r11,pc}
    .fnend
.align 2
.Lf2l_scale:
    .word 0x47800000            /* 65536.0f */
.size Soh3dsMtxF2LArm11, .-Soh3dsMtxF2LArm11
.section .note.GNU-stack,"",%progbits
