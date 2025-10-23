# 12MHz XIN Signal Verification Guide

## Hardware Verification Methods

### Method 1: Oscilloscope Measurement ⭐ **RECOMMENDED**
**Connection:**
- Probe: Connect to XIN pin (GPIO 20 on RP2350)
- Ground: Connect scope ground to board ground
- Probe setting: 10x probe, 50 ohm termination OFF

**Expected Signal Characteristics:**
- **Frequency**: 12.000 MHz (±0.01% tolerance)
- **Amplitude**: 0V to 3.3V (CMOS levels)
- **Rise/Fall Time**: < 5ns (for reliable digital operation)
- **Duty Cycle**: 45-55% (50% ideal)
- **Jitter**: < 1ns RMS
- **Overshoot**: < 10% of amplitude

**What to Check:**
```
✅ Clean square wave (no ringing, overshoot)
✅ Stable frequency reading
✅ Proper voltage levels (0V low, 3.3V high)
✅ Fast edges (< 5ns rise/fall time)
✅ No missing pulses or glitches
```

**Oscilloscope Settings:**
```
Timebase: 100ns/div (to see ~1.2 cycles)
Voltage: 1V/div (to see 0-3.3V range)
Trigger: Edge, rising, ~1.65V level
Acquisition: Normal mode, auto trigger
```

### Method 2: Logic Analyzer
**Benefits:** Better for digital signal analysis, frequency counting
**Connection:** Channel 0 → XIN pin (GPIO 20)
**Settings:** 
- Sample rate: 100 MHz minimum
- Trigger: Rising edge
- Measure: Frequency, period, duty cycle

### Method 3: Frequency Counter
**Simple Check:** Use a dedicated frequency counter
**Expected:** 12.000000 MHz ±100 Hz

## Software Verification Methods

### Method 1: RP2350 Built-in Frequency Counter
**Use the RP2350's internal frequency measurement capability:**

```c
// Add to your main.c for testing
#include "hardware/clocks.h"

void measure_xosc_frequency(void) {
    // Measure XOSC frequency using built-in counter
    uint32_t measured_freq = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_XOSC_CLKSRC);
    printf("Measured XOSC frequency: %lu kHz\n", measured_freq);
    printf("Expected: 12000 kHz\n");
    
    if (measured_freq > 11900 && measured_freq < 12100) {
        printf("✅ XOSC frequency OK\n");
    } else {
        printf("❌ XOSC frequency ERROR\n");
    }
}
```

### Method 2: Check XOSC Status Registers
**Monitor XOSC hardware status:**

```c
#include "hardware/structs/xosc.h"

void check_xosc_status(void) {
    printf("XOSC Status Register: 0x%08lx\n", xosc_hw->status);
    
    if (xosc_hw->status & XOSC_STATUS_STABLE_BITS) {
        printf("✅ XOSC is stable\n");
    } else {
        printf("❌ XOSC not stable\n");
    }
    
    if (xosc_hw->status & XOSC_STATUS_ENABLED_BITS) {
        printf("✅ XOSC is enabled\n");
    } else {
        printf("❌ XOSC not enabled\n");
    }
    
    // Check frequency range detection
    uint32_t freq_range = (xosc_hw->status & XOSC_STATUS_FREQ_RANGE_BITS) >> XOSC_STATUS_FREQ_RANGE_LSB;
    printf("XOSC frequency range: 0x%lx\n", freq_range);
}
```

### Method 3: Output Reference Clock for External Measurement
**Route internal clock to GPIO for external measurement:**

```c
#include "hardware/clocks.h"
#include "hardware/gpio.h"

void output_clock_for_measurement(void) {
    // Output XOSC clock to GPIO 21 (XOUT pin) for measurement
    // This allows you to measure the processed XOSC signal
    
    // Configure GPIO 21 as clock output
    gpio_set_function(21, GPIO_FUNC_GPCK);
    
    // Configure clock output to show XOSC
    clock_gpio_init(21, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_XOSC_CLKSRC, 1);
    
    printf("XOSC clock output on GPIO 21 for measurement\n");
}
```

## Troubleshooting Common Issues

### Issue 1: No Signal at XIN
**Symptoms:** Oscilloscope shows flat line or noise
**Causes:**
- Waveform generator not connected
- Waveform generator disabled
- Wrong pin connection
- Broken wire/connection

**Debug Steps:**
1. Check waveform generator output directly
2. Verify wiring continuity
3. Check if XIN pin is actually GPIO 20

### Issue 2: Wrong Frequency
**Symptoms:** Frequency ≠ 12.000 MHz
**Causes:**
- Waveform generator misconfigured
- Frequency drift in generator
- Loading effects

**Debug Steps:**
1. Measure generator output directly (without board connected)
2. Check generator settings and calibration
3. Verify generator load specifications

### Issue 3: Poor Signal Quality
**Symptoms:** Ringing, overshoot, slow edges
**Causes:**
- Impedance mismatch
- Long wires/poor connections  
- Ground loop issues
- Generator output not suited for CMOS

**Debug Steps:**
1. Use shorter, proper coaxial cable
2. Check ground connections
3. Add termination if needed
4. Verify generator output levels (0-3.3V)

### Issue 4: Intermittent Operation
**Symptoms:** Sometimes works, sometimes hangs
**Causes:**
- Loose connections
- Marginal signal levels
- Temperature-sensitive issues
- Power supply noise

**Debug Steps:**
1. Check all connections for reliability
2. Monitor signal over time/temperature
3. Check power supply quality
4. Add decoupling capacitors if needed

## RP2350 XIN Pin Specifications

**Pin Details:**
- **Pin Number:** GPIO 20 (XIN)
- **Input Type:** CMOS Schmitt trigger
- **Voltage Levels:** 
  - VIL (Low): < 0.8V
  - VIH (High): > 2.0V
- **Input Impedance:** ~1MΩ || ~5pF
- **Maximum Frequency:** 50 MHz
- **Minimum Edge Rate:** 1V/μs

## Quick Verification Checklist

```
□ Waveform generator set to 12.000000 MHz
□ Generator output enabled and connected to XIN (GPIO 20)
□ Ground connection between generator and board
□ Signal amplitude: 0V to 3.3V
□ Clean square wave (no distortion)
□ Stable frequency (no drift)
□ Fast rise/fall times (< 5ns)
□ Board powered on during measurement
```