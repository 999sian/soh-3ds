/* ARM11 64-byte burst word copy using VFPv2 load/store multiple.
 * s0-s15 are caller-saved in AAPCS-VFP: no stack frame or callee-saved registers.
 * dst and src must be at least word-aligned; count is number of 32-bit words.
 */
.syntax unified
.arch armv6k
.fpu vfp
.arm
.section .text.Soh3dsCopyWordsArm11,"ax",%progbits
.align 2
.global Soh3dsCopyWordsArm11
.type Soh3dsCopyWordsArm11,%function
Soh3dsCopyWordsArm11:
    .fnstart
    cmp r2, #0
    bxeq lr

.Lcopy64:
    cmp r2, #16
    blo .Lcopy_tail
    pld [r1, #128]
    vldmia r1!, {s0-s15}
    vstmia r0!, {s0-s15}
    sub r2, r2, #16
    b .Lcopy64

.Lcopy_tail:
    cmp r2, #4
    blo .Lcopy_word
    vldmia r1!, {s0-s3}
    vstmia r0!, {s0-s3}
    sub r2, r2, #4
    b .Lcopy_tail

.Lcopy_word:
    cmp r2, #1
    bxlo lr
    ldr r3, [r1], #4
    str r3, [r0], #4
    subs r2, r2, #1
    bne .Lcopy_word
    bx lr
    .fnend
.size Soh3dsCopyWordsArm11, .-Soh3dsCopyWordsArm11
.section .note.GNU-stack,"",%progbits
