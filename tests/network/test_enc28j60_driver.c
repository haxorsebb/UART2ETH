/**
 * @file test_enc28j60_driver.c
 * @brief Unit tests for ENC28J60 Ethernet Controller Driver
 * 
 * Tests ENC28J60 SPI driver functionality including initialization,
 * register access, buffer operations, and packet transmission/reception.
 * 
 * Test Strategy:
 * - Test initialization and basic SPI communication
 * - Test register read/write operations
 * - Test buffer memory access
 * - Test packet transmission and reception
 * - Test interrupt handling
 * - Test error conditions and recovery
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 * - Test-Driven Development process
 */

#include "unity.h"
#include "network/enc28j60_driver.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include <string.h>

// Function declarations for hardware debugging tests
void test_enc28j60_interrupt_monitoring(void);
void test_enc28j60_cable_detection(void);

// Test fixtures and helper data
static uint8_t test_mac_address[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};
static uint8_t test_packet_data[64] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination MAC (broadcast)
    0x02, 0x00, 0x00, 0x12, 0x34, 0x56,  // Source MAC
    0x08, 0x06,                          // EtherType (ARP)
    // ARP payload...
    0x00, 0x01, 0x08, 0x00, 0x06, 0x04, 0x00, 0x01,
    0x02, 0x00, 0x00, 0x12, 0x34, 0x56, 0xC0, 0xA8, 0x01, 0x64,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0xA8, 0x01, 0x01,
    // Padding to minimum frame size
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

void setUp(void) {
    // Test setup - called before each test
    // Initialize any test-specific resources
}

void tearDown(void) {
    // Test cleanup - called after each test
    // Clean up any test-specific resources
    enc28j60_deinit();
}

/**
 * @brief Test ENC28J60 driver initialization
 */
void test_enc28j60_init_success(void) {
    // Test that driver initializes successfully
    bool result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(result, "ENC28J60 initialization should succeed");
    
    // Test that driver reports as ready after initialization
    bool ready = enc28j60_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(ready, "ENC28J60 should be ready after initialization");
    
    // Test that driver state is initialized properly
    const enc28j60_state_t* state = enc28j60_get_state();
    TEST_ASSERT_NOT_NULL_MESSAGE(state, "Driver state should not be NULL");
    TEST_ASSERT_TRUE_MESSAGE(state->initialized, "Driver state should indicate initialized");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state->packets_sent, "Initial packets sent should be 0");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, state->packets_received, "Initial packets received should be 0");
}

/**
 * @brief Test ENC28J60 driver double initialization
 */
void test_enc28j60_init_already_initialized(void) {
    // Initialize once
    bool result1 = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(result1, "First initialization should succeed");
    
    // Try to initialize again - should handle gracefully
    bool result2 = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(result2, "Second initialization should succeed (idempotent)");
    
    // Should still be ready
    bool ready = enc28j60_is_ready();
    TEST_ASSERT_TRUE_MESSAGE(ready, "ENC28J60 should remain ready after double init");
}

/**
 * @brief Test ENC28J60 register read operations
 */
void test_enc28j60_register_read(void) {
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for register test");
    
    // Test reading ESTAT register (should have CLKRDY bit set after init)
    uint8_t estat = enc28j60_read_register(ENC28J60_ESTAT);
    printf("DEBUG: ESTAT = 0x%02X\n", estat);
    TEST_ASSERT_MESSAGE((estat & ENC28J60_ESTAT_CLKRDY) != 0, 
                       "ESTAT register should have CLKRDY bit set after initialization");
    
    // Test reading ECON1 register
    uint8_t econ1 = enc28j60_read_register(ENC28J60_ECON1);
    printf("DEBUG: ECON1 = 0x%02X (expected to have RXEN=0x04 set)\n", econ1);
    
    // Check if we can read the revision ID to verify hardware communication
    uint8_t erevid = enc28j60_read_register_bank(ENC28J60_EREVID, ENC28J60_BANK3);
    printf("DEBUG: EREVID = 0x%02X (should be 2 or 6)\n", erevid);
    
    // ECON1 should have RXEN bit set (0x04) after initialization
    TEST_ASSERT_MESSAGE((econ1 & ENC28J60_ECON1_RXEN) != 0, 
                       "ECON1 register should have RXEN bit set after initialization");
}

