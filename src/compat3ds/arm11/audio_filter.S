/* Eight-tap FIR, matching aFilterImpl's 64-bit accumulator and input history.
 * r0: in-place PCM, r1: eight history samples, r2: eight coefficients,
 * r3: sample count (multiple of eight; zero is a no-op).
 * External pointers need only halfword alignment and must be disjoint.
 * SMLALD adds two signed 16-bit products to a 64-bit accumulator. Preserve
 * the 0x4000 rounding term and saturate only after the complete sum >> 15.
 * Integer ARMv6K only; no VFP/NEON state. AAPCS r4-r11 and SP preserved.
 */
.syntax unified
.arch armv6k
.arm
.section .text.Soh3dsFilterArm11,"ax",%progbits
.align 2
.global Soh3dsFilterArm11
.type Soh3dsFilterArm11,%function
Soh3dsFilterArm11:
    .fnstart
    cmp r3, #0
    bxeq lr
    .save {r4-r11,lr}
    push {r4-r11,lr}
    .pad #36
    sub sp, sp, #36             @ aligned 16-sample window + history pointer
    str r1, [sp, #32]
    .irp offset,0,2,4,6,8,10,12,14
        ldrh r8, [r1, #\offset]
        strh r8, [sp, #\offset]
    .endr
    mov r1, r0                 @ output cursor
.macro filter_coeff reg,offset
    ldrh \reg, [r2, #\offset]
    ldrh r8, [r2, #(\offset-2)]
    orr \reg, \reg, r8, lsl #16
.endm
    filter_coeff r4,14          @ reversed pairs: 7/6, 5/4, 3/2, 1/0
    filter_coeff r5,10
    filter_coeff r6,6
    filter_coeff r7,2
.Lfilter_block:
    @ Save all eight input samples before overwriting any output.
    .irp offset,0,2,4,6,8,10,12,14
        ldrh r8, [r1, #\offset]
        strh r8, [sp, #(16+\offset)]
    .endr
    mov r0, sp
    mov lr, #8
.Lfilter_sample:
    mov r8, #0x4000
    mov r9, #0
.macro filter_pair offset,coeff
    ldrh r10, [r0, #\offset]
    ldrh r11, [r0, #(\offset+2)]
    orr r10, r10, r11, lsl #16
    smlald r8, r9, r10, \coeff
.endm
    filter_pair 0,r4
    filter_pair 4,r5
    filter_pair 8,r6
    filter_pair 12,r7
    mov r8, r8, lsr #15
    orr r8, r8, r9, lsl #17     @ the shifted sum fits signed 32 bits
    ssat r8, #16, r8
    strh r8, [r1], #2
    add r0, r0, #2
    subs lr, lr, #1
    bne .Lfilter_sample
    @ r0 now points at the saved input block, not the filtered output.
    ldmia r0, {r8-r11}
    stmia sp, {r8-r11}
    subs r3, r3, #8
    bne .Lfilter_block
    ldr r2, [sp, #32]
    .irp offset,0,2,4,6,8,10,12,14
        ldrh r8, [sp, #\offset]
        strh r8, [r2, #\offset]
    .endr
    add sp, sp, #36
    pop {r4-r11,pc}
    .fnend
.size Soh3dsFilterArm11, .-Soh3dsFilterArm11
.section .note.GNU-stack,"",%progbits
