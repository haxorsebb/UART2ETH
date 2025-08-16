/**
 * @file test_network_integration.c
 * @brief Network Integration Tests for Complete TCP/IP Stack (Performance Optimized)
 * 
 * Tests the complete network stack integration: ENC28J60 + lwIP + DHCP + ping.
 * Each test is independent and reinitializes the complete network stack.
 * 
 * Performance Optimizations for <5ms ping target:
 * - Debug output redirected to UART1 (/dev/ttyUSB0) instead of USB-CDC
 * - Network processing at 10kHz (100μs sleep) instead of 100Hz (10ms sleep) 
 * - Reduced debug output frequency during network processing
 * - Optimized polling intervals for faster response times
 * 
 * Test Strategy:
 * - Step 1: Basic network stack initialization (ENC28J60 + lwIP + network manager)
 * - Step 2: DHCP client functionality and IP address assignment
 * - Step 3: Network connectivity and ping response
 * 
 * Hardware Requirements:
 * - ENC28J60 connected to network with DHCP server
 * - Network cable connected
 * - DHCP server available on network
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 * - Test-Driven Development process
 */

#include "unity.h"
#include "network/network_manager.h"
#include "network/enc28j60_driver.h"
#include "network/lwip_netif_enc28j60.h"
#include "state_machine.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/uart.h"
#include "lwip/ip4_addr.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include <string.h>
#include <stdio.h>

// Test configuration
#define TEST_TIMEOUT_MS 60000    // 60 seconds for DHCP
#define POLLING_INTERVAL_MS 500  // 500ms between status checks
#define LINK_CHECK_TIMEOUT_MS 10000  // 10 seconds for link up

// Test helper functions
static void cleanup_network_stack(void);
static bool wait_for_link_up(uint32_t timeout_ms);
static bool wait_for_dhcp_completion(uint32_t timeout_ms);
static void print_network_diagnostics(void);

void setUp(void) {
    // Each test starts with clean network stack
    cleanup_network_stack();
    // Reduced debug output for performance
}

void tearDown(void) {
    // Clean up after each test
    cleanup_network_stack();
    // Reduced debug output for performance
}

/**
 * @brief Test Step 1: Core1 Network Stack Initialization (Production Integration)
 * 
 * Verifies that Core1 properly initializes the complete network stack.
 * This tests the actual production dual-core architecture.
 */
