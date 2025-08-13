/**
 * @file transmission_debug_test.c
 * @brief Minimal transmission debugging test
 */

#include "network/enc28j60_driver.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include <string.h>
#include <stdio.h>

// Test MAC address
static uint8_t test_mac_address[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};

// Simple test packet (minimal ARP)
static uint8_t test_packet_data[64] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination MAC (broadcast)
    0x02, 0x00, 0x00, 0x12, 0x34, 0x56,  // Source MAC
    0x08, 0x06,                          // EtherType (ARP)
    // ARP payload
    0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01,
    0x02, 0x00, 0x00, 0x12, 0x34, 0x56, 0xC0, 0xA8, 0x01, 0x64,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0xA8, 0x01, 0x01,
    // Padding to minimum frame size
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

int main(void) {
    // Initialize stdio for UART1 debug
    stdio_usb_init();
    stdio_uart_init_full(uart1, 115200, 20, 21);
    
    sleep_ms(2000);  // Wait for serial connection
    
    printf("\n=== ENC28J60 TRANSMISSION DEBUG TEST ===\n");
    printf("Diagnosing packet transmission issue...\n\n");
    
    // Step 1: Initialize driver
    printf("Step 1: Initializing ENC28J60 driver...\n");
    bool init_result = enc28j60_init();
    printf("   Init result: %s\n", init_result ? "SUCCESS" : "FAILED");
    
    if (!init_result) {
        printf("FATAL: Driver initialization failed\n");
        while (true) {
            sleep_ms(1000);
        }
    }
    
    // Step 2: Check driver state
    printf("\nStep 2: Checking driver state...\n");
    const enc28j60_state_t* state = enc28j60_get_state();
    printf("   Initialized: %s\n", state->initialized ? "YES" : "NO");
    printf("   TX packets sent: %u\n", state->packets_sent);
    printf("   RX packets received: %u\n", state->packets_received);
    printf("   TX errors: %u\n", state->tx_errors);
    printf("   RX errors: %u\n", state->rx_errors);
    
    // Step 3: Check ready status
    printf("\nStep 3: Checking ready status...\n");
    bool ready = enc28j60_is_ready();
    printf("   Ready: %s\n", ready ? "YES" : "NO");
    
    // Step 4: Check link status
    printf("\nStep 4: Checking link status...\n");
    bool link_up = enc28j60_get_link_status();
    printf("   Link: %s\n", link_up ? "UP" : "DOWN");
    
    // Step 5: Set MAC address
    printf("\nStep 5: Setting MAC address...\n");
    enc28j60_set_mac_address(test_mac_address);
    printf("   MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n", 
           test_mac_address[0], test_mac_address[1], test_mac_address[2],
           test_mac_address[3], test_mac_address[4], test_mac_address[5]);
    
    // Step 6: Read back key registers
    printf("\nStep 6: Reading critical registers...\n");
    uint8_t macon1 = enc28j60_read_register_bank(ENC28J60_MACON1, ENC28J60_BANK2);
    uint8_t macon3 = enc28j60_read_register_bank(ENC28J60_MACON3, ENC28J60_BANK2);
    uint8_t econ1 = enc28j60_read_register(ENC28J60_ECON1);
    uint8_t eie = enc28j60_read_register(ENC28J60_EIE);
    uint8_t eir = enc28j60_read_register(ENC28J60_EIR);
    
    printf("   MACON1: 0x%02X (should be 0x0D for MARXEN)\n", macon1);
    printf("   MACON3: 0x%02X (should be 0xF3 for TX config)\n", macon3);
    printf("   ECON1:  0x%02X (should have RXEN=0x04)\n", econ1);
    printf("   EIE:    0x%02X (interrupt enable)\n", eie);
    printf("   EIR:    0x%02X (interrupt flags)\n", eir);
    
    // Step 7: Attempt packet transmission
    printf("\nStep 7: Attempting packet transmission...\n");
    enc28j60_packet_t packet;
    packet.data = test_packet_data;
    packet.length = sizeof(test_packet_data);
    packet.valid = true;
    
    printf("   Packet length: %u bytes\n", packet.length);
    printf("   Packet valid: %s\n", packet.valid ? "YES" : "NO");
    printf("   Calling enc28j60_send_packet()...\n");
    
    bool send_result = enc28j60_send_packet(&packet);
    printf("   Send result: %s\n", send_result ? "SUCCESS" : "FAILED");
    
    if (!send_result) {
        printf("ERROR: Packet transmission failed to start\n");
    } else {
        // Step 8: Monitor transmission completion
        printf("\nStep 8: Monitoring transmission completion...\n");
        uint32_t timeout_count = 0;
        const uint32_t MAX_TIMEOUT = 50000;  // 50ms timeout
        
        while (!enc28j60_is_tx_complete() && timeout_count < MAX_TIMEOUT) {
            if (timeout_count % 1000 == 0) {
                uint8_t current_econ1 = enc28j60_read_register(ENC28J60_ECON1);
                uint8_t current_eir = enc28j60_read_register(ENC28J60_EIR);
                printf("   Timeout %u: ECON1=0x%02X, EIR=0x%02X\n", 
                       timeout_count / 1000, current_econ1, current_eir);
            }
            sleep_us(1);
            timeout_count++;
        }
        
        bool tx_complete = enc28j60_is_tx_complete();
        printf("   TX complete: %s\n", tx_complete ? "YES" : "NO");
        printf("   Timeout count: %u / %u\n", timeout_count, MAX_TIMEOUT);
        
        // Step 9: Check final state
        printf("\nStep 9: Checking final state...\n");
        state = enc28j60_get_state();
        printf("   TX packets sent: %u\n", state->packets_sent);
        printf("   TX errors: %u\n", state->tx_errors);
        
        uint8_t final_eir = enc28j60_read_register(ENC28J60_EIR);
        printf("   Final EIR: 0x%02X\n", final_eir);
    }
    
    printf("\n=== TRANSMISSION DEBUG TEST COMPLETE ===\n");
    
    // Keep running forever
    while (true) {
        printf("Test completed. System ready for analysis.\n");
        sleep_ms(5000);
    }
    
    return 0;
}
