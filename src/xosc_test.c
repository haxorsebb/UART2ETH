/**
 * @file xosc_test.c
 * @brief XOSC signal verification functions
 * 
 * Functions to verify that the 12MHz external clock signal is working correctly.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/structs/xosc.h"
#include "hardware/gpio.h"

/**
 * @brief Check XOSC status registers and print diagnostics
 */
void xosc_diagnostic_check(void) {
    printf("\n=== XOSC Diagnostic Check ===\n");
    
    // Read XOSC status register
    uint32_t status = xosc_hw->status;
    printf("XOSC Status Register: 0x%08lx\n", status);
    
    // Check individual status bits
    if (status & XOSC_STATUS_STABLE_BITS) {
        printf("✅ XOSC is STABLE\n");
    } else {
        printf("❌ XOSC is NOT STABLE\n");
    }
    
    if (status & XOSC_STATUS_ENABLED_BITS) {
        printf("✅ XOSC is ENABLED\n");
    } else {
        printf("❌ XOSC is NOT ENABLED\n");
    }
    
    // Check frequency range detection
    uint32_t freq_range = (status & XOSC_STATUS_FREQ_RANGE_BITS) >> XOSC_STATUS_FREQ_RANGE_LSB;
    printf("XOSC Frequency Range: 0x%lx ", freq_range);
    switch(freq_range) {
        case 0xaa0: printf("(1-15 MHz) ✅\n"); break;
        case 0xaa1: printf("(10-30 MHz)\n"); break;
        case 0xaa2: printf("(25-60 MHz)\n"); break;
        case 0xaa3: printf("(40-100 MHz)\n"); break;
        default: printf("(UNKNOWN) ❌\n"); break;
    }
    
    // Read control register
    uint32_t ctrl = xosc_hw->ctrl;
    printf("XOSC Control Register: 0x%08lx\n", ctrl);
    
    uint32_t enable_val = (ctrl & XOSC_CTRL_ENABLE_BITS) >> XOSC_CTRL_ENABLE_LSB;
    printf("XOSC Enable Value: 0x%lx ", enable_val);
    if (enable_val == XOSC_CTRL_ENABLE_VALUE_ENABLE) {
        printf("(ENABLED) ✅\n");
    } else if (enable_val == XOSC_CTRL_ENABLE_VALUE_DISABLE) {
        printf("(DISABLED) ❌\n");
    } else {
        printf("(INVALID) ❌\n");
    }
}

/**
 * @brief Measure XOSC frequency using built-in frequency counter
 */
void measure_xosc_frequency(void) {
    printf("\n=== XOSC Frequency Measurement ===\n");
    
    // Use the built-in frequency counter to measure XOSC
    uint32_t measured_khz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_XOSC_CLKSRC);
    
    printf("Measured XOSC Frequency: %lu kHz\n", measured_khz);
    printf("Expected Frequency: 12000 kHz\n");
    
    // Calculate error
    int32_t error_khz = (int32_t)measured_khz - 12000;
    float error_percent = (float)error_khz / 12000.0f * 100.0f;
    
    printf("Frequency Error: %ld kHz (%.3f%%)\n", error_khz, error_percent);
    
    // Check if frequency is within acceptable range (±1%)
    if (measured_khz >= 11880 && measured_khz <= 12120) {  // ±1% tolerance
        printf("✅ XOSC frequency is GOOD\n");
    } else if (measured_khz >= 11400 && measured_khz <= 12600) {  // ±5% tolerance
        printf("⚠️  XOSC frequency is MARGINAL\n");
    } else {
        printf("❌ XOSC frequency is BAD\n");
    }
}

/**
 * @brief Output XOSC clock to GPIO for external measurement
 * @param gpio_pin GPIO pin to output clock (recommend GPIO 22)
 */
void output_xosc_for_measurement(uint gpio_pin) {
    printf("\n=== XOSC Clock Output Setup ===\n");
    printf("Outputting XOSC clock to GPIO %u\n", gpio_pin);
    printf("Connect oscilloscope to GPIO %u to measure XOSC signal\n", gpio_pin);
    
    // Configure GPIO as clock output
    gpio_set_function(gpio_pin, GPIO_FUNC_GPCK);
    
    // Configure clock output to show XOSC (divide by 1)
    clock_gpio_init(gpio_pin, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_XOSC_CLKSRC, 1);
    
    printf("Clock output active - measure with oscilloscope\n");
    printf("Expected: 12 MHz square wave, 0-3.3V levels\n");
}

/**
 * @brief Comprehensive XOSC verification test
 */
void test_xosc_signal(void) {
    printf("\n████████████████████████████████████████\n");
    printf("        XOSC SIGNAL VERIFICATION TEST\n");
    printf("████████████████████████████████████████\n");
    
    // Wait a moment for clocks to stabilize
    sleep_ms(100);
    
    // Run diagnostic checks
    xosc_diagnostic_check();
    
    // Measure frequency
    measure_xosc_frequency();
    
    // Offer to output clock for external measurement
    printf("\n=== External Measurement Option ===\n");
    printf("To measure XOSC with oscilloscope:\n");
    printf("1. Connect scope probe to GPIO 22\n");
    printf("2. Call output_xosc_for_measurement(22) in your code\n");
    printf("3. Verify 12 MHz square wave signal\n");
    
    printf("\n=== Test Complete ===\n");
}

/**
 * @brief Quick XOSC status check (call this in main loop)
 */
bool is_xosc_working(void) {
    uint32_t status = xosc_hw->status;
    return (status & XOSC_STATUS_STABLE_BITS) && (status & XOSC_STATUS_ENABLED_BITS);
}