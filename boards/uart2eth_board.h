/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

// Custom board definition for UART2ETH project
// Based on pico2 but configured for boards without crystal oscillator

#ifndef _BOARDS_UART2ETH_BOARD_H
#define _BOARDS_UART2ETH_BOARD_H

#include <boards/pico2.h>

// For board detection
#define UART2ETH_BOARD

// CRITICAL: Clock configuration for external 12MHz waveform generator
// Configure XOSC for external clock input on XIN pin
#ifndef XOSC_MHZ
#define XOSC_MHZ 12
#endif

#ifndef XOSC_HZ
#define XOSC_HZ 12000000
#endif

// Reduce startup delay since external clock should be stable immediately
#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 1
#endif

#endif