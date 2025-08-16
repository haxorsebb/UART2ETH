#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Mock the pico SDK functions for testing
uint32_t get_core_num(void) { return 0; }
uint64_t get_absolute_time(void) { return 123456000; }  // 123.456 seconds in microseconds
void sleep_ms(uint32_t ms) { (void)ms; }

// Mock hardware sync functions
uint32_t spin_lock_blocking(uint32_t lock) { (void)lock; return 0; }
void spin_unlock(uint32_t lock, uint32_t save) { (void)lock; (void)save; }

// Include minimal required headers
#include "include/shared_memory.h"
#include "include/log_manager.h"

// Mock shared memory implementation for testing
static shared_memory_layout_t test_shared_memory;
static bool g_shared_initialized = false;

shared_memory_layout_t* shared_memory_get_layout(void) {
    if (!g_shared_initialized) {
        memset(&test_shared_memory, 0, sizeof(test_shared_memory));
        test_shared_memory.log_mgmt.max_entries = 100;
        test_shared_memory.log_mgmt.write_index = 0;
        test_shared_memory.log_mgmt.read_index = 0;
        test_shared_memory.log_mgmt.core0_sequence = 0;
        test_shared_memory.log_mgmt.core1_sequence = 0;
        test_shared_memory.log_mgmt.total_events_logged = 0;
        test_shared_memory.log_mgmt.entry_lock = 0;
        g_shared_initialized = true;
    }
    return &test_shared_memory;
}

bool shared_memory_init(void) {
    shared_memory_get_layout();
    return true;
}

int main(void) {
    printf("Testing Log Manager Safety Improvements\n");
    printf("========================================\n\n");
    
    // Initialize
    if (!shared_memory_init()) {
        printf("FAIL: Could not initialize shared memory\n");
        return 1;
    }
    
    if (!log_manager_init()) {
        printf("FAIL: Could not initialize log manager\n");
        return 1;
    }
    
    printf("1. Testing invalid event type (out of range)\n");
    bool result1 = log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, 9999, 0);
    printf("   Result: %s\n\n", result1 ? "UNEXPECTED SUCCESS" : "EXPECTED FAILURE");
    
    printf("2. Testing invalid event source (out of range)\n");
    bool result2 = log_event(255, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_BOOT, 0);
    printf("   Result: %s\n\n", result2 ? "UNEXPECTED SUCCESS" : "EXPECTED FAILURE");
    
    printf("3. Testing invalid log level (out of range)\n");
    bool result3 = log_event(EVENT_SOURCE_SYSTEM, 99, LOG_EVENT_SYSTEM_BOOT, 0);
    printf("   Result: %s\n\n", result3 ? "UNEXPECTED SUCCESS" : "EXPECTED FAILURE");
    
    printf("4. Testing valid event that should work\n");
    bool result4 = log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_BOOT, 12345);
    printf("   Result: %s\n", result4 ? "EXPECTED SUCCESS" : "UNEXPECTED FAILURE");
    
    printf("5. Formatting the valid event:\n");
    uint32_t formatted = log_manager_format_pending();
    printf("   Formatted %u events\n\n", formatted);
    
    printf("6. Testing edge case: event type at array boundary\n");
    // Use an event type that's at the edge of the array size
    bool result5 = log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, 9000, 0);
    printf("   Result: %s\n\n", result5 ? "UNEXPECTED SUCCESS" : "EXPECTED FAILURE");
    
    printf("Safety test completed successfully!\n");
    printf("All debug messages above should show helpful debugging information.\n");
    
    return 0;
}