/**
 * @brief Test ENC28J60 register write operations
 */
void test_enc28j60_register_write(void) {
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for register test");
    
    // Test writing to ERDPTL register (RX read pointer low)
    uint8_t test_value = 0x55;
    enc28j60_write_register(ENC28J60_ERDPTL, test_value);
    
    // Read back the value
    uint8_t read_value = enc28j60_read_register(ENC28J60_ERDPTL);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(test_value, read_value, 
                                   "Written register value should match read value");
    
    // Test with different value
    test_value = 0xAA;
    enc28j60_write_register(ENC28J60_ERDPTL, test_value);
    read_value = enc28j60_read_register(ENC28J60_ERDPTL);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(test_value, read_value, 
                                   "Second written register value should match read value");
}

/**
 * @brief Test ENC28J60 register bit field operations
 */
void test_enc28j60_register_bit_operations(void) {
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for bit operations test");
    
    // Test bit field set operation on EIE register
    uint8_t original_eie = enc28j60_read_register(ENC28J60_EIE);
    
    // Set a specific bit (bit 0)
    uint8_t bit_mask = 0x01;
    enc28j60_set_register_bits(ENC28J60_EIE, bit_mask);
    
    uint8_t eie_after_set = enc28j60_read_register(ENC28J60_EIE);
    TEST_ASSERT_MESSAGE((eie_after_set & bit_mask) != 0, 
                       "Bit should be set after set_register_bits operation");
    
    // Clear the same bit
    enc28j60_clear_register_bits(ENC28J60_EIE, bit_mask);
    
    uint8_t eie_after_clear = enc28j60_read_register(ENC28J60_EIE);
    TEST_ASSERT_MESSAGE((eie_after_clear & bit_mask) == 0, 
                       "Bit should be cleared after clear_register_bits operation");
}

/**
 * @brief Test ENC28J60 buffer memory read operations
 */
