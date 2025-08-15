/**
 * @file test_dhcp_debug.c
 * @brief Debug test for DHCP and ENC28J60 receive functionality
 * 
 * This test focuses on debugging why DHCP packets are being sent but no
 * responses are being received. Tests ENC28J60 receive functionality step by step.
 */

#include "unity.h"
#include "network/enc28j60_driver.h"
#include "network/network_manager.h"
#include "network/lwip_netif_enc28j60.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include <string.h>
#include <stdio.h>

// Test configuration
#define TEST_DURATION_MS 30000    // 30 seconds for debugging
#define STATUS_CHECK_INTERVAL_MS 2000  // Check status every 2 seconds

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
 * @brief Test ENC28J60 receive buffer configuration and status
 */
void test_enc28j60_receive_buffer_config(void) {
    printf("=== ENC28J60 Receive Buffer Configuration Test ===\n");
    
    // Initialize ENC28J60
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "ENC28J60 should initialize");
    
    // Check hardware is ready
    bool ready = enc28j60_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(ready, "ENC28J60 should be ready");
    
    // Check link status
    bool link_up = enc28j60_get_link_status();
    printf("Link Status: %s\n", link_up ? "UP" : "DOWN");
    TEST_ASSERT_TRUE_MESSAGE(link_up, "Link should be UP for receive test");
    
    // Read receive buffer configuration registers
    enc28j60_set_bank(0);  // Bank 0 for buffer registers
    uint8_t erxstl = enc28j60_read_register(0x08);  // ERXSTL
    uint8_t erxsth = enc28j60_read_register(0x09);  // ERXSTH
    uint8_t erxndl = enc28j60_read_register(0x0A);  // ERXNDL
    uint8_t erxndh = enc28j60_read_register(0x0B);  // ERXNDH
    uint8_t erxrdptl = enc28j60_read_register(0x0C);  // ERXRDPTL
    uint8_t erxrdpth = enc28j60_read_register(0x0D);  // ERXRDPTH
    
    uint16_t rx_start = erxstl | (erxsth << 8);
    uint16_t rx_end = erxndl | (erxndh << 8);
    uint16_t rx_read_ptr = erxrdptl | (erxrdpth << 8);
    
    printf("RX Buffer Start: 0x%04X\n", rx_start);
    printf("RX Buffer End: 0x%04X\n", rx_end);
    printf("RX Read Pointer: 0x%04X\n", rx_read_ptr);
    
    // Read receive filter configuration
    enc28j60_set_bank(1);  // Bank 1 for filter registers
    uint8_t erxfcon = enc28j60_read_register(0x18);  // ERXFCON
    uint8_t epktcnt = enc28j60_read_register(0x19);  // EPKTCNT
    
    printf("RX Filter Control: 0x%02X\n", erxfcon);
    printf("Packet Count: %u\n", epktcnt);
    
    // Check specific filter bits
    printf("Filter Settings:\n");
    printf("  UCEN (Unicast): %s\n", (erxfcon & 0x80) ? "Enabled" : "Disabled");
    printf("  ANDOR: %s\n", (erxfcon & 0x40) ? "AND" : "OR");
    printf("  CRCEN (CRC Check): %s\n", (erxfcon & 0x20) ? "Enabled" : "Disabled");
    printf("  MCEN (Multicast): %s\n", (erxfcon & 0x02) ? "Enabled" : "Disabled");
    printf("  BCEN (Broadcast): %s\n", (erxfcon & 0x01) ? "Enabled" : "Disabled");
    
    // Check ECON1 for receive enable
    enc28j60_set_bank(0);
    uint8_t econ1 = enc28j60_read_register(0x1F);  // ECON1
    printf("ECON1: 0x%02X\n", econ1);
    printf("RXEN (Receive Enable): %s\n", (econ1 & 0x04) ? "Enabled" : "Disabled");
    
    TEST_ASSERT_TRUE_MESSAGE((econ1 & 0x04) != 0, "Receive should be enabled");
    TEST_ASSERT_TRUE_MESSAGE((erxfcon & 0x01) != 0, "Broadcast filter should be enabled for DHCP");
}

/**
 * @brief Test interrupt status and processing
 */
