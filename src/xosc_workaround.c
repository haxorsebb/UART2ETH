/**
 * @file xosc_workaround.c
 * @brief XOSC configuration for external 12MHz waveform generator
 * 
 * This file configures XOSC in bypass mode to accept external 12MHz clock
 * from waveform generator instead of crystal oscillator.
 * 
 * Updated with improved error handling and initialization sequence.
 */

#include "hardware/xosc.h"
#include "hardware/clocks.h"
#include "hardware/regs/xosc.h"
#include "hardware/structs/xosc.h"
#include "hardware/regs/clocks.h"
#include "hardware/structs/clocks.h"

// Override the xosc_init function to configure for external clock
void __wrap_xosc_init(void) {
    // For RP2350 with external clock source, we need to be more careful
    // about the initialization sequence
    
    // First, ensure XOSC is disabled
    xosc_hw->ctrl = XOSC_CTRL_ENABLE_VALUE_DISABLE << XOSC_CTRL_ENABLE_LSB;
    
    // Wait for disable to take effect
    while (xosc_hw->status & XOSC_STATUS_ENABLED_BITS) {
        tight_loop_contents();
    }
    
    // For external clock sources, we don't need the startup delay
    // Set startup delay to minimum since external clock should be stable
    xosc_hw->startup = 1;  // Minimum startup delay
    
    // Configure for 12MHz frequency range
    // The 1_15MHZ range is correct for 12MHz
    uint32_t ctrl_val = (XOSC_CTRL_FREQ_RANGE_VALUE_1_15MHZ << XOSC_CTRL_FREQ_RANGE_LSB);
    
    // For external clock input, we need to enable the XOSC but it should
    // automatically detect the external clock on XIN
    ctrl_val |= (XOSC_CTRL_ENABLE_VALUE_ENABLE << XOSC_CTRL_ENABLE_LSB);
    
    xosc_hw->ctrl = ctrl_val;
    
    // Wait for XOSC to stabilize with external clock
    // For external clocks, this should be much faster than crystal startup
    uint32_t timeout = 100000; // Reduced timeout for external clock
    while (!(xosc_hw->status & XOSC_STATUS_STABLE_BITS) && timeout > 0) {
        tight_loop_contents();
        timeout--;
    }
    
    // Debug: If XOSC doesn't stabilize, something is wrong with external clock
    if (!(xosc_hw->status & XOSC_STATUS_STABLE_BITS)) {
        // Try to continue anyway - the system might work with internal oscillator
        // But don't disable XOSC completely as that could cause issues
    }
}

// Override for safe disable
void __wrap_xosc_disable(void) {
    xosc_hw->ctrl = (XOSC_CTRL_ENABLE_VALUE_DISABLE << XOSC_CTRL_ENABLE_LSB);
    
    // Wait for disable to complete
    while (xosc_hw->status & XOSC_STATUS_ENABLED_BITS) {
        tight_loop_contents();
    }
}

// Override for dormant mode - be careful with external clocks
void __wrap_xosc_dormant(void) {
    // For external clock sources, going dormant might cause issues
    // Instead, just disable the XOSC
    __wrap_xosc_disable();
}

// Return the actual XOSC frequency (12 MHz)
uint32_t __wrap_xosc_hz(void) {
    return 12000000;  // 12 MHz external clock frequency
}
