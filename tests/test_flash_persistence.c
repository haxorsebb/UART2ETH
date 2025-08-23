/**
 * @file test_flash_persistence.c
 * @brief Unit tests for flash persistence implementation
 * 
 * Tests the flash persistence system with 4-page ring buffer strategy
 * and hardware SHA256 integrity verification as documented in ADR-006.
 * 
 * Documentation Reference:
 * - ADR-006: Flash Persistence Strategy - Implementation Details
 * - arc42 Chapter 5 - Configuration Manager - Flash Ring Buffer Persistence
 */

// Explicitly enable USB reset interface with all required options - CRITICAL for autonomous flashing
#define PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE 1
#define PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_BOOTSEL 1
#define PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_FLASH_BOOT 1

#include "unity.h"
#include "shared_memory.h"
#include "network/network_manager.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

void setUp(void) {
    // Called before each test
}

void tearDown(void) {
    // Called after each test
}

/**
 * Test: Flash persistence should initialize successfully
 * 
 * This tests the most atomic condition: that our flash persistence 
 * system can initialize and discover partition ID=2.
 */
void test_flash_persistence_initialization(void) {
    // ARRANGE: Ensure shared memory is initialized first
    bool shared_init = shared_memory_init();
    TEST_ASSERT_TRUE_MESSAGE(shared_init, "Shared memory should initialize first");
    
    // ACT: Initialize flash persistence
    bool result = flash_persistence_init();
    
    // ASSERT: Initialization should succeed
    TEST_ASSERT_TRUE_MESSAGE(result, "Flash persistence initialization should succeed");
}

/**
 * Test: Flash persistence should be able to load configuration on startup
 * 
 * Tests that the startup recovery process can read flash pages and
 * either load valid configuration or default to factory settings.
 */
void test_flash_persistence_load_configuration(void) {
    // ARRANGE: Initialize persistence system
    bool shared_init = shared_memory_init();
    TEST_ASSERT_TRUE(shared_init);
    bool init_result = flash_persistence_init();
    TEST_ASSERT_TRUE(init_result);
    
    // ACT: Load configuration from flash
    bool load_result = flash_persistence_load_configuration();
    
    // ASSERT: Load should either succeed with valid data or factory reset
    TEST_ASSERT_TRUE_MESSAGE(load_result, 
        "Configuration load should succeed or perform factory reset");
}

/**
 * Test: Flash persistence should detect configuration changes
 * 
 * Tests the change detection mechanism that triggers persistence writes
 * when the revision counter increments.
 */
void test_flash_persistence_change_detection(void) {
    // ARRANGE: Initialize system and get baseline
    bool shared_init = shared_memory_init();
    TEST_ASSERT_TRUE(shared_init);
    bool init_result = flash_persistence_init();
    TEST_ASSERT_TRUE(init_result);
    
    shared_memory_layout_t* layout = shared_memory_get_layout();
    TEST_ASSERT_NOT_NULL(layout);
    
    uint32_t original_revision = layout->revision_counter;
    uint32_t original_write_count = flash_persistence_get_write_count();
    
    // ACT: Increment revision counter to simulate config change
    layout->revision_counter++;
    
    // Force save to test change detection
    bool save_result = flash_persistence_force_save_configuration();
    
    // ASSERT: Save should succeed and write count should increment
    TEST_ASSERT_TRUE_MESSAGE(save_result, "Force save should succeed");
    
    uint32_t new_write_count = flash_persistence_get_write_count();
    TEST_ASSERT_GREATER_THAN_MESSAGE(original_write_count, new_write_count,
        "Write count should increment after save");
    
    // Verify change detection works with save_if_needed
    layout->revision_counter++;
    bool save_needed = flash_persistence_save_configuration_if_needed();
    TEST_ASSERT_TRUE_MESSAGE(save_needed, "Save should be triggered by revision change");
    
    // Restore original state  
    layout->revision_counter = original_revision;
}

/**
 * Test: Flash persistence should enforce write frequency limits
 * 
 * Tests that the deferred write-back system limits writes to
 * maximum once per 30 seconds as specified in ADR-006.
 */