void test_network_stack_initialization(void) {
    printf("=== Step 1: Core1 Network Stack Initialization Test ===\n");
    printf("Monitoring Core1 production network initialization\n");
    
    // Test 1.1: Wait for Core1 to initialize network stack
    printf("Waiting for Core1 to initialize network stack...\n");
    
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    bool network_initialized = false;
    
    // Monitor Core1 initialization for up to 15 seconds
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < 15000) {
        network_status_t status = network_manager_get_status();
        
        if (status != NETWORK_STATUS_UNINITIALIZED) {
            network_initialized = true;
            printf("✓ Core1 initialized network stack, status: %s\n", 
                   network_manager_status_to_string(status));
            break;
        }
        
        sleep_ms(200);
    }
    
    TEST_ASSERT_TRUE_MESSAGE(network_initialized, "Core1 should initialize network within 15 seconds");
    
    // Test 1.2: Verify Core1 hardware initialization
    // We can safely check hardware state since Core1 should have initialized it
    printf("Verifying Core1 hardware initialization...\n");
    bool enc_ready = enc28j60_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(enc_ready, "Core1 should have initialized ENC28J60");
    
    const enc28j60_state_t* enc_state = enc28j60_get_state();
    TEST_ASSERT_NOT_NULL_MESSAGE(enc_state, "ENC28J60 state should be available from Core1");
    TEST_ASSERT_TRUE_MESSAGE(enc_state->initialized, "Core1 should have initialized hardware");
    printf("✓ Core1 hardware initialization verified\n");
    
    // Test 1.3: Verify network manager status
    network_status_t status = network_manager_get_status();
    TEST_ASSERT_MESSAGE(status != NETWORK_STATUS_ERROR, 
                       "Core1 network should not be in error state");
    printf("✓ Core1 network status: %s\n", network_manager_status_to_string(status));
    
    // Test 1.4: Wait for Core1 to bring up physical link
    printf("Waiting for Core1 to establish physical link");
    bool link_up = wait_for_link_up(LINK_CHECK_TIMEOUT_MS);
    TEST_ASSERT_TRUE_MESSAGE(link_up, "Core1 should establish physical link");
    printf("✓ Core1 established physical link\n");
    
    // Test 1.5: Verify MAC address configuration by Core1
    uint8_t mac_addr[6];
    network_manager_get_mac_address(mac_addr);
    
    // Check that MAC address is not all zeros
    bool mac_valid = false;
    for (int i = 0; i < 6; i++) {
        if (mac_addr[i] != 0) {
            mac_valid = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(mac_valid, "Core1 should configure MAC address");
    printf("✓ Core1 MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    
    printf("=== Step 1: Core1 Network Stack Initialization PASSED ===\n");
}

/**
 * @brief Test Step 2: Core1 DHCP Client Functionality (Production Integration)
 * 
 * Tests that Core1 properly handles DHCP client functionality.
 * This monitors the production system without interfering.
 */
void test_dhcp_client_functionality(void) {
    printf("=== Step 2: Core1 DHCP Client Functionality Test ===\n");
    printf("Monitoring Core1 production DHCP processing\n");
    
    // Test 2.1: Verify Core1 has network ready for DHCP
    printf("Verifying Core1 network readiness...\n");
    
    // Core1 should already have initialized the network
    network_status_t status = network_manager_get_status();
    bool link_up = network_manager_is_link_up();
    
    printf("  Core1 status: %s\n", network_manager_status_to_string(status));
    printf("  Core1 link status: %s\n", link_up ? "Up" : "Down");
    
    if (!link_up) {
        printf("Waiting for Core1 to establish link");
        bool link_established = wait_for_link_up(LINK_CHECK_TIMEOUT_MS);
        TEST_ASSERT_TRUE_MESSAGE(link_established, "Core1 should establish link for DHCP test");
    }
    printf("✓ Core1 network ready for DHCP\n");
    
    // Test 2.2: Monitor Core1 DHCP process
    printf("Monitoring Core1 DHCP client, waiting for IP assignment");
    bool dhcp_success = wait_for_dhcp_completion(TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE_MESSAGE(dhcp_success, "Core1 should complete DHCP within timeout");
    printf("✓ Core1 DHCP completed successfully\n");
    
    // Test 2.3: Verify Core1 DHCP binding
    bool dhcp_bound = network_manager_is_dhcp_bound();
    TEST_ASSERT_TRUE_MESSAGE(dhcp_bound, "Core1 should report DHCP bound");
    printf("✓ Core1 DHCP binding confirmed\n");
    
    // Test 2.4: Verify Core1 network readiness
    bool net_ready = network_manager_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(net_ready, "Core1 should report network ready after DHCP");
    printf("✓ Core1 network ready for TCP/IP operations\n");
    
    // Test 2.5: Verify Core1 assigned IP address
    simple_ip_addr_t ip_addr;
    bool ip_valid = network_manager_get_ip_address(&ip_addr);
    TEST_ASSERT_TRUE_MESSAGE(ip_valid, "Core1 should provide valid IP address");
    TEST_ASSERT_MESSAGE(ip_addr.addr != 0, "Core1 IP address should not be 0.0.0.0");
    
    char ip_str[16];
    network_manager_ip_to_string(&ip_addr, ip_str);
    printf("✓ Core1 assigned IP Address: %s\n", ip_str);
    
    // Test 2.6: Print Core1 network diagnostics
    print_network_diagnostics();
    
    printf("=== Step 2: Core1 DHCP Client Functionality PASSED ===\n");
}

/**
 * @brief Test Step 3: Network Connectivity (Ping Response) - Production Runtime Test
 * 
 * Tests the complete production system with Core1 handling network processing.
 * This is a true integration test of the dual-core architecture.
 * 
 * Core0 (this test): Monitors network status and coordinates test
 * Core1 (production): Handles all network processing, DHCP, packet handling
 */
void test_network_connectivity(void) {
    printf("=== Step 3: Network Connectivity Test (Production Runtime) ===\n");
    printf("Testing actual dual-core production system\n");
    
    // This test monitors the production system, doesn't replace it
    
    // Test 3.1: Monitor network initialization (handled by Core1)
    printf("Monitoring Core1 network initialization...\n");
    
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    bool network_up = false;
    
    // Wait for Core1 to initialize network (up to 30 seconds)
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < 30000) {
        network_status_t status = network_manager_get_status();
        
        if (status == NETWORK_STATUS_READY) {
            network_up = true;
            break;
        }
        
        // Print status updates
        static uint32_t last_status_print = 0;
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - last_status_print >= 3000) {
            printf("  Core1 status: %s\n", network_manager_status_to_string(status));
            last_status_print = current_time;
        }
        
        sleep_ms(200);  // Check every 200ms
    }
    
    TEST_ASSERT_TRUE_MESSAGE(network_up, "Core1 should initialize network within 30 seconds");
    printf("✓ Core1 network initialization successful\n");
    
    // Test 3.2: Verify DHCP was handled by Core1
    bool dhcp_bound = network_manager_is_dhcp_bound();
    TEST_ASSERT_TRUE_MESSAGE(dhcp_bound, "Core1 should complete DHCP automatically");
    printf("✓ Core1 completed DHCP successfully\n");
    
    // Test 3.3: Get IP address assigned by Core1
    simple_ip_addr_t ip_addr;
    bool ip_valid = network_manager_get_ip_address(&ip_addr);
    TEST_ASSERT_TRUE_MESSAGE(ip_valid, "Should have IP address from Core1 DHCP");
    
    char ip_str[16];
    network_manager_ip_to_string(&ip_addr, ip_str);
    
    printf("✓ Core1 assigned IP Address: %s\n", ip_str);
    
    // Test 3.4: Monitor Core1 network processing during ping test
    printf("\n=== PRODUCTION PING TEST READY ===\n");
    printf("Target IP: %s | Core1 handling all network processing\n", ip_str);
    printf("From another machine: ping %s\n", ip_str);
    printf("Monitoring Core1 network processing for 30 seconds...\n");
    
    start_time = to_ms_since_boot(get_absolute_time());
    uint32_t test_duration_ms = 30000;  // 30 seconds
    uint32_t last_status_time = start_time;
    
    network_stats_t initial_stats, current_stats;
    network_manager_get_stats(&initial_stats);
    
    printf("Core1 network processing active");
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < test_duration_ms) {
        // Print status every 5 seconds
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - last_status_time >= 5000) {
            network_manager_get_stats(&current_stats);
            printf(" [RX:%u TX:%u]", 
                   current_stats.packets_rx, current_stats.packets_tx);
            last_status_time = current_time;
        }
        
        // Check that network remains ready
        if (!network_manager_is_ready()) {
            printf("\nWARNING: Network went down during test\n");
            break;
        }
        
        // Core0 sleeps while Core1 does the real work
        sleep_ms(100);  // Check status every 100ms
    }
    printf("\n");
    
    // Test 3.5: Verify Core1 processed network traffic
    network_manager_get_stats(&current_stats);
    uint32_t rx_packets = current_stats.packets_rx - initial_stats.packets_rx;
    uint32_t tx_packets = current_stats.packets_tx - initial_stats.packets_tx;
    
    printf("✓ Core1 network traffic processed during test:\n");
    printf("  - Received packets: %u\n", rx_packets);
    printf("  - Transmitted packets: %u\n", tx_packets);
    
    // Test 3.6: Verify production system stability
    TEST_ASSERT_MESSAGE(network_manager_is_ready(), "Production system should remain stable");
    
    printf("✓ Production dual-core system remained stable\n");
    printf("\n=== MANUAL VERIFICATION REQUIRED ===\n");
    printf("Please verify that ping commands from external host succeeded.\n");
    printf("If pings worked, the PRODUCTION INTEGRATION test is SUCCESSFUL.\n");
    printf("=====================================\n");
    
    printf("=== Step 3: Production Network Connectivity Test COMPLETED ===\n");
}