void test_enc28j60_interrupt_status(void) {
    printf("=== ENC28J60 Interrupt Status Test ===\n");
    
    // Initialize ENC28J60
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "ENC28J60 should initialize");
    
    // Read interrupt enable register
    enc28j60_set_bank(0);
    uint8_t eie = enc28j60_read_register(0x1B);  // EIE
    printf("Interrupt Enable (EIE): 0x%02X\n", eie);
    printf("Interrupt Settings:\n");
    printf("  INTIE (Global): %s\n", (eie & 0x80) ? "Enabled" : "Disabled");
    printf("  PKTIE (Packet): %s\n", (eie & 0x40) ? "Enabled" : "Disabled");
    printf("  DMAIE (DMA): %s\n", (eie & 0x20) ? "Enabled" : "Disabled");
    printf("  LINKIE (Link): %s\n", (eie & 0x10) ? "Enabled" : "Disabled");
    printf("  TXIE (TX): %s\n", (eie & 0x08) ? "Enabled" : "Disabled");
    printf("  RXERIE (RX Error): %s\n", (eie & 0x01) ? "Enabled" : "Disabled");
    
    // Read current interrupt status
    uint8_t eir = enc28j60_read_register(0x1C);  // EIR
    printf("Interrupt Status (EIR): 0x%02X\n", eir);
    printf("Active Interrupts:\n");
    printf("  PKTIF (Packet): %s\n", (eir & 0x40) ? "ACTIVE" : "Clear");
    printf("  DMAIF (DMA): %s\n", (eir & 0x20) ? "ACTIVE" : "Clear");
    printf("  LINKIF (Link): %s\n", (eir & 0x10) ? "ACTIVE" : "Clear");
    printf("  TXIF (TX): %s\n", (eir & 0x08) ? "ACTIVE" : "Clear");
    printf("  RXERIF (RX Error): %s\n", (eir & 0x01) ? "ACTIVE" : "Clear");
    
    // Check interrupt pin status directly
    bool int_pin_state = gpio_get(14);  // ENC28J60_INTERRUPT_PIN
    printf("Hardware INT Pin (GPIO 14): %s\n", int_pin_state ? "HIGH" : "LOW");
    
    // Check if driver detected pending interrupt
    bool has_pending = enc28j60_has_pending_interrupt();
    printf("Driver Pending Interrupt: %s\n", has_pending ? "YES" : "NO");
    
    TEST_ASSERT_TRUE_MESSAGE((eie & 0x80) != 0, "Global interrupts should be enabled");
    TEST_ASSERT_TRUE_MESSAGE((eie & 0x40) != 0, "Packet interrupts should be enabled");
}

/**
 * @brief Test manual DHCP packet sending and receive monitoring
 */
