// SPDX-License-Identifier: Zlib
// SPDX-FileNotice: Modified from the original version by the BlocksDS project.
//
// Copyright (C) 2020 Gericom
// Copyright (C) 2023 Antonio Niño Díaz

#include <teak/asminc.h>
#include <teak/cpuregs.h>

BEGIN_ASM_FUNC cpuDisableIrqs

    dint
    ret     always

BEGIN_ASM_FUNC cpuEnableIrqs

    eint
    ret     always

BEGIN_ASM_FUNC cpuDisableInt0

    rst     MOD3_IRQ_INT0_MASK, mod3
    ret     always

BEGIN_ASM_FUNC cpuEnableInt0

    set     MOD3_IRQ_INT0_ENABLE, mod3
    ret     always

BEGIN_ASM_FUNC cpuDisableInt1

    rst     MOD3_IRQ_INT1_MASK, mod3
    ret     always

BEGIN_ASM_FUNC cpuEnableInt1

    set     MOD3_IRQ_INT1_ENABLE, mod3
    ret     always

BEGIN_ASM_FUNC cpuDisableInt2

    rst     MOD3_IRQ_INT2_MASK, mod3
    ret     always

BEGIN_ASM_FUNC cpuEnableInt2

    set     MOD3_IRQ_INT2_ENABLE, mod3
    ret     always

BEGIN_ASM_FUNC cpuDisableVInt

    rst     MOD3_IRQ_VINT_MASK, mod3
    ret     always

BEGIN_ASM_FUNC cpuEnableVInt

    set     MOD3_IRQ_VINT_ENABLE, mod3
    ret     always