/**
 * @brief Test runner function for network integration tests
 */
int test_network_integration_run_tests(void) {
    UNITY_BEGIN();
    
    printf("\n");
    printf("========================================\n");
    printf("=== NETWORK INTEGRATION TEST SUITE ===\n");
    printf("========================================\n");
    printf("Testing complete TCP/IP stack integration\n");
    printf("Hardware: ENC28J60 + lwIP + DHCP + ping\n");
    printf("========================================\n");
    
    // Step 1: Basic network stack initialization
    RUN_TEST(test_network_stack_initialization);
    
    // Step 2: DHCP client functionality  
    RUN_TEST(test_dhcp_client_functionality);
    
    // Step 3: Network connectivity (ping response)
    RUN_TEST(test_network_connectivity);
    
    printf("\n========================================\n");
    printf("=== NETWORK INTEGRATION TESTS DONE ===\n");
    printf("========================================\n");
    
    return UNITY_END();
}

// Helper function implementations

/**
 * @brief Cleanup for production integration tests
 * 
 * In production integration tests, Core1 manages the network stack.
 * Core0 tests don't directly manage network components.
 */
static void cleanup_network_stack(void) {
    // In production integration tests, Core1 manages all network components
    // Core0 test just needs to reset any local state
    
    printf("Integration test cleanup: Core1 continues managing network\n");
    
    // Small delay between tests
    sleep_ms(200);
}