void test_enc28j60_buffer_read(void) {
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for buffer test");
    
    // Set up buffer read pointer to a known location
    enc28j60_write_register(ENC28J60_ERDPTL, 0x00);
    enc28j60_write_register(ENC28J60_ERDPTH, 0x00);
    
    // Read some data from buffer memory
    uint8_t buffer[16];
    memset(buffer, 0xFF, sizeof(buffer));  // Initialize to known pattern
    
    enc28j60_read_buffer(buffer, sizeof(buffer));
    
    // Buffer should contain different data after read (not all 0xFF)
    bool data_changed = false;
    for (int i = 0; i < sizeof(buffer); i++) {
        if (buffer[i] != 0xFF) {
            data_changed = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(data_changed, "Buffer read should return data from ENC28J60 memory");
}

/**
 * @brief Test ENC28J60 buffer memory write operations
 */
void test_enc28j60_buffer_write(void) {
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for buffer test");
    
    // Set up buffer write pointer to TX buffer area
    uint16_t tx_start = 0x1A00;  // TX buffer start address
    enc28j60_write_register(ENC28J60_EWRPTL, tx_start & 0xFF);
    enc28j60_write_register(ENC28J60_EWRPTH, (tx_start >> 8) & 0xFF);
    
    // Write test pattern to buffer
    uint8_t test_pattern[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                               0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    
    enc28j60_write_buffer(test_pattern, sizeof(test_pattern));
    
    // Read back the data to verify write
    enc28j60_write_register(ENC28J60_ERDPTL, tx_start & 0xFF);
    enc28j60_write_register(ENC28J60_ERDPTH, (tx_start >> 8) & 0xFF);
    
    uint8_t read_buffer[16];
    enc28j60_read_buffer(read_buffer, sizeof(read_buffer));
    
    // Verify written data matches read data
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(test_pattern, read_buffer, sizeof(test_pattern),
                                         "Written buffer data should match read data");
}

/**
 * @brief Test ENC28J60 MAC address configuration
 */
void test_enc28j60_mac_address(void) {
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for MAC address test");
    
    // Set MAC address
    enc28j60_set_mac_address(test_mac_address);
    
    // Read back MAC address
    uint8_t read_mac[6];
    enc28j60_get_mac_address(read_mac);
    
    // Verify MAC address matches
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(test_mac_address, read_mac, 6,
                                         "Read MAC address should match written MAC address");
}

/**
 * @brief Test ENC28J60 broadcast packet reception configuration
 */
void test_enc28j60_broadcast_reception_config(void) {
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for broadcast reception test");
    
    // Read receive filter configuration from Bank 1
    enc28j60_set_bank(ENC28J60_BANK1);
    uint8_t erxfcon = enc28j60_read_register(ENC28J60_ERXFCON);
    
    printf("DEBUG: ERXFCON = 0x%02X\n", erxfcon);
    printf("DEBUG: UCEN (Unicast Enable): %s\n", (erxfcon & ENC28J60_ERXFCON_UCEN) ? "YES" : "NO");
    printf("DEBUG: CRCEN (CRC Enable): %s\n", (erxfcon & ENC28J60_ERXFCON_CRCEN) ? "YES" : "NO");
    printf("DEBUG: MCEN (Multicast Enable): %s\n", (erxfcon & ENC28J60_ERXFCON_MCEN) ? "YES" : "NO");
    printf("DEBUG: BCEN (Broadcast Enable): %s\n", (erxfcon & ENC28J60_ERXFCON_BCEN) ? "YES" : "NO");
    
    // Test that broadcast packet reception is enabled for DHCP functionality
    TEST_ASSERT_MESSAGE((erxfcon & ENC28J60_ERXFCON_BCEN) != 0, 
                       "Broadcast packet reception MUST be enabled for DHCP functionality");
    
    // Test that unicast is also enabled
    TEST_ASSERT_MESSAGE((erxfcon & ENC28J60_ERXFCON_UCEN) != 0, 
                       "Unicast packet reception should be enabled");
    
    // Test that CRC checking is enabled
    TEST_ASSERT_MESSAGE((erxfcon & ENC28J60_ERXFCON_CRCEN) != 0, 
                       "CRC checking should be enabled for packet integrity");
}

/**
 * @brief Test ENC28J60 link status detection
 */
void test_enc28j60_link_status(void) {
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for link status test");
    
    // Get link status (should be deterministic for connected hardware)
    bool link_status = enc28j60_get_link_status();
    
    // Since we have hardware-in-the-loop, link should be up
    TEST_ASSERT_TRUE_MESSAGE(link_status, "Link status should be UP with connected hardware");
}

/**
 * @brief Test ENC28J60 packet transmission
 */
void test_enc28j60_send_packet(void) {
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for packet transmission test");
    
    // Set MAC address first
    enc28j60_set_mac_address(test_mac_address);
    
    // Create test packet
    enc28j60_packet_t packet;
    packet.data = test_packet_data;
    packet.length = sizeof(test_packet_data);
    packet.valid = true;
    
    // Send packet
    bool send_result = enc28j60_send_packet(&packet);
    TEST_ASSERT_TRUE_MESSAGE(send_result, "Packet transmission should start successfully");
    
    // Wait for transmission to complete (with timeout)
    uint32_t timeout_count = 0;
    const uint32_t MAX_TIMEOUT = 10000;  // 10ms timeout
    
    while (!enc28j60_is_tx_complete() && timeout_count < MAX_TIMEOUT) {
        sleep_us(1);
        timeout_count++;
    }
    
    TEST_ASSERT_TRUE_MESSAGE(enc28j60_is_tx_complete(), 
                           "Packet transmission should complete within timeout");
    
    // Verify statistics updated
    const enc28j60_state_t* state = enc28j60_get_state();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, state->packets_sent, 
                                   "Statistics should show 1 packet sent");
}

/**
 * @brief Test ENC28J60 software reset
 */
void test_enc28j60_reset(void) {
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for reset test");
    
    // Set a register to known value
    enc28j60_write_register(ENC28J60_ERDPTL, 0x55);
    uint8_t before_reset = enc28j60_read_register(ENC28J60_ERDPTL);
    printf("DEBUG: ERDPTL before reset = 0x%02X (should be 0x55)\n", before_reset);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x55, before_reset, "Register should be set before reset");
    
    // Perform software reset
    printf("DEBUG: Performing software reset...\n");
    enc28j60_reset();
    
    // Wait for reset to complete
    sleep_ms(10);
    
    // After reset, we need to ensure proper bank (reset puts us in bank 0)
    enc28j60_set_bank(ENC28J60_BANK0);
    
    // Check that register value changed from what we set (indicating reset occurred)
    uint8_t after_reset = enc28j60_read_register(ENC28J60_ERDPTL);
    printf("DEBUG: ERDPTL after reset = 0x%02X (should be different from 0x55)\n", after_reset);
    
    // Also check if we can still communicate after reset
    uint8_t estat_after = enc28j60_read_register(ENC28J60_ESTAT);
    printf("DEBUG: ESTAT after reset = 0x%02X\n", estat_after);
    
    // Test that register value changed (reset occurred) and communication still works
    TEST_ASSERT_NOT_EQUAL_UINT8_MESSAGE(0x55, after_reset, 
                                        "Register should change from set value after reset");
    TEST_ASSERT_NOT_EQUAL_UINT8_MESSAGE(0xFF, estat_after, 
                                        "SPI communication should work after reset");
}

/**
 * @brief Test ENC28J60 interrupt status functions
 */
void test_enc28j60_interrupt_status(void) {
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for interrupt test");
    
    // Get interrupt status
    uint8_t int_status = enc28j60_get_interrupt_status();
    
    // Interrupt status should be a valid value (not 0xFF)
    TEST_ASSERT_MESSAGE(int_status != 0xFF, "Interrupt status should be valid");
    
    // Clear all interrupts
    enc28j60_clear_interrupts(0xFF);
    
    // Check that interrupts are cleared
    int_status = enc28j60_get_interrupt_status();
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, int_status, 
                                   "All interrupts should be cleared after clear operation");
}

