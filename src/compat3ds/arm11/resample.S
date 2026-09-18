/* ARM11 four-tap resampler. No NEON; ARMv6K halfword multiplies and saturation.
 * Each tap rounds BEFORE summation, exactly as mixer.c's C implementation.
 * The input may be only halfword-aligned, so never read it with word loads.
 * AAPCS: r4-r11 and SP preserved; no VFP registers touched.
 */
.syntax unified
.arch armv6k
.arm
.section .text.Soh3dsResampleArm11,"ax",%progbits
.align 2
.global Soh3dsResampleArm11
.type Soh3dsResampleArm11,%function
Soh3dsResampleArm11:
    .fnstart
    push    {r4-r11, lr}
    .save   {r4-r11, lr}
    ldr     r4, [sp, #36]          @ output count
    ldr     r5, [sp, #40]          @ aligned coefficient table
    ldr     r6, [r2]               @ phase (0..65535)
    mov     r7, #0x4000           @ per-tap rounding
    cmp     r4, #0
    beq     .Ldone
.Lsample:
    mov     r8, r6, lsr #10
    add     r8, r5, r8, lsl #3
    ldmia   r8, {r8, r9}          @ packed coefficients 0/1 and 2/3
    ldrh    r10, [r0]
    ldrh    r11, [r0, #2]
    smlabb  r10, r10, r8, r7
    smlabt  r11, r11, r8, r7
    ldrh    r12, [r0, #4]
    ldrh    lr, [r0, #6]
    mov     r10, r10, asr #15
    smlabb  r12, r12, r9, r7
    add     r10, r10, r11, asr #15
    smlabt  lr, lr, r9, r7
    add     r10, r10, r12, asr #15
    add     r6, r6, r3
    add     r10, r10, lr, asr #15
    mov     r8, r6, lsr #16
    ssat    r10, #16, r10
    add     r0, r0, r8, lsl #1
    strh    r10, [r1], #2
    uxth    r6, r6
    subs    r4, r4, #1
    bne     .Lsample
.Ldone:
    str     r6, [r2]
    pop     {r4-r11, pc}
    .fnend
.size Soh3dsResampleArm11, .-Soh3dsResampleArm11
.section .note.GNU-stack,"",%progbits