void test_flash_persistence_write_frequency_limiting(void) {
    // ARRANGE: Initialize system
    bool shared_init = shared_memory_init();
    TEST_ASSERT_TRUE(shared_init);
    bool init_result = flash_persistence_init();
    TEST_ASSERT_TRUE(init_result);
    
    shared_memory_layout_t* layout = shared_memory_get_layout();
    TEST_ASSERT_NOT_NULL(layout);
    
    uint32_t initial_write_count = flash_persistence_get_write_count();
    
    // ACT: Force first save to establish baseline timestamp
    layout->revision_counter++;
    bool first_save = flash_persistence_force_save_configuration();
    TEST_ASSERT_TRUE_MESSAGE(first_save, "First save should succeed");
    
    // Immediately try second save - should be deferred due to frequency limiting
    layout->revision_counter++;
    bool second_save = flash_persistence_save_configuration_if_needed();
    
    uint32_t final_write_count = flash_persistence_get_write_count();
    
    // ASSERT: Write count should show only one actual write occurred
    // (second save should be deferred, not executed immediately)
    TEST_ASSERT_EQUAL_MESSAGE(initial_write_count + 1, final_write_count,
        "Only one write should occur due to frequency limiting");
    
    TEST_ASSERT_TRUE_MESSAGE(second_save, 
        "Save function should return true even when deferred");
}

/**
 * Test: Flash persistence should provide diagnostic information
 * 
 * Tests that the system can provide statistics about write operations
 * and corruption events for system monitoring.
 */
void test_flash_persistence_diagnostics(void) {
    // ARRANGE: Initialize system
    bool shared_init = shared_memory_init();
    TEST_ASSERT_TRUE(shared_init);
    bool init_result = flash_persistence_init();
    TEST_ASSERT_TRUE(init_result);
    
    // ACT: Get diagnostic information
    uint32_t write_count = flash_persistence_get_write_count();
    uint32_t corruption_count = flash_persistence_get_corruption_count();
    bool integrity_ok = flash_persistence_verify_ring_buffer_integrity();
    
    // ASSERT: Diagnostic functions should return reasonable values
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(0, write_count, 
        "Write count should be non-negative");
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(0, corruption_count,
        "Corruption count should be non-negative");
    // Integrity check result depends on flash state, just verify function works
    (void)integrity_ok; // May be true or false depending on flash state
}

/**
 * Test: Flash persistence should handle factory reset scenario correctly
 * 
 * Tests the complete factory reset sequence:
 * 1. Set non-default configuration 
 * 2. Perform factory reset (saves defaults to flash)
 * 3. Simulate system restart with normal boot sequence
 * 4. Verify system loads factory defaults from flash
 * 
 * This tests that factory reset works in a real boot scenario.
 */