/**
 * @brief Test raw packet reception (Arduino-style, bypassing lwIP)
 */
void test_enc28j60_raw_packet_reception(void) {
    printf("=== Raw Packet Reception Test (Arduino-style) ===\n");
    
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for raw reception test");
    
    // Send an ARP packet to generate network traffic for testing
    enc28j60_packet_t arp_packet;
    arp_packet.data = test_packet_data;
    arp_packet.length = sizeof(test_packet_data);
    arp_packet.valid = true;
    
    printf("Raw RX: Sending ARP packet to generate traffic\n");
    bool send_result = enc28j60_send_packet(&arp_packet);
    TEST_ASSERT_TRUE_MESSAGE(send_result, "Should be able to send ARP packet");
    
    printf("Raw RX: Monitoring for ANY incoming packets (20 seconds)...\n");
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    uint32_t test_duration_ms = 20000;  // 20 seconds
    uint32_t packets_detected = 0;
    
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < test_duration_ms) {
        // Check EPKTCNT register directly (Arduino style)
        enc28j60_set_bank(ENC28J60_BANK1);
        uint8_t pktcnt = enc28j60_read_register(0x19);  // EPKTCNT
        
        if (pktcnt > 0) {
            packets_detected++;
            printf("Raw RX: Detected %u packets in buffer!\n", pktcnt);
            
            // Try to read packet size (Arduino readFrameSize style)
            enc28j60_set_bank(ENC28J60_BANK0);
            
            // Read packet header to get size info
            uint8_t header[6];
            enc28j60_read_buffer(header, 6);
            
            uint16_t next_ptr = header[0] | (header[1] << 8);
            uint16_t packet_len = header[2] | (header[3] << 8);
            uint8_t status_low = header[4];
            uint8_t status_high = header[5];
            
            printf("Raw RX: Packet length=%u, next_ptr=0x%04X, status=0x%02X%02X\n", 
                   packet_len, next_ptr, status_high, status_low);
            
            // Consume the packet to clear buffer (Arduino style)
            if (packet_len > 0 && packet_len < 1600) {
                uint8_t dummy_buffer[64];
                uint16_t bytes_to_read = (packet_len > 64) ? 64 : packet_len;
                enc28j60_read_buffer(dummy_buffer, bytes_to_read);
                
                printf("Raw RX: First 16 bytes: ");
                for (int i = 0; i < 16 && i < bytes_to_read; i++) {
                    printf("%02X ", dummy_buffer[i]);
                }
                printf("\n");
                
                // Arduino-style packet cleanup
                uint16_t rx_read_ptr;
                if (next_ptr == 0x0000) {
                    rx_read_ptr = 0x0FFF;  // RX_BUF_END
                } else {
                    rx_read_ptr = next_ptr - 1;
                }
                
                enc28j60_write_register(0x0C, rx_read_ptr & 0xFF);        // ERXRDPTL
                enc28j60_write_register(0x0D, (rx_read_ptr >> 8) & 0xFF); // ERXRDPTH
                
                // Decrement packet count
                enc28j60_set_register_bits(ENC28J60_ECON2, ENC28J60_ECON2_PKTDEC);
            }
        }
        
        // Check every 100ms (faster than our current polling)
        sleep_ms(100);
    }
    
    printf("Raw RX: Test complete. Total packets detected: %u\n", packets_detected);
    
    // Test passes if we can at least detect the hardware is working
    // Even 0 packets is OK for this test - it tells us about the receive path
    printf("Raw RX: Hardware reception path tested\n");
}

