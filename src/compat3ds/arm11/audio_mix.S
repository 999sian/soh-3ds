/* ARMv6K integer DSP kernels. Halfword-aligned PCM, scalar ordering and
 * 32-bit accumulator wrap match mixer.c. No NEON or VFP state required. */
.syntax unified
.arch armv6k
.arm

.section .text.Soh3dsAdpcmHalfArm11,"ax",%progbits
.align 2
.global Soh3dsAdpcmHalfArm11
.type Soh3dsAdpcmHalfArm11,%function
Soh3dsAdpcmHalfArm11:
    .fnstart
    .save {r4-r7}
    push {r4-r7}
    ldrsh r3, [r0, #-4]
    ldrsh r4, [r0, #-2]
.macro adpcm_sample j
    ldrsh r5, [r1, #(2*\j)]
    mov r7, r5, lsl #11
    ldrsh r6, [r2, #(2*\j)]
    smlabb r7, r6, r3, r7
    ldrsh r6, [r2, #(16+2*\j)]
    smlabb r7, r6, r4, r7
    .set tap, 0
    .rept \j
        ldrsh r5, [r1, #(2*tap)]
        ldrsh r6, [r2, #(16+2*(\j-tap-1))]
        smlabb r7, r6, r5, r7
        .set tap, tap+1
    .endr
    ssat r7, #16, r7, asr #11
    strh r7, [r0, #(2*\j)]
.endm
    .irp j,0,1,2,3,4,5,6,7
        adpcm_sample \j
    .endr
    pop {r4-r7}
    bx lr
    .fnend
.size Soh3dsAdpcmHalfArm11, .-Soh3dsAdpcmHalfArm11

.section .text.Soh3dsMixArm11,"ax",%progbits
.align 2
.global Soh3dsMixArm11
.type Soh3dsMixArm11,%function
Soh3dsMixArm11:
    .fnstart
    cmp r3, #0
    bxeq lr
    .save {r4-r6,lr}
    push {r4-r6,lr}
    mov r6, #0x7f00
    orr r6, r6, #0xff
    cmn r2, #32768
    beq .Lsubtract
.Lmix:
    ldrsh r4, [r0], #2
    ldrsh r5, [r1]
    mul r4, r2, r4
    smlabb r4, r5, r6, r4
    add r4, r4, #0x4000
    ssat r4, #16, r4, asr #15
    strh r4, [r1], #2
    subs r3, r3, #1
    bne .Lmix
    pop {r4-r6,pc}
.Lsubtract:
    ldrsh r4, [r0], #2
    ldrsh r5, [r1]
    sub r4, r5, r4
    ssat r4, #16, r4
    strh r4, [r1], #2
    subs r3, r3, #1
    bne .Lsubtract
    pop {r4-r6,pc}
    .fnend
.size Soh3dsMixArm11, .-Soh3dsMixArm11

/* input, dry[2], wet[2], count; stack: vols[3], rates[3], negs[4], swap.
 * count is a positive multiple of 8. Volume ramps wrap every eight samples.
 * Keep dry0/wet0/dry1/wet1 store order for overlapping DMEM buffers. */
.section .text.Soh3dsEnvMixerArm11,"ax",%progbits
.align 2
.global Soh3dsEnvMixerArm11
.type Soh3dsEnvMixerArm11,%function
Soh3dsEnvMixerArm11:
    .fnstart
    .save {r4-r11,lr}
    push {r4-r11,lr}
    .pad #28
    sub sp, sp, #28
    str r3, [sp, #16]
    ldr r12, [sp, #64]
    ldrh r6, [r12]
    ldrh r7, [r12, #2]
    ldrh r8, [r12, #4]
    ldr r12, [sp, #68]
    str r12, [sp, #4]
    ldr r12, [sp, #72]
    str r12, [sp]
    ldr r12, [sp, #76]
    str r12, [sp, #8]
    ldr r4, [r2]
    ldr r5, [r2, #4]
    ldr r2, [r1]
    ldr r3, [r1, #4]
.Lenv_block:
    mov r12, #8
    str r12, [sp, #12]
.Lenv_sample:
    ldrsh r12, [r0], #2
    mul r9, r6, r12
    mul r10, r7, r12
    ldr lr, [sp]
    ldr r11, [lr]
    eor r9, r11, r9, asr #16
    ldr r11, [lr, #4]
    eor r10, r11, r10, asr #16
    ldrsh r11, [r2]
    add r11, r11, r9
    ssat r11, #16, r11
    strh r11, [r2], #2
    ldr r12, [sp, #8]
    cmp r12, #0
    moveq r11, r9
    movne r11, r10
    mul r11, r8, r11
    ldr r12, [lr, #8]
    eor r11, r12, r11, asr #16
    ldrsh r12, [r4]
    add r11, r11, r12
    ssat r11, #16, r11
    strh r11, [r4], #2
    ldrsh r11, [r3]
    add r11, r11, r10
    ssat r11, #16, r11
    strh r11, [r3], #2
    moveq r11, r10
    movne r11, r9
    mul r11, r8, r11
    ldr r12, [lr, #12]
    eor r11, r12, r11, asr #16
    ldrsh r12, [r5]
    add r11, r11, r12
    ssat r11, #16, r11
    strh r11, [r5], #2
    ldr r12, [sp, #12]
    subs r12, r12, #1
    str r12, [sp, #12]
    bne .Lenv_sample
    ldr r12, [sp, #4]
    ldrh lr, [r12]
    add r6, r6, lr
    uxth r6, r6
    ldrh lr, [r12, #2]
    add r7, r7, lr
    uxth r7, r7
    ldrh lr, [r12, #4]
    add r8, r8, lr
    uxth r8, r8
    ldr r12, [sp, #16]
    subs r12, r12, #8
    str r12, [sp, #16]
    bgt .Lenv_block
    add sp, sp, #28
    pop {r4-r11,pc}
    .fnend
.size Soh3dsEnvMixerArm11, .-Soh3dsEnvMixerArm11

/* Saturating 16-bit vector sum for aAddMixer.
 * in and out are at least halfword-aligned; count is number of samples.
 * Uses QADD16 (dual 16-bit signed saturating addition).
 */
.section .text.Soh3dsAddMixerArm11,"ax",%progbits
.align 2
.global Soh3dsAddMixerArm11
.type Soh3dsAddMixerArm11,%function
Soh3dsAddMixerArm11:
    .fnstart
    cmp r2, #0
    bxeq lr
    .save {r4-r11,lr}
    push {r4-r11,lr}

    /* Check if both in and out are word-aligned (bits 0,1 == 0) */
    orr r3, r0, r1
    tst r3, #3
    bne .Ladd_unaligned

.Ladd_loop16:
    cmp r2, #16
    blo .Ladd_tail_words
    ldmia r0!, {r3-r6}
    ldmia r1, {r7-r10}
    qadd16 r7, r7, r3
    qadd16 r8, r8, r4
    qadd16 r9, r9, r5
    qadd16 r10, r10, r6
    stmia r1!, {r7-r10}
    ldmia r0!, {r3-r6}
    ldmia r1, {r7-r10}
    qadd16 r7, r7, r3
    qadd16 r8, r8, r4
    qadd16 r9, r9, r5
    qadd16 r10, r10, r6
    stmia r1!, {r7-r10}
    sub r2, r2, #16
    b .Ladd_loop16

.Ladd_tail_words:
    cmp r2, #2
    blo .Ladd_tail_sample
    ldr r3, [r0], #4
    ldr r4, [r1]
    qadd16 r4, r4, r3
    str r4, [r1], #4
    sub r2, r2, #2
    b .Ladd_tail_words

.Ladd_tail_sample:
    cmp r2, #1
    blo .Ladd_done
    ldrsh r3, [r0], #2
    ldrsh r4, [r1]
    add r4, r4, r3
    ssat r4, #16, r4
    strh r4, [r1], #2

.Ladd_done:
    pop {r4-r11,pc}

.Ladd_unaligned:
    cmp r2, #2
    blo .Ladd_tail_sample
    ldr r3, [r0], #4
    ldr r4, [r1]
    qadd16 r4, r4, r3
    str r4, [r1], #4
    sub r2, r2, #2
    b .Ladd_unaligned
    .fnend
.size Soh3dsAddMixerArm11, .-Soh3dsAddMixerArm11

/* Stereo interleave for aInterleave.
 * r0 = left, r1 = right, r2 = dest, r3 = groups of 4 stereo frames.
 * Packs L/R halfwords with PKHBT/PKHTB: 4 frames per two loads and one store.
 * Buffers are at least halfword-aligned; the word path needs all three aligned.
 */
.section .text.Soh3dsInterleaveArm11,"ax",%progbits
.align 2
.global Soh3dsInterleaveArm11
.type Soh3dsInterleaveArm11,%function
Soh3dsInterleaveArm11:
    .fnstart
    cmp r3, #0
    bxeq lr
    .save {r4-r11,lr}
    push {r4-r11,lr}

    orr ip, r0, r1
    orr ip, ip, r2
    tst ip, #3
    bne .Lil_scalar

.Lil_fast:
    ldmia r0!, {r4, r5}         /* r4=[L1,L0] r5=[L3,L2] */
    ldmia r1!, {r6, r7}         /* r6=[R1,R0] r7=[R3,R2] */
    pkhbt r8, r4, r6, lsl #16   /* [R0,L0] */
    pkhtb r9, r6, r4, asr #16   /* [R1,L1] */
    pkhbt r10, r5, r7, lsl #16  /* [R2,L2] */
    pkhtb r11, r7, r5, asr #16  /* [R3,L3] */
    stmia r2!, {r8-r11}
    subs r3, r3, #1
    bne .Lil_fast
    pop {r4-r11,pc}

.Lil_scalar:
    mov ip, #4
.Lil_scalar_inner:
    ldrh r4, [r0], #2
    ldrh r5, [r1], #2
    strh r4, [r2], #2
    strh r5, [r2], #2
    subs ip, ip, #1
    bne .Lil_scalar_inner
    subs r3, r3, #1
    bne .Lil_scalar
    pop {r4-r11,pc}
    .fnend
.size Soh3dsInterleaveArm11, .-Soh3dsInterleaveArm11

/* Every-other-sample copy for aInterl (reverb downsample).
 * r0 = in, r1 = out, r2 = groups of 8 output samples (16 consumed).
 * PKHBT pairs two input words into one output word, so eight outputs cost one
 * ldmia, four packs and one stmia instead of sixteen scalar accesses.
 *
 * The reference reads and writes sequentially, so overlap is observable. The
 * burst path loads 16 samples before storing 8, which is only equivalent when
 * the destination does not run ahead of the source; out>in falls back to the
 * scalar sequence that matches the reference exactly.
 */
.section .text.Soh3dsInterlArm11,"ax",%progbits
.align 2
.global Soh3dsInterlArm11
.type Soh3dsInterlArm11,%function
Soh3dsInterlArm11:
    .fnstart
    cmp r2, #0
    bxeq lr
    .save {r4-r11,lr}
    push {r4-r11,lr}

    cmp r1, r0
    bhi .Lnt_scalar             /* out > in: preserve sequential alias order */
    orr r3, r0, r1
    tst r3, #3
    bne .Lnt_scalar

.Lnt_fast:
    ldmia r0!, {r4-r11}         /* 16 input samples */
    pkhbt r4, r4, r5, lsl #16
    pkhbt r5, r6, r7, lsl #16
    pkhbt r6, r8, r9, lsl #16
    pkhbt r7, r10, r11, lsl #16
    stmia r1!, {r4-r7}          /* 8 output samples */
    subs r2, r2, #1
    bne .Lnt_fast
    pop {r4-r11,pc}

.Lnt_scalar:
    mov r3, #8
.Lnt_scalar_inner:
    ldrh r4, [r0], #4
    strh r4, [r1], #2
    subs r3, r3, #1
    bne .Lnt_scalar_inner
    subs r2, r2, #1
    bne .Lnt_scalar
    pop {r4-r11,pc}
    .fnend
.size Soh3dsInterlArm11, .-Soh3dsInterlArm11
.section .note.GNU-stack,"",%progbits
