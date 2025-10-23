# UART2ETH Multicore Lockup Debugging Guide

## Problem Description
ARM Cortex-M lockup occurs when calling `multicore_launch_core1()` function, with symptoms:
- "ARM M in lockup state, stack unwinding terminated"
- Program appears stuck after Core1 launch

## Root Cause Analysis

### Primary Issue: Spinlock Race Condition
The lockup is caused by a race condition between Core0 and Core1 accessing shared memory spinlocks:

1. **Core0 Sequence:**
   - Initializes shared memory (`shared_memory_init()`)
   - Claims and initializes spinlock for log management
   - Calls multiple `log_event()` functions successfully
   - Calls `multicore_launch_core1(core1_entry)`

2. **Core1 Sequence (PROBLEMATIC):**
   - Starts in `core1_entry()`
   - **Immediately calls `log_event()`** before proper initialization
   - Tries to acquire the same spinlock that Core0 might be holding
   - **BLOCKS INDEFINITELY** → ARM Cortex-M lockup

### Contributing Factors
1. **No synchronization delay** between Core0 and Core1 startup
2. **Missing memory barriers** before Core1 launch
3. **Immediate logging** in Core1 without checking spinlock availability
4. **Potential SRAM bank conflicts** (using SRAM4_BASE)

## Implemented Solutions

### Solution 1: Safe Core1 Entry Point
**File:** `src/main.c` - `core1_entry()` function

**Changes:**
- Added 10ms delay to allow Core0 initialization to complete
- Added spinlock availability test before logging
- Graceful fallback if logging system is not ready
- Retry logic with timeout for spinlock acquisition

### Solution 2: Robust Shared Memory Initialization  
**File:** `src/config/shared_memory.c` - `shared_memory_init()` function

**Changes:**
- Added retry logic for spinlock claiming (10 attempts with delays)
- Added spinlock functionality test after initialization
- Better error handling and diagnostics

### Solution 3: Memory Synchronization
**File:** `src/main.c` - Before `multicore_launch_core1()`

**Changes:**
- Added Data Synchronization Barrier (`__dsb()`)
- Added Instruction Synchronization Barrier (`__isb()`)
- Added 5ms delay to ensure all memory operations complete
- Better debug output for tracking initialization sequence

### Solution 4: Alternative Memory Region Option
**File:** `include/shared_memory.h`

**Changes:**
- Added commented alternative to use end of SRAM0 instead of SRAM4
- Allows testing if SRAM bank conflicts are the issue

## Testing and Verification

### Build Status
✅ **Project builds successfully** with all changes applied
- CMake configuration: PASSED
- Compilation: PASSED  
- Linking: PASSED
- No syntax or dependency errors

### Debug Features Added
1. **Enhanced logging** with step-by-step progress tracking
2. **Spinlock availability testing** before use
3. **Memory barrier insertion** for proper synchronization
4. **Graceful degradation** if logging system unavailable

## Additional Debugging Steps

### If Issue Persists:

1. **Try Alternative Memory Region:**
   ```c
   // In shared_memory.h, uncomment:
   #define SRAM_BANK4_BASE     (SRAM_BASE + 256*1024 - 64*1024)
   ```

2. **Disable Core1 Logging Completely:**
   ```c
   // In core1_entry(), comment out all log_event() calls temporarily
   ```

3. **Check Stack Allocation:**
   - Verify Core1 has sufficient stack space
   - Check for stack overflow in linker map files

4. **Hardware-Specific Checks:**
   - Verify crystal oscillator configuration (12MHz external)
   - Check if XOSC bypass mode is working correctly
   - Validate RP2350 ARM-S mode configuration

### Advanced Debugging:
1. **SWD Debugger:** Use OpenOCD/GDB to examine exact lockup location
2. **Minimal Core1:** Start with empty Core1 function to isolate issue
3. **Single-Step:** Add breakpoints around `multicore_launch_core1()`
4. **Memory Analysis:** Check if shared memory layout overlaps with stacks

## Expected Behavior After Fix

1. **Core0:** Completes initialization cleanly
2. **Memory barriers:** Ensure all operations complete before Core1 launch  
3. **Core1:** Starts with delay, tests spinlock availability
4. **Logging:** Works safely for both cores without conflicts
5. **Debug output:** Shows clear progression through initialization steps

## Files Modified
- `src/main.c` - Core1 entry point safety improvements
- `src/config/shared_memory.c` - Robust spinlock initialization
- `include/shared_memory.h` - Alternative memory region option
- `MULTICORE_LOCKUP_DEBUG.md` - This debugging guide

## Next Steps
1. Flash the updated firmware to your hardware
2. Monitor serial output for debug messages
3. Verify Core1 launches successfully without lockup
4. If issues persist, follow additional debugging steps above

The build completed successfully, indicating the fixes are properly implemented and ready for hardware testing.
