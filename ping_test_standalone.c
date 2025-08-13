/**
 * @file ping_test_standalone.c
 * @brief Standalone ping test - keeps network alive indefinitely
 * 
 * Simple test that initializes network stack and keeps it running
 * indefinitely for external ping testing. No test framework overhead.
 */

#include "network/network_manager.h"
#include "network/enc28j60_driver.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "lwip/timeouts.h"
#include <stdio.h>

#define DHCP_TIMEOUT_MS 60000

int main(void) {
    stdio_usb_init();
    // Initialize UART1 for debug output (as per your instructions)
    stdio_uart_init_full(uart1, 115200, 20, 21);
    
    sleep_ms(2000);
    
    printf("\n=== UART2ETH Standalone Ping Test ===\n");
    printf("This test keeps the network alive indefinitely for ping testing\n");
    printf("Debug output: UART1 (/dev/ttyUSB0)\n\n");
    
    // Initialize ENC28J60
    printf("Initializing ENC28J60...\n");
    bool enc_init = enc28j60_init();
    if (!enc_init) {
        printf("ERROR: ENC28J60 initialization failed\n");
        while (true) {
            printf("ENC28J60 init failed - halting\n");
            sleep_ms(5000);
        }
    }
    printf("✓ ENC28J60 initialized\n");
    
    // Initialize network manager with DHCP
    printf("Initializing network manager...\n");
    network_config_t config;
    network_manager_get_default_config(&config);
    config.use_dhcp = true;
    config.dhcp_timeout_ms = DHCP_TIMEOUT_MS;
    
    bool net_init = network_manager_init(&config);
    if (!net_init) {
        printf("ERROR: Network manager initialization failed\n");
        while (true) {
            printf("Network init failed - halting\n");
            sleep_ms(5000);
        }
    }
    printf("✓ Network manager initialized\n");
    
    // Wait for link up
    printf("Waiting for physical link");
    uint32_t link_timeout = 30000; // 30 seconds
    uint32_t link_start = to_ms_since_boot(get_absolute_time());
    
    while (!network_manager_is_link_up()) {
        network_manager_process();
        if ((to_ms_since_boot(get_absolute_time()) - link_start) > link_timeout) {
            printf("\nERROR: Link timeout\n");
            while (true) {
                printf("Link timeout - check cable\n");
                sleep_ms(5000);
            }
        }
        printf(".");
        sleep_ms(500);
    }
    printf("\n✓ Physical link up\n");
    
    // Wait for DHCP
    printf("Starting DHCP, waiting for IP address");
    uint32_t dhcp_start = to_ms_since_boot(get_absolute_time());
    
    while (!network_manager_is_dhcp_bound()) {
        network_manager_process();
        sys_check_timeouts();
        
        if ((to_ms_since_boot(get_absolute_time()) - dhcp_start) > DHCP_TIMEOUT_MS) {
            printf("\nERROR: DHCP timeout\n");
            while (true) {
                printf("DHCP timeout - check DHCP server\n");
                sleep_ms(5000);
            }
        }
        printf(".");
        sleep_ms(500);
    }
    printf("\n✓ DHCP successful\n");
    
    // Get and display IP address
    simple_ip_addr_t ip_addr;
    bool ip_valid = network_manager_get_ip_address(&ip_addr);
    if (!ip_valid) {
        printf("ERROR: Could not get IP address\n");
        while (true) {
            printf("IP address error\n");
            sleep_ms(5000);
        }
    }
    
    char ip_str[16];
    network_manager_ip_to_string(&ip_addr, ip_str);
    
    printf("\n🎉 NETWORK READY FOR PING TESTING 🎉\n");
    printf("Target IP: %s\n", ip_str);
    printf("From another machine run: ping %s\n", ip_str);
    printf("Network will stay active indefinitely...\n\n");
    
    // Infinite network processing loop 
    uint32_t last_status = to_ms_since_boot(get_absolute_time());
    uint32_t loop_count = 0;
    
    while (true) {
        // High-performance network processing
        network_manager_process();
        
        // Process lwIP timeouts occasionally
        if (++loop_count % 1000 == 0) {
            sys_check_timeouts();
        }
        
        // Status update every 10 seconds
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - last_status >= 10000) {
            network_stats_t stats;
            network_manager_get_stats(&stats);
            
            bool link_up = network_manager_is_link_up();
            bool dhcp_bound = network_manager_is_dhcp_bound();
            
            printf("Status: Link=%s DHCP=%s IP=%s RX=%u TX=%u\n",
                   link_up ? "UP" : "DOWN",
                   dhcp_bound ? "BOUND" : "UNBOUND", 
                   ip_str,
                   stats.packets_rx,
                   stats.packets_tx);
            
            last_status = current_time;
        }
        
        // CRITICAL: Minimal delay for maximum ping responsiveness
        sleep_us(100);  // 10kHz processing rate
    }
    
    return 0;  // Never reached
}
