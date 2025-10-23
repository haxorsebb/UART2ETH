/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

// Custom board definition for UART2ETH project with external 12MHz waveform generator
// Use this if your waveform generator is connected to XOSC pins

#ifndef _BOARDS_UART2ETH_EXTERNAL_CLK_H
#define _BOARDS_UART2ETH_EXTERNAL_CLK_H

// For board detection
#define UART2ETH_EXTERNAL_CLK_BOARD

// --- RP2350 VARIANT ---
#define PICO_RP2350 1

#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif

#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 27
#endif

#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 28
#endif

// --- LED ---
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

// --- I2C ---
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 0
#endif

#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 4
#endif

#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 5
#endif

// --- SPI ---
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif

#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif

#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif

#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif

#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 17
#endif

// --- FLASH ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

// Drive high to force power supply into PWM mode (lower ripple on 3V3 at light loads)
#ifndef PICO_SMPS_MODE_PIN
#define PICO_SMPS_MODE_PIN 23
#endif

// CRITICAL: Clock configuration for external 12MHz waveform generator
// Configure for external clock source on XOSC pins
#ifndef XOSC_MHZ
#define XOSC_MHZ 12
#endif

// Reduce startup delay since waveform generator should be stable immediately
#ifndef PICO_XOSC_STARTUP_DELAY_MULTIPLIER
#define PICO_XOSC_STARTUP_DELAY_MULTIPLIER 1
#endif

// Use external clock (waveform generator) instead of crystal
// This assumes your 12MHz waveform generator is connected to XIN pin
#ifndef PICO_XOSC_MODE
#define PICO_XOSC_MODE XOSC_MODE_BYPASS  // Bypass mode for external clock
#endif

// Configure for RP2350 ARM mode
#ifndef PICO_RP2350_ARM_S
#define PICO_RP2350_ARM_S 1
#endif

#endif