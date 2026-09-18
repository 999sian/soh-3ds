/* Game matrix copy: 16 floats, word-aligned. VFPv2 register transfers perform
 * no floating-point arithmetic (NaN payloads and signed zero stay unchanged).
 * s0-s15 are caller-saved in AAPCS-VFP. No stack or callee-saved registers used.
 * Preserve the C routine's sequential semantics for partially overlapping
 * ranges; the normal disjoint and exact-in-place cases use the fast path.
 */
.syntax unified
.arch armv6k
.fpu vfp
.arm
.section .text.Matrix_MtxFCopy,"ax",%progbits
.align 2
.global Matrix_MtxFCopy
.type Matrix_MtxFCopy,%function
Matrix_MtxFCopy:
    .fnstart
    sub     r2, r0, r1
    cmp     r2, #0
    beq     .Lcopy
    cmp     r2, #64
    blo     Soh3dsMatrixCopyReference
    cmn     r2, #64
    bhi     Soh3dsMatrixCopyReference
.Lcopy:
    vldmia  r1, {s0-s15}
    vstmia  r0, {s0-s15}
    bx      lr
    .fnend
.size Matrix_MtxFCopy, .-Matrix_MtxFCopy
.section .note.GNU-stack,"",%progbits