void test_manual_dhcp_monitoring(void) {
    printf("=== Manual DHCP Monitoring Test ===\n");
    
    // Initialize complete network stack
    bool enc_init = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(enc_init, "ENC28J60 initialization required");
    
    network_config_t config;
    network_manager_get_default_config(&config);
    config.use_dhcp = true;
    config.dhcp_timeout_ms = 30000;
    
    bool net_init = network_manager_init(&config);
    TEST_ASSERT_TRUE_MESSAGE(net_init, "Network manager initialization required");
    
    // Wait for link up
    printf("Waiting for physical link...");
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < 10000) {
        network_manager_process();
        if (network_manager_is_link_up()) {
            printf(" UP!\n");
            break;
        }
        printf(".");
        sleep_ms(500);
    }
    
    TEST_ASSERT_TRUE_MESSAGE(network_manager_is_link_up(), "Link should be up");
    
    printf("Starting DHCP monitoring for %d seconds...\n", TEST_DURATION_MS / 1000);
    printf("Will check interrupt status and packet counts every %d seconds\n", STATUS_CHECK_INTERVAL_MS / 1000);
    
    start_time = to_ms_since_boot(get_absolute_time());
    uint32_t last_status_time = start_time;
    uint32_t status_check_count = 0;
    
    // Get initial statistics
    const enc28j60_state_t* initial_state = enc28j60_get_state();
    uint32_t initial_tx = initial_state ? initial_state->packets_sent : 0;
    uint32_t initial_rx = initial_state ? initial_state->packets_received : 0;
    
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < TEST_DURATION_MS) {
        // Process network stack
        network_manager_process();
        sys_check_timeouts();
        
        // Check for interrupts
        if (enc28j60_has_pending_interrupt()) {
            printf("*** INTERRUPT DETECTED ***\n");
            enc28j60_process_interrupts(false);
        }
        
        // Detailed status check every STATUS_CHECK_INTERVAL_MS
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - last_status_time >= STATUS_CHECK_INTERVAL_MS) {
            status_check_count++;
            printf("\n--- Status Check #%u (Time: %u ms) ---\n", 
                   status_check_count, current_time - start_time);
            
            // ENC28J60 statistics
            const enc28j60_state_t* state = enc28j60_get_state();
            if (state) {
                printf("ENC28J60: TX=%u (+%u), RX=%u (+%u), TX_ERR=%u, RX_ERR=%u\n",
                       state->packets_sent, state->packets_sent - initial_tx,
                       state->packets_received, state->packets_received - initial_rx,
                       state->tx_errors, state->rx_errors);
            }
            
            // Check packet count register directly
            enc28j60_set_bank(1);
            uint8_t pktcnt = enc28j60_read_register(0x19);
            printf("EPKTCNT Register: %u packets\n", pktcnt);
            
            // Check interrupt status
            uint8_t eir = enc28j60_get_interrupt_status();
            printf("EIR Register: 0x%02X", eir);
            if (eir & 0x40) printf(" [PACKET]");
            if (eir & 0x10) printf(" [LINK]");
            if (eir & 0x08) printf(" [TX]");
            if (eir & 0x01) printf(" [RX_ERR]");
            printf("\n");
            
            // Check GPIO interrupt pin
            bool int_pin = gpio_get(14);
            printf("GPIO 14 (INT): %s\n", int_pin ? "HIGH" : "LOW");
            
            // Network manager status
            network_status_t net_status = network_manager_get_status();
            printf("Network Status: %s\n", network_manager_status_to_string(net_status));
            
            if (network_manager_is_dhcp_bound()) {
                printf("*** DHCP SUCCESS! ***\n");
                simple_ip_addr_t ip_addr;
                if (network_manager_get_ip_address(&ip_addr)) {
                    char ip_str[16];
                    network_manager_ip_to_string(&ip_addr, ip_str);
                    printf("Assigned IP: %s\n", ip_str);
                }
                break;
            }
            
            last_status_time = current_time;
            printf("----------------------------------------\n");
        }
        
        sleep_ms(10);
    }
    
    printf("\n=== Final Statistics ===\n");
    const enc28j60_state_t* final_state = enc28j60_get_state();
    if (final_state) {
        printf("Total TX Packets: %u\n", final_state->packets_sent);
        printf("Total RX Packets: %u\n", final_state->packets_received);
        printf("TX Errors: %u\n", final_state->tx_errors);
        printf("RX Errors: %u\n", final_state->rx_errors);
    }
    
    // The test passes if we can send packets, even if DHCP doesn't complete
    // This helps us debug the receive path
    TEST_ASSERT_TRUE_MESSAGE(final_state && final_state->packets_sent > 0, 
                            "Should have sent at least one packet");
    
    printf("=== DHCP Debug Test Complete ===\n");
}

/**
 * @brief Test runner for DHCP debug tests
 */
int test_dhcp_debug_run_tests(void) {
    UNITY_BEGIN();
    
    printf("\n========================================\n");
    printf("=== DHCP DEBUG TEST SUITE ===\n");
    printf("========================================\n");
    
    RUN_TEST(test_enc28j60_receive_buffer_config);
    RUN_TEST(test_enc28j60_interrupt_status);
    RUN_TEST(test_manual_dhcp_monitoring);
    
    printf("========================================\n");
    printf("=== DHCP DEBUG TESTS COMPLETE ===\n");
    printf("========================================\n");
    
    return UNITY_END();
}

/**
 * @brief Main function for DHCP debug tests
 */
int main(void) {
    stdio_usb_init();
    // Init UART1 for debug output
    stdio_uart_init_full(uart1, 115200, 20, 21);
    
    // Wait for serial connection
    sleep_ms(2000);
    
    printf("\n");
    printf("===============================================\n");
    printf("=== UART2ETH DHCP Debug Test Suite ===\n");
    printf("===============================================\n");
    printf("Debugging why DHCP packets are sent but no responses received\n");
    printf("Hardware: ENC28J60 on SPI0\n");
    printf("Debug Output: UART1 (GPIO 20/21)\n");
    printf("===============================================\n");
    
    // Run the debug tests
    int result = test_dhcp_debug_run_tests();
    
    // Keep running forever
    while (true) {
        printf("\nDHCP Debug Tests completed with result: %d\n", result);
        printf("Tests %s\n", (result == 0) ? "PASSED" : "FAILED");
        sleep_ms(10000);
    }
    
    return result;
}
