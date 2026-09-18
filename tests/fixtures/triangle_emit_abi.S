.syntax unified
.arch armv6k
.fpu vfp
.arm
.text
.align 2
.global Soh3dsTriangleEmitAbi
.type Soh3dsTriangleEmitAbi,%function
Soh3dsTriangleEmitAbi:
    ldr r12, =Soh3dsEmitTriangleArm11
    b .LcheckAbi
.global Soh3dsFogEmitAbi
.type Soh3dsFogEmitAbi,%function
Soh3dsFogEmitAbi:
    ldr r12, =Soh3dsEmitFogTriangleArm11
.LcheckAbi:
    push {r4-r11,lr}
    vpush {d8-d15}
    sub sp, sp, #12
    vmrs r3, fpscr
    str r3, [sp]
    ldr r3, =0x00c00009
    str r3, [sp, #4]
    vmsr fpscr, r3
    ldr r3, =.Lvalues
    vldmia r3, {d8-d15}
    mov r4, #0x44
    mov r5, #0x55
    mov r6, #0x66
    mov r7, #0x77
    mov r8, #0x88
    mov r9, #0x99
    mov r10, #0xaa
    mov r11, #0xbb
    blx r12
    cmp r4, #0x44
    cmpeq r5, #0x55
    cmpeq r6, #0x66
    cmpeq r7, #0x77
    cmpeq r8, #0x88
    cmpeq r9, #0x99
    cmpeq r10, #0xaa
    cmpeq r11, #0xbb
    bne .Lbad
    vmrs r0, fpscr
    ldr r1, [sp, #4]
    cmp r0, r1
    bne .Lbad
    sub sp, sp, #64
    vstmia sp, {d8-d15}
    mov r4, sp
    ldr r5, =.Lvalues
    mov r6, #16
.Lcompare:
    ldr r0, [r4], #4
    ldr r1, [r5], #4
    cmp r0, r1
    bne .Lvfpbad
    subs r6, r6, #1
    bne .Lcompare
    mov r0, #0
    add sp, sp, #64
    b .Lrestore
.Lvfpbad:
    add sp, sp, #64
.Lbad:
    mov r0, #1
.Lrestore:
    ldr r3, [sp]
    vmsr fpscr, r3
    add sp, sp, #12
    vpop {d8-d15}
    pop {r4-r11,pc}
.size Soh3dsTriangleEmitAbi, .-Soh3dsTriangleEmitAbi
.size Soh3dsFogEmitAbi, .-Soh3dsFogEmitAbi
.align 3
.Lvalues:
    .word 0x01234567,0x89abcdef,0x10213243,0x54657687
    .word 0x98badcfe,0x55aa33cc,0x0f1e2d3c,0x4b5a6978
    .word 0x87654321,0xfedcba98,0x13579bdf,0x2468ace0
    .word 0xdeadbeef,0xc001d00d,0x7fc12345,0x80000001
.section .note.GNU-stack,"",%progbits
