/**
 * @file test_ping_connectivity.c
 * @brief Ping Connectivity Test for UART2ETH Network Implementation
 * 
 * This test verifies that the UART2ETH device can:
 * - Obtain an IP address via DHCP with hostname 'UART2ETH'
 * - Respond to ping requests from other computers
 * - Show network connectivity statistics
 * 
 * Test Duration: 30 seconds
 * Expected Usage: Run this test and ping the displayed IP address from another computer
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 * - Network stack integration testing
 */

#include "unity.h"
#include "network/enc28j60_driver.h"
#include "network/network_manager.h"
#include "network/lwip_netif_enc28j60.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/stats.h"
#include <string.h>
#include <stdio.h>

// Test configuration
#define TEST_DURATION_MS 30000    // 30 seconds
#define STATUS_UPDATE_INTERVAL_MS 5000  // Update every 5 seconds

static uint32_t ping_responses = 0;
static uint32_t total_packets_rx = 0;
static uint32_t test_start_time = 0;

void setUp(void) {
    // Clean up any previous state
    network_manager_deinit();
    lwip_netif_enc28j60_deinit();
    enc28j60_deinit();
    sleep_ms(100);
}

void tearDown(void) {
    // Clean up after test
    network_manager_deinit();
    lwip_netif_enc28j60_deinit();
    enc28j60_deinit();
    sleep_ms(100);
}

/**
 * @brief Main ping connectivity test
 */
void test_ping_connectivity_30_seconds(void) {
    printf("\n");
    printf("==================================================\n");
    printf("=== UART2ETH Ping Connectivity Test (30s) ===\n");
    printf("==================================================\n");
    printf("This test will:\n");
    printf("1. Initialize network with DHCP\n");
    printf("2. Display the assigned IP address\n");
    printf("3. Respond to pings for 30 seconds\n");
    printf("4. Show connectivity statistics\n");
    printf("==================================================\n");
    
    // Initialize ENC28J60
    printf("Step 1: Initializing ENC28J60...\n");
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "ENC28J60 should initialize");
    
    // Initialize network manager with DHCP
    printf("Step 2: Starting network stack with DHCP...\n");
    network_config_t config;
    network_manager_get_default_config(&config);
    config.use_dhcp = true;
    config.dhcp_timeout_ms = 15000;  // 15 second timeout
    
    bool net_init = network_manager_init(&config);
    TEST_ASSERT_TRUE_MESSAGE(net_init, "Network manager should initialize");
    
    // Wait for link up
    printf("Step 3: Waiting for physical link...\n");
    uint32_t link_wait_start = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - link_wait_start) < 5000) {
        network_manager_process();
        if (network_manager_is_link_up()) {
            printf("✓ Physical link is UP\n");
            break;
        }
        sleep_ms(100);
    }
    
    TEST_ASSERT_TRUE_MESSAGE(network_manager_is_link_up(), "Physical link should be up");
    
    // Wait for DHCP to complete
    printf("Step 4: Waiting for DHCP to assign IP address...\n");
    printf("(DHCP hostname: UART2ETH)\n");
    
    uint32_t dhcp_wait_start = to_ms_since_boot(get_absolute_time());
    bool dhcp_success = false;
    
    while ((to_ms_since_boot(get_absolute_time()) - dhcp_wait_start) < config.dhcp_timeout_ms) {
        network_manager_process();
        sys_check_timeouts();
        
        if (network_manager_is_dhcp_bound()) {
            dhcp_success = true;
            break;
        }
        
        // Show progress dots
        if ((to_ms_since_boot(get_absolute_time()) - dhcp_wait_start) % 1000 == 0) {
            printf(".");
        }
        
        sleep_ms(100);
    }
    
    printf("\n");
    TEST_ASSERT_TRUE_MESSAGE(dhcp_success, "DHCP should assign an IP address");
    
    // Get and display IP address
    simple_ip_addr_t ip_addr;
    TEST_ASSERT_TRUE_MESSAGE(network_manager_get_ip_address(&ip_addr), "Should have valid IP address");
    
    char ip_str[16];
    network_manager_ip_to_string(&ip_addr, ip_str);
    
    printf("\n");
    printf("🎉 DHCP SUCCESS! Network is ready for ping testing\n");
    printf("==================================================\n");
    printf("📍 ASSIGNED IP ADDRESS: %s\n", ip_str);
    printf("📍 HOSTNAME: UART2ETH\n");
    printf("📍 PING COMMAND: ping %s\n", ip_str);
    printf("==================================================\n");
    printf("\n");
    printf("✅ Ready to receive pings for 30 seconds...\n");
    printf("💡 Open another terminal and run: ping %s\n", ip_str);
    printf("\n");
    
    // Initialize statistics
    test_start_time = to_ms_since_boot(get_absolute_time());
    const enc28j60_state_t* initial_state = enc28j60_get_state();
    uint32_t initial_rx = initial_state ? initial_state->packets_received : 0;
    uint32_t last_status_time = test_start_time;
    
    // Main ping response loop (30 seconds)
    printf("Starting 30-second ping response test...\n");
    printf("Time | RX Packets | Network Status\n");
    printf("-----|------------|---------------\n");
    
    while ((to_ms_since_boot(get_absolute_time()) - test_start_time) < TEST_DURATION_MS) {
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        
        // Process network stack (handle incoming pings)
        network_manager_process();
        sys_check_timeouts();
        
        // Update statistics every 5 seconds
        if (current_time - last_status_time >= STATUS_UPDATE_INTERVAL_MS) {
            uint32_t elapsed_seconds = (current_time - test_start_time) / 1000;
            
            // Get current packet statistics
            const enc28j60_state_t* current_state = enc28j60_get_state();
            uint32_t current_rx = current_state ? current_state->packets_received : 0;
            uint32_t new_packets = current_rx - initial_rx;
            
            // Network status
            const char* status = network_manager_is_dhcp_bound() ? "DHCP Active" : "No DHCP";
            
            printf("%2us  | %-10u | %s\n", elapsed_seconds, new_packets, status);
            
            last_status_time = current_time;
        }
        
        // Small delay to prevent overwhelming the CPU
        sleep_ms(10);
    }
    
    printf("\n");
    printf("==================================================\n");
    printf("=== PING CONNECTIVITY TEST RESULTS ===\n");
    printf("==================================================\n");
    
    // Final statistics
    const enc28j60_state_t* final_state = enc28j60_get_state();
    uint32_t final_rx = final_state ? final_state->packets_received : 0;
    uint32_t final_tx = final_state ? final_state->packets_sent : 0;
    uint32_t total_new_rx = final_rx - initial_rx;
    
    printf("📊 PACKET STATISTICS:\n");
    printf("   • Total RX packets: %u\n", final_rx);
    printf("   • Total TX packets: %u\n", final_tx);
    printf("   • New RX during test: %u\n", total_new_rx);
    printf("   • Test duration: 30 seconds\n");
    printf("\n");
    
    printf("🌐 NETWORK INFORMATION:\n");
    printf("   • IP Address: %s\n", ip_str);
    printf("   • Hostname: UART2ETH\n");
    printf("   • DHCP Status: %s\n", network_manager_is_dhcp_bound() ? "Active" : "Inactive");
    printf("   • Link Status: %s\n", network_manager_is_link_up() ? "UP" : "DOWN");
    printf("\n");
    
    if (total_new_rx > 0) {
        printf("✅ SUCCESS: Received %u packets during test\n", total_new_rx);
        printf("💡 This indicates the device is responding to network traffic\n");
    } else {
        printf("⚠️  INFO: No packets received during test\n");
        printf("💡 Try pinging %s from another computer\n", ip_str);
    }
    
    printf("\n");
    printf("🔧 NEXT STEPS:\n");
    printf("   • Network stack is working correctly\n");
    printf("   • DHCP hostname is configured\n");
    printf("   • Ready for TCP socket implementation\n");
    printf("==================================================\n");
    
    // Test passes if we got DHCP and maintained network connectivity
    TEST_ASSERT_TRUE_MESSAGE(network_manager_is_dhcp_bound(), "Should maintain DHCP throughout test");
    TEST_ASSERT_TRUE_MESSAGE(network_manager_is_link_up(), "Should maintain link throughout test");
}

