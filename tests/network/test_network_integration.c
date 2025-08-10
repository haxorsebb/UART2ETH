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
#include "pico/stdlib.h"
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
 * @brief Test Step 1: Basic Network Stack Initialization
 * 
 * Verifies that the complete network stack (ENC28J60 + lwIP + network manager)
 * initializes properly and hardware communication works.
 */
void test_network_stack_initialization(void) {
    printf("=== Step 1: Network Stack Initialization Test ===\n");
    
    // Test 1.1: ENC28J60 driver initialization
    printf("Testing ENC28J60 driver initialization...\n");
    bool enc_init = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(enc_init, "ENC28J60 driver should initialize successfully");
    
    bool enc_ready = enc28j60_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(enc_ready, "ENC28J60 should be ready after initialization");
    printf("✓ ENC28J60 driver initialized and ready\n");
    
    // Test 1.2: Check hardware communication
    const enc28j60_state_t* enc_state = enc28j60_get_state();
    TEST_ASSERT_NOT_NULL_MESSAGE(enc_state, "ENC28J60 state should be available");
    TEST_ASSERT_TRUE_MESSAGE(enc_state->initialized, "ENC28J60 state should show initialized");
    printf("✓ ENC28J60 hardware communication verified\n");
    
    // Test 1.3: Network manager initialization
    printf("Testing network manager initialization...\n");
    network_config_t config;
    network_manager_get_default_config(&config);
    
    bool net_init = network_manager_init(&config);
    TEST_ASSERT_TRUE_MESSAGE(net_init, "Network manager should initialize successfully");
    printf("✓ Network manager initialized\n");
    
    // Test 1.4: Check network manager status
    network_status_t status = network_manager_get_status();
    TEST_ASSERT_MESSAGE(status != NETWORK_STATUS_UNINITIALIZED, 
                       "Network should not be in uninitialized state");
    TEST_ASSERT_MESSAGE(status != NETWORK_STATUS_ERROR, 
                       "Network should not be in error state");
    
    printf("✓ Network manager status: %s\n", network_manager_status_to_string(status));
    
    // Test 1.5: Wait for physical link to come up
    printf("Waiting for physical link up");
    bool link_up = wait_for_link_up(LINK_CHECK_TIMEOUT_MS);
    TEST_ASSERT_TRUE_MESSAGE(link_up, "Physical link should come up within timeout");
    printf("✓ Physical link is up\n");
    
    // Test 1.6: Verify MAC address configuration
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
    TEST_ASSERT_TRUE_MESSAGE(mac_valid, "MAC address should be configured");
    printf("✓ MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    
    printf("=== Step 1: Network Stack Initialization PASSED ===\n");
}

/**
 * @brief Test Step 2: DHCP Client Functionality
 * 
 * Tests DHCP client functionality and verifies IP address assignment.
 * This test requires a DHCP server on the network.
 */
void test_dhcp_client_functionality(void) {
    printf("=== Step 2: DHCP Client Functionality Test ===\n");
    
    // Initialize complete network stack
    printf("Initializing network stack for DHCP test...\n");
    
    // ENC28J60 initialization
    bool enc_init = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(enc_init, "ENC28J60 initialization required for DHCP test");
    
    // Network manager initialization with DHCP enabled
    network_config_t config;
    network_manager_get_default_config(&config);
    config.use_dhcp = true;
    config.dhcp_timeout_ms = TEST_TIMEOUT_MS;
    
    bool net_init = network_manager_init(&config);
    TEST_ASSERT_TRUE_MESSAGE(net_init, "Network manager initialization required for DHCP test");
    
    // Wait for link up first
    printf("Waiting for physical link");
    bool link_up = wait_for_link_up(LINK_CHECK_TIMEOUT_MS);
    TEST_ASSERT_TRUE_MESSAGE(link_up, "Physical link required for DHCP test");
    printf("✓ Physical link established\n");
    
    // Test 2.1: Wait for DHCP to complete
    printf("Starting DHCP client, waiting for IP address assignment");
    bool dhcp_success = wait_for_dhcp_completion(TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE_MESSAGE(dhcp_success, "DHCP should assign IP address within timeout");
    printf("✓ DHCP completed successfully\n");
    
    // Test 2.2: Verify DHCP binding
    bool dhcp_bound = network_manager_is_dhcp_bound();
    TEST_ASSERT_TRUE_MESSAGE(dhcp_bound, "Network manager should report DHCP bound");
    printf("✓ DHCP binding confirmed\n");
    
    // Test 2.3: Verify network is ready
    bool net_ready = network_manager_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(net_ready, "Network should be ready after DHCP success");
    printf("✓ Network ready for TCP/IP operations\n");
    
    // Test 2.4: Get and validate IP address
    simple_ip_addr_t ip_addr;
    bool ip_valid = network_manager_get_ip_address(&ip_addr);
    TEST_ASSERT_TRUE_MESSAGE(ip_valid, "Should be able to get IP address after DHCP");
    TEST_ASSERT_MESSAGE(ip_addr.addr != 0, "IP address should not be 0.0.0.0");
    
    char ip_str[16];
    network_manager_ip_to_string(&ip_addr, ip_str);
    printf("✓ Assigned IP Address: %s\n", ip_str);
    
    // Test 2.5: Print detailed network configuration
    print_network_diagnostics();
    
    printf("=== Step 2: DHCP Client Functionality PASSED ===\n");
}

/**
 * @brief Test Step 3: Network Connectivity (Ping Response)
 * 
 * Tests that the target can respond to ping requests after getting IP address.
 * This test relies on external ping from host to verify connectivity.
 */
void test_network_connectivity(void) {
    printf("=== Step 3: Network Connectivity Test ===\n");
    
    // Initialize complete network stack
    printf("Initializing network stack for connectivity test...\n");
    
    // ENC28J60 initialization
    bool enc_init = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(enc_init, "ENC28J60 initialization required for connectivity test");
    
    // Network manager initialization with DHCP
    network_config_t config;
    network_manager_get_default_config(&config);
    config.use_dhcp = true;
    config.dhcp_timeout_ms = TEST_TIMEOUT_MS;
    
    bool net_init = network_manager_init(&config);
    TEST_ASSERT_TRUE_MESSAGE(net_init, "Network manager initialization required for connectivity test");
    
    // Wait for link and DHCP
    printf("Establishing network connection");
    bool link_up = wait_for_link_up(LINK_CHECK_TIMEOUT_MS);
    TEST_ASSERT_TRUE_MESSAGE(link_up, "Physical link required for connectivity test");
    
    bool dhcp_success = wait_for_dhcp_completion(TEST_TIMEOUT_MS);
    TEST_ASSERT_TRUE_MESSAGE(dhcp_success, "DHCP required for connectivity test");
    printf("✓ Network connection established\n");
    
    // Test 3.1: Verify network is ready for connectivity
    bool net_ready = network_manager_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(net_ready, "Network should be ready for connectivity test");
    
    // Test 3.2: Get IP address for ping instructions
    simple_ip_addr_t ip_addr;
    bool ip_valid = network_manager_get_ip_address(&ip_addr);
    TEST_ASSERT_TRUE_MESSAGE(ip_valid, "Should have valid IP address for connectivity test");
    
    char ip_str[16];
    network_manager_ip_to_string(&ip_addr, ip_str);
    
    printf("✓ Target IP Address: %s\n", ip_str);
    printf("✓ Network stack ready for ping response\n");
    
    // Test 3.3: High-performance network processing loop for ping response
    printf("\n=== PING TEST READY ===\n");
    printf("Target IP: %s | Ready for ping requests\n", ip_str);
    printf("From another machine: ping %s\n", ip_str);
    printf("Processing network traffic for 30 seconds (high-performance mode)...\n");
    
    // Process network traffic for 30 seconds to handle ping requests
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    uint32_t test_duration_ms = 30000;  // 30 seconds
    uint32_t last_status_time = start_time;
    uint32_t loop_count = 0;
    
    network_stats_t initial_stats, current_stats;
    network_manager_get_stats(&initial_stats);
    
    printf("Network processing active");
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < test_duration_ms) {
        // PERFORMANCE CRITICAL: Process network stack with minimal delay
        network_manager_process();
        
        // Process lwIP timeouts (less frequently)
        if (++loop_count % 1000 == 0) {
            sys_check_timeouts();
        }
        
        // Print status every 5 seconds (much less frequently)
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - last_status_time >= 5000) {
            network_manager_get_stats(&current_stats);
            printf(" [RX:%u TX:%u]", 
                   current_stats.packets_rx, current_stats.packets_tx);
            last_status_time = current_time;
        }
        
        // CRITICAL: Minimal delay for maximum responsiveness (<5ms ping target)
        sleep_us(100);  // Process at 10kHz instead of 100Hz for sub-5ms ping
    }
    printf("\n");
    
    // Test 3.4: Check if any network traffic was processed
    network_manager_get_stats(&current_stats);
    uint32_t rx_packets = current_stats.packets_rx - initial_stats.packets_rx;
    uint32_t tx_packets = current_stats.packets_tx - initial_stats.packets_tx;
    
    printf("✓ Network traffic processed during test:\n");
    printf("  - Received packets: %u\n", rx_packets);
    printf("  - Transmitted packets: %u\n", tx_packets);
    
    // Note: We can't automatically verify ping success without external coordination
    // The test passes if the network stack processes without errors
    TEST_ASSERT_MESSAGE(network_manager_is_ready(), "Network should remain ready throughout test");
    
    printf("✓ Network stack remained stable during connectivity test\n");
    printf("\n=== MANUAL VERIFICATION REQUIRED ===\n");
    printf("Please verify that ping commands from external host succeeded.\n");
    printf("If pings worked, the connectivity test is SUCCESSFUL.\n");
    printf("=====================================\n");
    
    printf("=== Step 3: Network Connectivity Test COMPLETED ===\n");
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
 * @brief Clean up network stack (deinitialize all components)
 */
static void cleanup_network_stack(void) {
    // Deinitialize in reverse order
    network_manager_deinit();
    lwip_netif_enc28j60_deinit();
    enc28j60_deinit();
    
    // Small delay to ensure cleanup completes
    sleep_ms(100);
}

/**
 * @brief Wait for physical link to come up with minimal debug output
 */
static bool wait_for_link_up(uint32_t timeout_ms) {
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    uint32_t last_dot_time = start_time;
    
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < timeout_ms) {
        // Process network manager to update link status
        network_manager_process();
        
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
        
        // Faster polling for quicker link detection
        sleep_ms(100);  // 100ms instead of 500ms
    }
    
    printf("\n");
    return false;
}

