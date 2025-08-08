/**
 * @file enc28j60_driver.h
 * @brief ENC28J60 Ethernet Controller SPI Driver for RP2350
 * 
 * Provides low-level SPI communication with the ENC28J60 10BASE-T 
 * Ethernet controller. Handles register access, buffer management,
 * and packet transmission/reception.
 * 
 * Hardware Configuration:
 * - SPI0 interface
 * - Interrupt pin for event notifications
 * - CS pin for SPI chip select
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Core 1 Network Subsystem  
 * - ADR-002: Ethernet Controller Selection
 */

#ifndef ENC28J60_DRIVER_H
#define ENC28J60_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Hardware pin configuration
#define ENC28J60_INTERRUPT_PIN  8
#define ENC28J60_SCK_PIN        2
#define ENC28J60_MOSI_PIN       3   // SI - Serial Input
#define ENC28J60_MISO_PIN       4   // SO - Serial Output  
#define ENC28J60_CS_PIN         5

// ENC28J60 Register definitions
#define ENC28J60_ERDPTL         0x00
#define ENC28J60_ERDPTH         0x01
#define ENC28J60_EWRPTL         0x02
#define ENC28J60_EWRPTH         0x03
#define ENC28J60_ETXSTL         0x04
#define ENC28J60_ETXSTH         0x05
#define ENC28J60_ETXNDL         0x06
#define ENC28J60_ETXNDH         0x07
#define ENC28J60_ERXSTL         0x08
#define ENC28J60_ERXSTH         0x09
#define ENC28J60_ERXNDL         0x0A
#define ENC28J60_ERXNDH         0x0B
#define ENC28J60_ERXRDPTL       0x0C
#define ENC28J60_ERXRDPTH       0x0D

// Control registers
#define ENC28J60_ECON1          0x1F
#define ENC28J60_ECON2          0x1E
#define ENC28J60_ESTAT          0x1D
#define ENC28J60_EIR            0x1C
#define ENC28J60_EIE            0x1B

// ECON1 bits
#define ENC28J60_ECON1_TXRTS    0x08
#define ENC28J60_ECON1_RXEN     0x04
#define ENC28J60_ECON1_TXRST    0x80
#define ENC28J60_ECON1_RXRST    0x40

// ESTAT bits
#define ENC28J60_ESTAT_CLKRDY   0x01

// SPI command opcodes
#define ENC28J60_READ_CTRL_REG  0x00
#define ENC28J60_READ_BUF_MEM   0x3A
#define ENC28J60_WRITE_CTRL_REG 0x40
#define ENC28J60_WRITE_BUF_MEM  0x7A
#define ENC28J60_BIT_FIELD_SET  0x80
#define ENC28J60_BIT_FIELD_CLR  0xA0
#define ENC28J60_SOFT_RESET     0xFF

// Buffer size configuration
#define ENC28J60_BUFFER_SIZE    8192    // 8KB total buffer
#define ENC28J60_RX_BUFFER_SIZE 6144    // 6KB for RX
#define ENC28J60_TX_BUFFER_SIZE 2048    // 2KB for TX

// Maximum Ethernet frame size
#define ENC28J60_MAX_FRAME_SIZE 1518

/**
 * @brief ENC28J60 driver state structure
 */
typedef struct {
    bool initialized;           // Driver initialization status
    uint16_t next_packet_ptr;   // Next packet pointer for RX
    uint32_t packets_sent;      // Statistics: transmitted packets
    uint32_t packets_received;  // Statistics: received packets  
    uint32_t tx_errors;         // Statistics: transmission errors
    uint32_t rx_errors;         // Statistics: reception errors
} enc28j60_state_t;

/**
 * @brief Ethernet packet structure for transmission/reception
 */
typedef struct {
    uint8_t* data;              // Packet data buffer
    uint16_t length;            // Packet length in bytes
    bool valid;                 // Packet validity flag
} enc28j60_packet_t;

/**
 * @brief Driver initialization and control functions
 */

/**
 * Initialize ENC28J60 driver and SPI interface
 * @return true if initialization successful, false otherwise
 */
bool enc28j60_init(void);

/**
 * Deinitialize ENC28J60 driver and SPI interface
 */
void enc28j60_deinit(void);

/**
 * Check if ENC28J60 is ready for operation
 * @return true if ready, false otherwise
 */
bool enc28j60_is_ready(void);

/**
 * Get driver state for diagnostics
 * @return pointer to driver state structure
 */
const enc28j60_state_t* enc28j60_get_state(void);

/**
 * @brief Low-level register access functions
 */

/**
 * Read control register
 * @param reg Register address
 * @return Register value
 */
uint8_t enc28j60_read_register(uint8_t reg);

/**
 * Write control register  
 * @param reg Register address
 * @param value Value to write
 */
void enc28j60_write_register(uint8_t reg, uint8_t value);

/**
 * Set bits in control register
 * @param reg Register address
 * @param mask Bit mask to set
 */
void enc28j60_set_register_bits(uint8_t reg, uint8_t mask);

/**
 * Clear bits in control register
 * @param reg Register address
 * @param mask Bit mask to clear
 */
void enc28j60_clear_register_bits(uint8_t reg, uint8_t mask);

/**
 * @brief Buffer memory access functions
 */

/**
 * Read data from buffer memory
 * @param buffer Output buffer
 * @param length Number of bytes to read
 */
void enc28j60_read_buffer(uint8_t* buffer, uint16_t length);

/**
 * Write data to buffer memory
 * @param buffer Input buffer
 * @param length Number of bytes to write
 */
void enc28j60_write_buffer(const uint8_t* buffer, uint16_t length);

/**
 * @brief Packet transmission and reception functions
 */

/**
 * Send Ethernet packet
 * @param packet Packet to transmit
 * @return true if transmission started successfully, false otherwise
 */
bool enc28j60_send_packet(const enc28j60_packet_t* packet);

/**
 * Check if packet transmission is complete
 * @return true if transmission complete, false if still in progress
 */
bool enc28j60_is_tx_complete(void);

/**
 * Check if received packet is available
 * @return true if packet available, false otherwise
 */
bool enc28j60_has_rx_packet(void);

/**
 * Receive Ethernet packet
 * @param packet Output packet structure (data buffer must be allocated)
 * @param max_length Maximum buffer size
 * @return true if packet received successfully, false otherwise
 */
bool enc28j60_receive_packet(enc28j60_packet_t* packet, uint16_t max_length);

/**
 * @brief Interrupt and event handling
 */

/**
 * Check interrupt status
 * @return Interrupt status register value
 */
uint8_t enc28j60_get_interrupt_status(void);

/**
 * Clear interrupt flags
 * @param flags Interrupt flags to clear
 */
void enc28j60_clear_interrupts(uint8_t flags);

/**
 * @brief Utility and diagnostic functions
 */

/**
 * Perform software reset of ENC28J60
 */
void enc28j60_reset(void);

/**
 * Get link status
 * @return true if link is up, false if link is down
 */
bool enc28j60_get_link_status(void);

/**
 * Set MAC address
 * @param mac_addr 6-byte MAC address
 */
void enc28j60_set_mac_address(const uint8_t mac_addr[6]);

/**
 * Get MAC address
 * @param mac_addr Output buffer for 6-byte MAC address
 */
void enc28j60_get_mac_address(uint8_t mac_addr[6]);

#ifdef __cplusplus
}
#endif

#endif // ENC28J60_DRIVER_H