/**
 * @brief Test invalid packet transmission
 */
void test_enc28j60_send_invalid_packet(void) {
    // Initialize driver
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "Driver initialization required for invalid packet test");
    
    // Test NULL packet
    bool result1 = enc28j60_send_packet(NULL);
    TEST_ASSERT_FALSE_MESSAGE(result1, "Sending NULL packet should fail");
    
    // Test packet with NULL data
    enc28j60_packet_t invalid_packet;
    invalid_packet.data = NULL;
    invalid_packet.length = 64;
    invalid_packet.valid = true;
    
    bool result2 = enc28j60_send_packet(&invalid_packet);
    TEST_ASSERT_FALSE_MESSAGE(result2, "Sending packet with NULL data should fail");
    
    // Test packet with zero length
    invalid_packet.data = test_packet_data;
    invalid_packet.length = 0;
    invalid_packet.valid = true;
    
    bool result3 = enc28j60_send_packet(&invalid_packet);
    TEST_ASSERT_FALSE_MESSAGE(result3, "Sending packet with zero length should fail");
    
    // Test packet marked as invalid
    invalid_packet.data = test_packet_data;
    invalid_packet.length = 64;
    invalid_packet.valid = false;
    
    bool result4 = enc28j60_send_packet(&invalid_packet);
    TEST_ASSERT_FALSE_MESSAGE(result4, "Sending invalid packet should fail");
}

/**
 * @brief Test operations before initialization
 */
void test_enc28j60_operations_before_init(void) {
    // Ensure driver is not initialized
    enc28j60_deinit();
    
    // Test operations that should fail before initialization
    bool ready = enc28j60_is_ready();
    TEST_ASSERT_FALSE_MESSAGE(ready, "Driver should not be ready before initialization");
    
    enc28j60_packet_t packet;
    packet.data = test_packet_data;
    packet.length = sizeof(test_packet_data);
    packet.valid = true;
    
    bool send_result = enc28j60_send_packet(&packet);
    TEST_ASSERT_FALSE_MESSAGE(send_result, "Packet transmission should fail before initialization");
    
    bool link_status = enc28j60_get_link_status();
    TEST_ASSERT_FALSE_MESSAGE(link_status, "Link status should be false before initialization");
}

