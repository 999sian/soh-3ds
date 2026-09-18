/* Test-only trampoline: seed and check all AAPCS callee-saved core registers. */
.syntax unified
.arch armv6k
.arm
.text
.align 2
.global Soh3dsFilterAbiCheck
.type Soh3dsFilterAbiCheck,%function
Soh3dsFilterAbiCheck:
    push {r4-r11,lr}
    sub sp, sp, #4
    mov r4, #0x44
    mov r5, #0x55
    mov r6, #0x66
    mov r7, #0x77
    mov r8, #0x88
    mov r9, #0x99
    mov r10, #0xaa
    mov r11, #0xbb
    bl Soh3dsFilterArm11
    cmp r4, #0x44
    bne .Labi_bad
    cmp r5, #0x55
    bne .Labi_bad
    cmp r6, #0x66
    bne .Labi_bad
    cmp r7, #0x77
    bne .Labi_bad
    cmp r8, #0x88
    bne .Labi_bad
    cmp r9, #0x99
    bne .Labi_bad
    cmp r10, #0xaa
    bne .Labi_bad
    cmp r11, #0xbb
    bne .Labi_bad
    mov r0, #0
    b .Labi_done
.Labi_bad:
    mov r0, #1
.Labi_done:
    add sp, sp, #4
    pop {r4-r11,pc}
.size Soh3dsFilterAbiCheck, .-Soh3dsFilterAbiCheck
.section .note.GNU-stack,"",%progbits