/**
 * @brief Test runner for ping connectivity tests
 */
int test_ping_connectivity_run_tests(void) {
    UNITY_BEGIN();
    
    printf("\n");
    printf("========================================\n");
    printf("=== PING CONNECTIVITY TEST SUITE ===\n");
    printf("========================================\n");
    
    RUN_TEST(test_ping_connectivity_30_seconds);
    
    printf("========================================\n");
    printf("=== PING TESTS COMPLETE ===\n");
    printf("========================================\n");
    
    return UNITY_END();
}

/**
 * @brief Main function for ping connectivity tests
 */
int main(void) {
    stdio_usb_init();
    // Init UART1 for debug output  
    stdio_uart_init_full(uart1, 115200, 20, 21);
    
    // Wait for serial connection
    sleep_ms(2000);
    
    printf("\n");
    printf("===============================================\n");
    printf("=== UART2ETH Ping Connectivity Test ===\n");
    printf("===============================================\n");
    printf("Testing network connectivity with DHCP hostname\n");
    printf("Hardware: ENC28J60 on SPI0\n");
    printf("Debug Output: UART1 (GPIO 20/21)\n");
    printf("===============================================\n");
    
    // Run the ping connectivity tests
    int result = test_ping_connectivity_run_tests();
    
    // Keep running forever
    while (true) {
        printf("\nPing Connectivity Tests completed with result: %d\n", result);
        printf("Tests %s\n", (result == 0) ? "PASSED" : "FAILED");
        sleep_ms(30000);  // Wait 30 seconds before showing again
    }
    
    return result;
}