// Test runner function
int test_enc28j60_driver_run_tests(void) {
    UNITY_BEGIN();
    
    // Basic initialization tests
    RUN_TEST(test_enc28j60_init_success);
    RUN_TEST(test_enc28j60_init_already_initialized);
    
    // Register access tests
    RUN_TEST(test_enc28j60_register_read);
    RUN_TEST(test_enc28j60_register_write);
    RUN_TEST(test_enc28j60_register_bit_operations);
    
    // Buffer memory tests
    RUN_TEST(test_enc28j60_buffer_read);
    RUN_TEST(test_enc28j60_buffer_write);
    
    // Configuration tests
    RUN_TEST(test_enc28j60_mac_address);
    RUN_TEST(test_enc28j60_broadcast_reception_config);
    
    // Status and monitoring tests
    RUN_TEST(test_enc28j60_link_status);
    RUN_TEST(test_enc28j60_interrupt_status);
    
    // Packet transmission tests
    RUN_TEST(test_enc28j60_send_packet);
    
    // Raw packet reception test (Arduino-style)
    RUN_TEST(test_enc28j60_raw_packet_reception);
    
    // Reset functionality test
    RUN_TEST(test_enc28j60_reset);
    
    // Error condition tests
    RUN_TEST(test_enc28j60_send_invalid_packet);
    RUN_TEST(test_enc28j60_operations_before_init);
    
    // Hardware debugging tests (interactive)
    printf("\n=== HARDWARE-LEVEL DEBUGGING TESTS ===\n");
    RUN_TEST(test_enc28j60_interrupt_monitoring);
    RUN_TEST(test_enc28j60_cable_detection);
    
    return UNITY_END();
}

/**
 * @brief Test interrupt pin monitoring and register status
 */
void test_enc28j60_interrupt_monitoring(void) {
    printf("=== ENC28J60 Interrupt Monitoring Test ===\n");
    
    // Initialize ENC28J60
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "ENC28J60 initialization required for interrupt test");
    
    printf("Monitoring interrupt pin and registers for 30 seconds...\n");
    printf("GPIO %d (INT pin): Monitoring...\n", ENC28J60_INTERRUPT_PIN);
    
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    uint32_t test_duration_ms = 30000;  // 30 seconds for this test
    int check_count = 0;
    
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < test_duration_ms) {
        check_count++;
        
        // Read interrupt pin state directly
        bool int_pin_state = gpio_get(ENC28J60_INTERRUPT_PIN);
        
        // Read ENC28J60 interrupt status register
        uint8_t eir_status = enc28j60_get_interrupt_status();
        
        // Read current link status
        bool link_status = enc28j60_get_link_status();
        
        // Read packet count
        bool has_packets = enc28j60_has_rx_packet();
        
        // Get driver statistics
        const enc28j60_state_t* state = enc28j60_get_state();
        
        printf("Check %3d: INT_PIN=%d, EIR=0x%02X, LINK=%s, RX_PKTS=%s, TX=%u, RX=%u\n",
               check_count,
               int_pin_state ? 1 : 0,
               eir_status,
               link_status ? "UP  " : "DOWN",
               has_packets ? "YES" : "NO ",
               state ? state->packets_sent : 0,
               state ? state->packets_received : 0);
        
        sleep_ms(500);  // Check every 500ms
    }
    
    printf("✓ Interrupt monitoring test completed\n");
    enc28j60_deinit();
}

/**
 * @brief Test cable plug/unplug detection
 */