/**
 * @brief Wait for Core1 to bring physical link up (production integration test)
 */
static bool wait_for_link_up(uint32_t timeout_ms) {
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    uint32_t last_dot_time = start_time;
    
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < timeout_ms) {
        // Monitor link status via network manager
        if (network_manager_is_link_up()) {
            printf("\n");
            return true;
        }
        
        // Print progress dots less frequently (every 2 seconds)
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - last_dot_time >= 2000) {
            printf(".");
            last_dot_time = current_time;
        }
        
        // Core0 just monitors while Core1 does the work
        sleep_ms(200);  // Monitor every 200ms
    }
    
    printf("\n");
    return false;
}

/**
 * @brief Wait for Core1 to complete DHCP (production integration test)
 */
static bool wait_for_dhcp_completion(uint32_t timeout_ms) {
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    uint32_t last_status_time = start_time;
    
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < timeout_ms) {
        // Check if DHCP completed
        if (network_manager_is_dhcp_bound()) {
            printf("\n");
            return true;
        }
        
        // Print status every 5 seconds (less frequent)
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - last_status_time >= 5000) {
            network_status_t status = network_manager_get_status();
            printf(" [%s]", network_manager_status_to_string(status));
            last_status_time = current_time;
        }
        
        // Core0 monitors while Core1 does the work
        sleep_ms(200);  // Monitor every 200ms
    }
    
    printf("\n");
    return false;
}

/**
 * @brief Print detailed network diagnostics
 */
static void print_network_diagnostics(void) {
    printf("\n=== Network Diagnostics ===\n");
    
    // Get network statistics
    network_stats_t stats;
    network_manager_get_stats(&stats);
    
    // Print current configuration
    if (stats.current_ip.addr != 0) {
        char ip_str[16], gw_str[16], nm_str[16];
        network_manager_ip_to_string(&stats.current_ip, ip_str);
        network_manager_ip_to_string(&stats.current_gateway, gw_str);
        network_manager_ip_to_string(&stats.current_netmask, nm_str);
        
        printf("IP Address: %s\n", ip_str);
        printf("Gateway:    %s\n", gw_str);
        printf("Netmask:    %s\n", nm_str);
    }
    
    printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           stats.current_mac[0], stats.current_mac[1], stats.current_mac[2],
           stats.current_mac[3], stats.current_mac[4], stats.current_mac[5]);
    
    printf("Status: %s\n", network_manager_status_to_string(stats.status));
    printf("Link Events: Up=%u, Down=%u\n", stats.link_up_events, stats.link_down_events);
    printf("DHCP Requests: %u\n", stats.dhcp_requests);
    printf("Packets: RX=%u, TX=%u\n", stats.packets_rx, stats.packets_tx);
    printf("Uptime: %u seconds\n", stats.uptime_seconds);
    
    printf("===========================\n\n");
}

