.syntax unified
.arch armv6k
.fpu vfp
.arm
.text
.align 2
.global Soh3dsEmitTriangleArm11
.type Soh3dsEmitTriangleArm11,%function
// r0: output (returned advanced), r1: vertex pointers, r2: params.
// Only caller-saved VFP d0-d7 are used. Preserve the entire FPSCR, including
// exception flags; no vector mode, division, fused operations or colour table.
Soh3dsEmitTriangleArm11:
    push {r4-r8,lr}
    vmrs r7, fpscr
    ldr r5, [r2]
    mov r6, #3
    vldr s4, .Lhalf
    vldr s5, .Linv255
    tst r5, #1
    addne r3, r2, #4
    vldmiane r3, {s6-s11}
    tst r5, #2
    addne r3, r2, #28
    vldmiane r3, {s12-s15}
    and r3, r5, #3
    cmp r3, #0
    beq .Lplain
    cmp r3, #1
    beq .Ltexture
    cmp r3, #2
    beq .Luniform
    b .Lboth

// Layout selection happens once for all three vertices. The small four loops
// avoid per-vertex descriptor walks and channel/source switches.
.macro emit name, texture, uniform
.L\name:
    ldr r4, [r1], #4
    vldmia r4, {s0-s3}
    tst r5, #4
    vnegne.f32 s1, s1
    tst r5, #8
    vaddne.f32 s2, s2, s3
    vmulne.f32 s2, s2, s4
    vstmia r0!, {s0-s3}
.if \texture
    vldr s0, [r4, #16]
    vldr s1, [r4, #20]
    vmov.f32 s2, s8
    vmov.f32 s3, s9
    // VFPv2 VMLA is NOT fused: it rounds the product, then adds. Match GCC's
    // scalar instruction as well as its NaN operand priority exactly.
    vmla.f32 s2, s0, s6
    vmla.f32 s3, s1, s7
    tst r5, #16
    // Rectangles skip the half-texel add entirely, including for signed zero.
    vaddeq.f32 s2, s2, s10
    vaddeq.f32 s3, s3, s11
    vstmia r0!, {s2-s3}
.endif
    ldrb r3, [r4, #24]
    vmov s0, r3
    ldrb r3, [r4, #25]
    vmov s1, r3
    ldrb r3, [r4, #26]
    vmov s2, r3
    ldrb r3, [r4, #27]
    vmov s3, r3
    vcvt.f32.u32 s0, s0
    vcvt.f32.u32 s1, s1
    vcvt.f32.u32 s2, s2
    vcvt.f32.u32 s3, s3
    vmul.f32 s0, s0, s5
    vmul.f32 s1, s1, s5
    vmul.f32 s2, s2, s5
    vmul.f32 s3, s3, s5
    vstmia r0!, {s0-s3}
.if \uniform
    vstmia r0!, {s12-s15}
.endif
    subs r6, r6, #1
    bne .L\name
    b .Ldone
.endm
emit plain, 0, 0
emit texture, 1, 0
emit uniform, 0, 1
emit both, 1, 1
.Ldone:
    vmsr fpscr, r7
    pop {r4-r8,pc}
.align 2
.Lhalf: .float 0.5
.Linv255: .word 0x3b808081
.size Soh3dsEmitTriangleArm11, .-Soh3dsEmitTriangleArm11
// Complete stock fog triangle. Invariants occupy s8-s28; preserve
// d8-d14, the only callee-saved VFP registers touched. Frame size 24+56 is
// eight-byte aligned. Full FPSCR is restored, including cumulative flags.
.align 2
.global Soh3dsEmitFogTriangleArm11
.type Soh3dsEmitFogTriangleArm11,%function
Soh3dsEmitFogTriangleArm11:
    push {r4-r8,lr}
    vpush {d8-d14}
    vmrs r7, fpscr
    ldr r5, [r2]
    mov r6, #3
    vldr s6, .LfogInv255
    vldr s7, .LfogHalf
    add r3, r2, #4
    vldmia r3, {s8-s19}
    add r3, r2, #52
    vldmia r3, {s20-s22}
    add r3, r2, #64
    vldmia r3, {s24-s26}
    tst r5, #1
    addne r3, r2, #76
    vldmiane r3, {s27-s28}
.LopaqueFogVertex:
    ldr r4, [r1], #4
    vldmia r4, {s0-s3}
    tst r5, #4
    vnegne.f32 s1, s1
    tst r5, #8
    vaddne.f32 s2, s2, s3
    vmulne.f32 s2, s2, s7
    vstmia r0!, {s0-s3}

    vldr s0, [r4, #16]
    vldr s1, [r4, #20]
    vmov.f32 s2, s10
    vmov.f32 s3, s11
    vmla.f32 s2, s0, s8
    vmla.f32 s3, s1, s9
    tst r5, #16
    vaddeq.f32 s2, s2, s12
    vaddeq.f32 s3, s3, s13
    vstmia r0!, {s2-s3}
    // Keep the same non-fused VFPv2 VMLA/half-add order for slot 1.
    vmov.f32 s2, s16
    vmov.f32 s3, s17
    vmla.f32 s2, s0, s14
    vmla.f32 s3, s1, s15
    vaddeq.f32 s2, s2, s18
    vaddeq.f32 s3, s3, s19
    vstmia r0!, {s2-s3}

    vstmia r0!, {s20-s22}
    ldrb r3, [r4, #27]
    vmov s0, r3
    vcvt.f32.u32 s0, s0
    vmul.f32 s0, s0, s6
    vstmia r0!, {s0}
    ldrb r3, [r4, #24]
    vmov s0, r3
    ldrb r3, [r4, #25]
    vmov s1, r3
    ldrb r3, [r4, #26]
    vmov s2, r3
    vcvt.f32.u32 s0, s0
    vcvt.f32.u32 s1, s1
    vcvt.f32.u32 s2, s2
    vmul.f32 s0, s0, s6
    vmul.f32 s1, s1, s6
    vmul.f32 s2, s2, s6
    vstmia r0!, {s0-s2}
    tst r5, #1
    vstmiane r0!, {s27}
    vstmia r0!, {s24-s26}
    vstmiane r0!, {s28}
    subs r6, r6, #1
    bne .LopaqueFogVertex
    vmsr fpscr, r7
    vpop {d8-d14}
    pop {r4-r8,pc}
.align 2
.LfogHalf: .float 0.5
.LfogInv255: .word 0x3b808081
.size Soh3dsEmitFogTriangleArm11, .-Soh3dsEmitFogTriangleArm11
.section .note.GNU-stack,"",%progbits