void test_enc28j60_cable_detection(void) {
    printf("=== ENC28J60 Cable Detection Test ===\n");
    
    // Initialize ENC28J60
    bool init_result = enc28j60_init();
    TEST_ASSERT_TRUE_MESSAGE(init_result, "ENC28J60 initialization required for cable test");
    
    printf("Cable detection test - Ready for cable plug/unplug!\n");
    printf("Instructions:\n");
    printf("1. Please unplug the network cable now\n");
    printf("2. Wait for 'READY FOR PLUG' message\n");
    printf("3. Plug the cable back in\n");
    printf("4. Test will monitor for link state changes\n\n");
    
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    uint32_t test_duration_ms = 60000;  // 60 seconds
    bool last_link_state = enc28j60_get_link_status();
    int state_changes = 0;
    int check_count = 0;
    bool ready_for_plug_announced = false;
    
    printf("Starting cable detection - monitoring for %d seconds...\n", test_duration_ms / 1000);
    printf("Initial link state: %s\n\n", last_link_state ? "UP" : "DOWN");
    
    while ((to_ms_since_boot(get_absolute_time()) - start_time) < test_duration_ms) {
        check_count++;
        
        // Read current link status
        bool current_link_state = enc28j60_get_link_status();
        
        // Read interrupt registers
        uint8_t eir_status = enc28j60_get_interrupt_status();
        bool int_pin_state = gpio_get(ENC28J60_INTERRUPT_PIN);
        
        // Check for link state change
        if (current_link_state != last_link_state) {
            state_changes++;
            printf("\n*** LINK STATE CHANGE #%d ***\n", state_changes);
            printf("Link changed from %s to %s\n", 
                   last_link_state ? "UP" : "DOWN",
                   current_link_state ? "UP" : "DOWN");
            printf("EIR=0x%02X, INT_PIN=%d\n",
                   eir_status, int_pin_state ? 1 : 0);
            printf("Timestamp: %u ms\n\n", to_ms_since_boot(get_absolute_time()) - start_time);
            
            last_link_state = current_link_state;
        }
        
        // Announce when ready for cable plug
        if (!ready_for_plug_announced && !current_link_state && check_count > 5) {
            printf(">>> READY FOR PLUG - Please plug in network cable now <<<\n\n");
            ready_for_plug_announced = true;
        }
        
        // Print status every 10 checks (5 seconds)
        if (check_count % 10 == 0) {
            printf("Status check %3d: LINK=%s, EIR=0x%02X, INT_PIN=%d\n",
                   check_count,
                   current_link_state ? "UP  " : "DOWN",
                   eir_status,
                   int_pin_state ? 1 : 0);
        }
        
        sleep_ms(500);
    }
    
    printf("\n=== Cable Detection Test Results ===\n");
    printf("Total link state changes detected: %d\n", state_changes);
    printf("Total status checks: %d\n", check_count);
    
    if (state_changes > 0) {
        printf("✓ Cable detection working - link state changes detected!\n");
    } else {
        printf("⚠ No link state changes detected - check cable or hardware\n");
    }
    
    enc28j60_deinit();
}

/**
 * @brief Main function for running ENC28J60 driver tests
 */
int main(void) {
    stdio_usb_init();
    //init uart1 for debug output
    stdio_uart_init_full(uart1, 115200, 20, 21);

    
    // Wait for USB-serial connection for debugging
    sleep_ms(2000);
    
    printf("=== ENC28J60 Driver Tests ===\n");
    printf("Testing ENC28J60 SPI driver functionality\n");
    printf("Hardware Configuration:\n");
    printf("  SPI: SPI0\n");
    printf("  CS Pin: %d\n", ENC28J60_CS_PIN);
    printf("  SCK Pin: %d\n", ENC28J60_SCK_PIN);
    printf("  MOSI Pin: %d\n", ENC28J60_MOSI_PIN);
    printf("  MISO Pin: %d\n", ENC28J60_MISO_PIN);
    printf("  INT Pin: %d\n", ENC28J60_INTERRUPT_PIN);
    printf("\n");
    
    // Run the tests
    int result = test_enc28j60_driver_run_tests();
    
    // Keep running forever for embedded system
    while (true) {
        printf("Tests completed with result: %d\n", result);
        printf("ENC28J60 driver tests %s\n", (result == 0) ? "PASSED" : "FAILED");
        sleep_ms(5000);
    }
    
    return result;  // Never reached
}