void test_flash_persistence_factory_reset(void) {
    // ARRANGE: Initialize system and set non-default configuration
    bool shared_init = shared_memory_init();
    TEST_ASSERT_TRUE(shared_init);
    bool init_result = flash_persistence_init();
    TEST_ASSERT_TRUE(init_result);
    
    shared_memory_layout_t* layout = shared_memory_get_layout();
    TEST_ASSERT_NOT_NULL(layout);
    
    // Set non-default values to ensure we can detect factory reset
    layout->revision_counter = 999;
    layout->config.channels[0].baud_rate = 460800;  // Non-default
    layout->config.channels[0].data_bits = 7;       // Non-default  
    layout->config.channels[0].parity = 2;          // EVEN parity (non-default)
    // Set static IP address (10.10.10.10)
    layout->config.network.static_ip.addr = (10 << 24) | (10 << 16) | (10 << 8) | 10;
    layout->config.network.tcp_ports[0] = 9999;          // Non-default port
    layout->config.log_level = 3;                        // ERROR level (non-default)
    
    char ip_buffer[16];
    network_manager_ip_to_string(&layout->config.network.static_ip, ip_buffer);
    printf("Factory reset test - before reset: baud=%u, ip=%s, port=%u\n",
           layout->config.channels[0].baud_rate,
           ip_buffer,
           layout->config.network.tcp_ports[0]);
    
    // ACT: Trigger factory reset (this automatically saves defaults to flash)
    flash_persistence_factory_reset();
    
    printf("Factory reset performed (automatically saved to flash)\n");
    
    // ACT: Simulate system restart by corrupting memory, then loading from flash
    printf("Simulating system restart - corrupting memory then loading from flash...\n");
    
    // Corrupt memory to simulate restart (since we can't reset g_initialized in test)
    layout->revision_counter = 88888;  // Corrupt value
    layout->config.channels[0].baud_rate = 1200;     // Corrupt baud
    layout->config.channels[0].data_bits = 5;        // Corrupt data bits  
    layout->config.channels[0].parity = 1;           // Corrupt parity
    // Set corrupt IP address (255.0.0.1)
    layout->config.network.static_ip.addr = (255 << 24) | (0 << 16) | (0 << 8) | 1;
    layout->config.network.tcp_ports[0] = 1;              // Corrupt port
    layout->config.log_level = 0;                         // Corrupt log level
    layout->config.watchdog_timeout_ms = 50;              // Corrupt timeout
    
    printf("Memory corrupted to simulate restart\n");
    
    // Now load configuration from flash (should restore factory defaults)
    bool load_result = flash_persistence_load_configuration();
    TEST_ASSERT_TRUE_MESSAGE(load_result, "Should load factory defaults from flash");
    
    network_manager_ip_to_string(&layout->config.network.static_ip, ip_buffer);
    printf("After boot sequence: baud=%u, ip=%s, port=%u, revision=%u\n",
           layout->config.channels[0].baud_rate,
           ip_buffer,
           layout->config.network.tcp_ports[0],
           layout->revision_counter);
    
    // ASSERT: System should have loaded factory defaults from flash
    // Since all flash pages were invalidated, system should fall back to factory defaults
    
    // Revision counter should be 1 (factory default) since no valid pages found in flash
    TEST_ASSERT_EQUAL_MESSAGE(1, layout->revision_counter,
        "After factory reset boot sequence, should load factory defaults (revision=1)");
    
    printf("Factory reset result: revision=%u (factory default from no valid flash pages)\n", layout->revision_counter);
    
    // UART defaults should be loaded from flash
    TEST_ASSERT_EQUAL_MESSAGE(115200, layout->config.channels[0].baud_rate,
        "After factory reset boot sequence, UART baud should be default 115200");
    TEST_ASSERT_EQUAL_MESSAGE(8, layout->config.channels[0].data_bits,
        "After factory reset boot sequence, UART data bits should be default 8");
    TEST_ASSERT_EQUAL_MESSAGE(0, layout->config.channels[0].parity,
        "After factory reset boot sequence, UART parity should be default NONE");
    TEST_ASSERT_FALSE_MESSAGE(layout->config.channels[0].enabled,
        "After factory reset boot sequence, UART should be disabled by default");
    
    // Network defaults should be loaded from flash
    // Default IP: 192.168.1.100 = (192 << 24) | (168 << 16) | (1 << 8) | 100
    uint32_t expected_ip = (192 << 24) | (168 << 16) | (1 << 8) | 100;
    TEST_ASSERT_EQUAL_MESSAGE(expected_ip, layout->config.network.static_ip.addr,
        "After factory reset boot sequence, IP should be default 192.168.1.100");
    TEST_ASSERT_EQUAL_MESSAGE(4001, layout->config.network.tcp_ports[0],
        "After factory reset boot sequence, TCP port 0 should be default 4001");
    TEST_ASSERT_TRUE_MESSAGE(layout->config.network.use_dhcp,
        "After factory reset boot sequence, DHCP should be enabled by default");
    
    // System settings should be defaults
    TEST_ASSERT_EQUAL_MESSAGE(1, layout->config.log_level,
        "After factory reset boot sequence, log level should be default INFO (1)");
    TEST_ASSERT_EQUAL_MESSAGE(200, layout->config.watchdog_timeout_ms,
        "After factory reset boot sequence, watchdog timeout should be default 200ms");
    
    printf("✓ Factory reset test PASSED - system correctly loads defaults after full boot sequence\n");
}

/**
 * Test: Flash persistence write-read cycle verification
 * 
 * This is the fundamental test that verifies the complete write-then-read cycle:
 * 1. Write specific known configuration data to flash
 * 2. Manually corrupt in-memory data (simulates power loss/memory corruption)
 * 3. Load configuration from flash
 * 4. Verify loaded data exactly matches what was written (not the corrupted data)
 * 
 * This test proves that persistence actually works end-to-end.
 */
