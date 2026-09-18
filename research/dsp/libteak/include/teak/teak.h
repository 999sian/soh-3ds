// SPDX-License-Identifier: Zlib
// SPDX-FileNotice: Modified from the original version by the BlocksDS project.
//
// Copyright (C) 2020 Gericom
// Copyright (c) 2023 Antonio Niño Díaz

#ifndef LIBTEAK_TEAK_H__
#define LIBTEAK_TEAK_H__

#ifdef __cplusplus
extern "C" {
#endif

/// @file teak/teak.h
///
/// @brief The global include file for libteak.
///
/// @mainpage libteak documentation
///
/// @section intro Introduction
/// Welcome to the libteak reference documentation, a library that helps develop
/// applications for the DSP included in the DSi and 3DS consoles.
///
/// @section library_api libteak modules
/// - @ref teak/ahbm.h "AHBM (ARM AMBA AHB bus) utilities"
/// - @ref teak/apbp.h "Host Port Interface (APBP aka HPI)"
/// - @ref teak/btdmp.h "BTDMP (speakers/microphone FIFO)"
/// - @ref teak/dma.h "DMA helpers"
/// - @ref teak/global.h "Global helpers"
/// - @ref teak/icu.h "ICU helpers"
/// - @ref teak/timer.h "Timer utilities"

#include <teak/ahbm.h>
#include <teak/apbp.h>
#include <teak/btdmp.h>
#include <teak/cpu.h>
#include <teak/dma.h>
#include <teak/global.h>
#include <teak/icu.h>
#include <teak/timer.h>
#include <teak/types.h>

#ifdef __cplusplus
}
#endif

#endif // LIBTEAK_TEAK_H__
