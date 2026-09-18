.syntax unified
.arch armv6k
.fpu vfp
.arm
.text
.align 2
.global Soh3dsSkinMatrixMultAbi
.type Soh3dsSkinMatrixMultAbi,%function
Soh3dsSkinMatrixMultAbi:
    push {r4-r6,lr}
    vpush {d8-d15}
    ldr r3, =abi_values
    vldmia r3, {d8-d15}
    bl Soh3dsSkinMatrixMultArm11
    sub sp, sp, #64
    vstmia sp, {d8-d15}
    mov r4, sp
    ldr r5, =abi_values
    mov r6, #16
1:
    ldr r0, [r4], #4
    ldr r1, [r5], #4
    cmp r0, r1
    bne 2f
    subs r6, r6, #1
    bne 1b
    mov r0, #0
    b 3f
2:
    mov r0, #1
3:
    add sp, sp, #64
    vpop {d8-d15}
    pop {r4-r6,pc}
.size Soh3dsSkinMatrixMultAbi, .-Soh3dsSkinMatrixMultAbi
.align 3
abi_values:
    .word 0x01234567,0x89abcdef,0x10213243,0x54657687
    .word 0x98badcfe,0x55aa33cc,0x0f1e2d3c,0x4b5a6978
    .word 0x87654321,0xfedcba98,0x13579bdf,0x2468ace0
    .word 0xdeadbeef,0xc001d00d,0x7fc12345,0x80000001
.section .note.GNU-stack,"",%progbits