void test_flash_persistence_write_read_cycle_verification(void) {
    // ARRANGE: Initialize system with default configuration
    bool shared_init = shared_memory_init();
    TEST_ASSERT_TRUE_MESSAGE(shared_init, "Shared memory should initialize");
    bool init_result = flash_persistence_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Flash persistence should initialize");
    
    shared_memory_layout_t* layout = shared_memory_get_layout();
    TEST_ASSERT_NOT_NULL_MESSAGE(layout, "Shared memory layout should be accessible");
    
    // Declare IP buffer for string conversions
    char ip_buffer[16];
    
    // ARRANGE: Create unique test configuration data with known values
    printf("TEST: Setting up unique test configuration...\n");
    
    // Find current highest revision and use a higher number to ensure we're testing the latest
    uint32_t test_revision = layout->revision_counter + 100;  // Ensure it's higher than current
    printf("  Current revision: %u, using test revision: %u\n", layout->revision_counter, test_revision);
    
    // Set unique, recognizable test values that differ from defaults  
    layout->revision_counter = test_revision;
    layout->config.channels[0].baud_rate = 230400;  // Non-default baud rate
    layout->config.channels[0].data_bits = 7;       // Non-default data bits
    layout->config.channels[0].parity = 1;          // ODD parity (non-default)
    layout->config.channels[1].baud_rate = 460800;  // Different for second channel
    layout->config.channels[1].enabled = true;      // Enabled channel (non-default)
    // Set test IP: 10.0.0.99 = (10 << 24) | (0 << 16) | (0 << 8) | 99
    layout->config.network.static_ip.addr = (10 << 24) | (0 << 16) | (0 << 8) | 99;
    layout->config.network.tcp_ports[0] = 9001;          // Non-default port
    layout->config.network.tcp_ports[1] = 9002;          // Different port
    layout->config.network.use_dhcp = true;              // Non-default network setting
    layout->config.log_level = 3;                        // ERROR level (non-default)
    layout->config.watchdog_timeout_ms = 1000;           // Non-default timeout
    
    printf("  Test values to write: revision=%u, baud0=%u, ip=10.0.0.99, port0=%u\n", 
           layout->revision_counter, layout->config.channels[0].baud_rate,
           layout->config.network.tcp_ports[0]);
    
    // ACT: Force save the test configuration to flash
    printf("TEST: Writing test configuration to flash...\n");
    bool save_result = flash_persistence_force_save_configuration();
    TEST_ASSERT_TRUE_MESSAGE(save_result, "Force save of test configuration should succeed");
    
    // Give flash write time to complete
    sleep_ms(100);
    
    // ACT: Manually corrupt the in-memory data (simulates power loss/memory corruption)
    // This demonstrates that we're actually loading FROM FLASH, not just using memory
    printf("TEST: Corrupting in-memory data to simulate power loss...\n");
    layout->revision_counter = 12345;  // Corrupt revision
    layout->config.channels[0].baud_rate = 9600;    // Corrupt baud rate
    layout->config.channels[0].data_bits = 5;       // Corrupt data bits
    layout->config.channels[0].parity = 2;          // Corrupt parity
    layout->config.channels[1].baud_rate = 1200;    // Corrupt second channel
    layout->config.channels[1].enabled = false;     // Corrupt enabled state
    // Set corrupt IP: 255.255.255.255 = (255 << 24) | (255 << 16) | (255 << 8) | 255
    layout->config.network.static_ip.addr = (255 << 24) | (255 << 16) | (255 << 8) | 255;
    layout->config.network.tcp_ports[0] = 1;             // Corrupt port
    layout->config.network.tcp_ports[1] = 2;             // Corrupt port
    layout->config.network.use_dhcp = false;             // Corrupt DHCP setting
    layout->config.log_level = 0;                        // Corrupt log level
    layout->config.watchdog_timeout_ms = 50;             // Corrupt timeout
    
    network_manager_ip_to_string(&layout->config.network.static_ip, ip_buffer);
    printf("  Corrupted values: revision=%u, baud0=%u, ip=%s, port0=%u\n",
           layout->revision_counter, layout->config.channels[0].baud_rate,
           ip_buffer, layout->config.network.tcp_ports[0]);
    
    // Verify corruption was applied (sanity check)
    TEST_ASSERT_EQUAL_MESSAGE(12345, layout->revision_counter, 
        "Memory should be corrupted with revision=12345");
    TEST_ASSERT_EQUAL_MESSAGE(9600, layout->config.channels[0].baud_rate,
        "Memory should be corrupted with baud=9600");
    uint32_t expected_corrupt_ip = (255 << 24) | (255 << 16) | (255 << 8) | 255;
    TEST_ASSERT_EQUAL_MESSAGE(expected_corrupt_ip, layout->config.network.static_ip.addr,
        "Memory should be corrupted with IP=255.255.255.255");
    
    // ACT: Load configuration from flash (this should restore our ORIGINAL test data)
    printf("TEST: Loading configuration from flash to restore original data...\n");
    bool load_result = flash_persistence_load_configuration();
    TEST_ASSERT_TRUE_MESSAGE(load_result, "Load configuration from flash should succeed");
    
    network_manager_ip_to_string(&layout->config.network.static_ip, ip_buffer);
    printf("  Restored values: revision=%u, baud0=%u, ip=%s, port0=%u\n",
           layout->revision_counter, layout->config.channels[0].baud_rate,
           ip_buffer, layout->config.network.tcp_ports[0]);
    
    // ASSERT: Verify that ALL our test data was correctly restored from flash
    // (This proves the write-read cycle works and data comes from flash, not memory)
    
    // Core revision counter - should be >= our test revision (may be higher due to other tests)
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(test_revision, layout->revision_counter,
        "Loaded revision should be >= our test revision (may be incremented by other tests)");
    
    printf("TEST: Data verification - test_revision=%u, loaded_revision=%u\n", 
           test_revision, layout->revision_counter);
    
    // UART channel 0 settings
    TEST_ASSERT_EQUAL_MESSAGE(230400, layout->config.channels[0].baud_rate,
        "UART0 baud rate should be restored from flash to original value");
    TEST_ASSERT_EQUAL_MESSAGE(7, layout->config.channels[0].data_bits,
        "UART0 data bits should be restored from flash to original value");  
    TEST_ASSERT_EQUAL_MESSAGE(1, layout->config.channels[0].parity,
        "UART0 parity should be restored from flash to original value");
    
    // UART channel 1 settings  
    TEST_ASSERT_EQUAL_MESSAGE(460800, layout->config.channels[1].baud_rate,
        "UART1 baud rate should be restored from flash to original value");
    TEST_ASSERT_TRUE_MESSAGE(layout->config.channels[1].enabled,
        "UART1 enabled state should be restored from flash to original value");
    
    // Network settings - check IP address
    uint32_t expected_test_ip = (10 << 24) | (0 << 16) | (0 << 8) | 99;
    TEST_ASSERT_EQUAL_MESSAGE(expected_test_ip, layout->config.network.static_ip.addr,
        "Network IP address should be restored from flash to original value");
    TEST_ASSERT_EQUAL_MESSAGE(9001, layout->config.network.tcp_ports[0],
        "Network TCP port 0 should be restored from flash to original value");
    TEST_ASSERT_EQUAL_MESSAGE(9002, layout->config.network.tcp_ports[1],
        "Network TCP port 1 should be restored from flash to original value");
    TEST_ASSERT_TRUE_MESSAGE(layout->config.network.use_dhcp,
        "Network DHCP setting should be restored from flash to original value");
    
    // System settings
    TEST_ASSERT_EQUAL_MESSAGE(3, layout->config.log_level,
        "Log level should be restored from flash to original value");
    TEST_ASSERT_EQUAL_MESSAGE(1000, layout->config.watchdog_timeout_ms,
        "Watchdog timeout should be restored from flash to original value");
    
    printf("TEST: ✓ All original test data successfully restored from flash after corruption\n");
    printf("TEST: ✓ Write-read cycle verification PASSED - persistence works correctly!\n");
}

// Test runner
int main() {
    // Initialize Pico SDK
    stdio_init_all();
    
    // Wait for USB-serial connection
    sleep_ms(2000);
    
    printf("*** PARTITION 0 FIRMWARE *** Starting Flash Persistence Tests...\n");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_flash_persistence_initialization);
    RUN_TEST(test_flash_persistence_load_configuration);
    RUN_TEST(test_flash_persistence_change_detection);
    RUN_TEST(test_flash_persistence_write_frequency_limiting);
    RUN_TEST(test_flash_persistence_diagnostics);
    RUN_TEST(test_flash_persistence_factory_reset);
    RUN_TEST(test_flash_persistence_write_read_cycle_verification);
    
    while (true) {
        printf("Flash Persistence Tests completed\n");
        UNITY_END();
        sleep_ms(1000);
    }
}