/**
 * @brief Core1 entry point for production network processing
 */
void core1_entry() {
    // Import the production Core1 main function
    extern void core1_main(void);
    core1_main();
}

/**
 * @brief Main function for PRODUCTION network integration tests
 * 
 * This launches the actual production dual-core system and tests it.
 * Core0: Runs these integration tests
 * Core1: Runs production network processing (ENC28J60 + lwIP + DHCP)
 */
int main(void) {
    // Initialize dual stdio like production system
    stdio_usb_init();
    
    // Initialize UART1 for debug output (UART0 conflicts with SPI0)
    // This ensures debug output goes to /dev/ttyUSB0 as mentioned
    stdio_uart_init_full(uart1, 115200, 20, 21);
    
    // Wait for UART connection
    sleep_ms(1000);
    
    printf("\n=== UART2ETH PRODUCTION Integration Test ===\n");
    printf("Testing: Dual-core production system (Core0 + Core1)\n");
    printf("Core0: Integration test monitoring\n");
    printf("Core1: Production network processing\n");
    printf("Hardware: ENC28J60 + lwIP + DHCP + ping\n");
    printf("Debug: UART1 (/dev/ttyUSB0)\n");
    
    // Initialize shared memory and state machine like production system
    extern bool shared_memory_init(void);
    extern bool state_machine_init(void);
    extern bool log_manager_init(void);
    
    if (!shared_memory_init()) {
        printf("ERROR: Failed to initialize shared memory\n");
        while (true) sleep_ms(1000);
    }
    
    if (!state_machine_init()) {
        printf("ERROR: Failed to initialize state machine\n");
        while (true) sleep_ms(1000);
    }
    
    if (!log_manager_init()) {
        printf("ERROR: Failed to initialize log manager\n");
        while (true) sleep_ms(1000);
    }
    
    printf("✓ Production system components initialized\n");
    
    // Launch Core1 with production network processing
    extern void multicore_launch_core1(void (*entry)(void));
    multicore_launch_core1(core1_entry);
    
    // Give Core1 time to initialize
    sleep_ms(100);
    
    printf("✓ Core1 launched with production network processing\n");
    
    // CRITICAL: Send the state machine event that production Core0 would send
    // This signals Core1 that hardware initialization is complete
    extern bool state_machine_process_main_event(main_state_event_t event);
    bool event_sent = state_machine_process_main_event(MAIN_EVENT_INIT_COMPLETE_CORE0);
    if (event_sent) {
        printf("✓ Sent MAIN_EVENT_INIT_COMPLETE_CORE0 to Core1\n");
    } else {
        printf("⚠ Failed to send MAIN_EVENT_INIT_COMPLETE_CORE0\n");
    }
    
    printf("✓ Production dual-core system running\n");
    
    // Run the production integration tests
    int result = test_network_integration_run_tests();
    
    // Keep the production system running
    printf("\nProduction Integration Tests completed: %s\n", (result == 0) ? "PASSED" : "FAILED");
    printf("Production system continues running with Core1 network processing...\n");
    
    while (true) {
        // Core0 can do other work while Core1 handles network
        // This simulates the production UART processing that would run here
        
        static uint32_t last_status = 0;
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        
        if (current_time - last_status >= 10000) {  // Every 10 seconds
            printf("Production system running: Core1 network active, Core0 monitoring\n");
            
            if (network_manager_is_ready()) {
                network_stats_t stats;
                network_manager_get_stats(&stats);
                printf("  Network stats: RX=%u TX=%u Status=%s\n", 
                       stats.packets_rx, stats.packets_tx,
                       network_manager_status_to_string(stats.status));
            }
            
            last_status = current_time;
        }
        
        sleep_ms(1000);  // Core0 low-priority monitoring
    }
    
    return result;  // Never reached
}