/**
 * @brief Wait for DHCP completion with minimal debug output
 */
static bool wait_for_dhcp_completion(uint32_t timeout_ms) {
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    uint32_t last_status_time = start_time;
    
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < timeout_ms) {
        // Process network manager and lwIP timers
        network_manager_process();
        sys_check_timeouts();
        
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
        
        // Reduced polling interval for faster response
        sleep_ms(100);  // 100ms instead of 500ms
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
 * @brief Main function for network integration tests
 */
int main(void) {
    stdio_usb_init();
    // Initialize UART1 for debug output (UART0 conflicts with SPI0)
    // This ensures debug output goes to /dev/ttyUSB0 as mentioned
    stdio_uart_init_full(uart1, 115200, 20, 21);
    
    // Wait for UART connection
    sleep_ms(1000);
    
    printf("\n=== UART2ETH Network Integration Test (Performance Optimized) ===\n");
    printf("Hardware: ENC28J60 + lwIP + DHCP + ping | Debug: UART1 (/dev/ttyUSB0)\n");
    
    // Run the integration tests
    int result = test_network_integration_run_tests();
    
    // Keep running forever for embedded system with optimized network processing
    printf("\nNetwork Integration Tests completed: %s\n", (result == 0) ? "PASSED" : "FAILED");
    printf("Continuing high-performance network processing...\n");
    
    while (true) {
        // Continue processing network if initialized
        if (network_manager_is_ready()) {
            // High-performance continuous network processing
            for (int i = 0; i < 50000; i++) {  // 5 seconds at 10kHz
                network_manager_process();
                sleep_us(100);
            }
            // Brief status update every 5 seconds
            printf("Network active - continuous processing\n");
        } else {
            sleep_ms(1000);
        }
    }
    
    return result;  // Never reached
}
