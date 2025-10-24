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
#define ENC28J60_RESET_PIN      12 
#define ENC28J60_INTERRUPT_PIN  13
#define ENC28J60_SCK_PIN        10 
#define ENC28J60_MOSI_PIN       11    
#define ENC28J60_MISO_PIN       8      
#define ENC28J60_CS_PIN         9 

// Register Bank definitions
#define ENC28J60_BANK0          0x00
#define ENC28J60_BANK1          0x01
#define ENC28J60_BANK2          0x02
#define ENC28J60_BANK3          0x03

// Bank 0 registers
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

// Bank 1 registers
#define ENC28J60_ERXFCON        0x18
#define ENC28J60_EPKTCNT        0x19

// ERXFCON bits (from Arduino reference)
#define ENC28J60_ERXFCON_UCEN   0x80  // Unicast enable
#define ENC28J60_ERXFCON_ANDOR  0x40  // AND/OR filter logic
#define ENC28J60_ERXFCON_CRCEN  0x20  // CRC check enable  
#define ENC28J60_ERXFCON_MCEN   0x02  // Multicast enable
#define ENC28J60_ERXFCON_BCEN   0x01  // Broadcast enable

// Bank 2 registers (MAC/MII - require dummy byte)
#define ENC28J60_MACON1         0x00
#define ENC28J60_MACON3         0x02
#define ENC28J60_MACON4         0x03
#define ENC28J60_MABBIPG        0x04
#define ENC28J60_MAIPGL         0x06
#define ENC28J60_MAIPGH         0x07
#define ENC28J60_MAMXFLL        0x0A
#define ENC28J60_MAMXFLH        0x0B
#define ENC28J60_MICMD          0x12
#define ENC28J60_MIREGADR       0x14
#define ENC28J60_MIRDL          0x18
#define ENC28J60_MIRDH          0x19

// Bank 3 registers (MAC/MII - require dummy byte)
#define ENC28J60_MAADR1         0x04  // MAADR<47:40>
#define ENC28J60_MAADR2         0x05  // MAADR<39:32>
#define ENC28J60_MAADR3         0x02  // MAADR<31:24>
#define ENC28J60_MAADR4         0x03  // MAADR<23:16>
#define ENC28J60_MAADR5         0x00  // MAADR<15:8>
#define ENC28J60_MAADR6         0x01  // MAADR<7:0>
#define ENC28J60_MISTAT         0x0A
#define ENC28J60_EREVID         0x12

// Control registers (available in all banks)
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

// ECON2 bits
#define ENC28J60_ECON2_AUTOINC  0x80
#define ENC28J60_ECON2_PKTDEC   0x40

// ESTAT bits
#define ENC28J60_ESTAT_CLKRDY   0x01  // Clock ready
#define ENC28J60_ESTAT_TXABRT   0x02  // Transmit abort

// EIE (Ethernet Interrupt Enable) bits
#define ENC28J60_EIE_INTIE      0x80  // Global interrupt enable
#define ENC28J60_EIE_PKTIE      0x40  // Receive packet pending interrupt enable
#define ENC28J60_EIE_DMAIE      0x20  // DMA interrupt enable
#define ENC28J60_EIE_LINKIE     0x10  // Link status change interrupt enable
#define ENC28J60_EIE_TXIE       0x08  // Transmit interrupt enable
#define ENC28J60_EIE_WOLIE      0x04  // Wake-on-LAN interrupt enable (Rev. B only)
#define ENC28J60_EIE_TXERIE     0x02  // Transmit error interrupt enable
#define ENC28J60_EIE_RXERIE     0x01  // Receive error interrupt enable

// EIR (Ethernet Interrupt) flags - same bit positions as EIE
#define ENC28J60_EIR_PKTIF      0x40  // Receive packet pending interrupt flag
#define ENC28J60_EIR_DMAIF      0x20  // DMA interrupt flag
#define ENC28J60_EIR_LINKIF     0x10  // Link status change interrupt flag
#define ENC28J60_EIR_TXIF       0x08  // Transmit interrupt flag
#define ENC28J60_EIR_WOLIF      0x04  // Wake-on-LAN interrupt flag (Rev. B only)
#define ENC28J60_EIR_TXERIF     0x02  // Transmit error interrupt flag
#define ENC28J60_EIR_RXERIF     0x01  // Receive error interrupt flag

// MAC register bits
#define ENC28J60_MACON1_MARXEN  0x01
#define ENC28J60_MACON1_PASSALL 0x02
#define ENC28J60_MACON1_RXPAUS  0x04
#define ENC28J60_MACON1_TXPAUS  0x08

#define ENC28J60_MACON3_FULDPX  0x01
#define ENC28J60_MACON3_FRMLNEN 0x02
#define ENC28J60_MACON3_TXCRCEN 0x10
#define ENC28J60_MACON3_PADCFG0 0x20
#define ENC28J60_MACON3_PADCFG1 0x40
#define ENC28J60_MACON3_PADCFG2 0x80
#define ENC28J60_MACON3_PADCFG_FULL (ENC28J60_MACON3_PADCFG2 | ENC28J60_MACON3_PADCFG1 | ENC28J60_MACON3_PADCFG0)  // Auto-pad to 60 bytes

// MII/MAC additional register addresses and bits
#define ENC28J60_MACON2         0x10
#define ENC28J60_MACSTAT1       0x01
#define ENC28J60_MACSTAT2       0x11

// PHY register addresses
#define ENC28J60_PHCON1         0x00
#define ENC28J60_PHSTAT1        0x01
#define ENC28J60_PHCON2         0x10
#define ENC28J60_PHSTAT2        0x11

// MICMD (MII Command) register bits
#define ENC28J60_MICMD_MIISCAN  0x02  // MII Scan enable
#define ENC28J60_MICMD_MIIRD    0x01  // MII Read enable

// MISTAT (MII Status) register bits  
#define ENC28J60_MISTAT_BUSY    0x01  // MII Management Busy

// PHY Status register 2 (PHSTAT2) bits
#define ENC28J60_PHSTAT2_LLSTAT  (1<<2)  // Link Status (bit 2)
#define ENC28J60_PHSTAT2_JBSTAT  (1<<1)  // Link Status (bit 1)
#define ENC28J60_PHSTAT2_TXSTAT  (1<<13)  // Link Status (bit 13)
#define ENC28J60_PHSTAT2_RXSTAT  (1<<12)  // Link Status (bit 12)
#define ENC28J60_PHSTAT2_COLSTAT  (1<<11)  // Link Status (bit 11)
#define ENC28J60_PHSTAT2_LSTAT  (1<<10)  // Link Status (bit 13)
#define ENC28J60_PHSTAT2_DPXSTAT  (1<<9)  // Link Status (bit 9)
#define ENC28J60_PHSTAT2_PLRITY  (1<<5)  // Link Status (bit 5)



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
#define ENC28J60_RX_BUFFER_SIZE 4096    // 4KB for RX
#define ENC28J60_TX_BUFFER_SIZE 4096    // 4KB for TX

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
    uint32_t likely_false_collisions;   //Errata#15 collision false positives
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
 * Set register bank
 * @param bank Bank number (0-3)
 */
void enc28j60_set_bank(uint8_t bank);

/**
 * Read control register (with automatic bank switching)
 * @param reg Register address
 * @param bank Bank number (0-3)
 * @return Register value
 */
uint8_t enc28j60_read_register_bank(uint8_t reg, uint8_t bank);

/**
 * Write control register (with automatic bank switching)
 * @param reg Register address
 * @param bank Bank number (0-3)
 * @param value Value to write
 */
void enc28j60_write_register_bank(uint8_t reg, uint8_t bank, uint8_t value);

/**
 * Read control register (current bank)
 * @param reg Register address
 * @return Register value
 */
uint8_t enc28j60_read_register(uint8_t reg);

/**
 * Write control register (current bank)
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
 * @return number of rx packet available
 */
uint8_t enc28j60_has_rx_packet(void);

/**
 * Check if the LINKIF flag is set
 * @return 
 */
bool enc28j60_has_link_change_pending(void);

/**
 * Check if the TXIF flag is set
 * @return 
 */
bool enc28j60_has_txif_pending(void);

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
 * Process pending interrupts
 * @return true if interrupts were processed, false if none pending
 */
bool enc28j60_process_interrupts(bool forced);

/**
 * Check if interrupt is pending
 * @return true if interrupt is pending, false otherwise  
 */
bool enc28j60_has_pending_interrupt(void);

/**
 * @brief Utility and diagnostic functions
 */

/**
 * Perform software reset of ENC28J60
 */
void enc28j60_reset(void);

uint32_t get_interrupt_ms(void);

/**
 * Get link status
 * @return true if link is up, false if link is down
 */
bool enc28j60_get_link_status(void);

/**
 * Gets link status , clears pending interrupt bit
 * @return true if link is up, false if link is down
 */
bool enc28j60_process_linkif_interrupt(void);

/**
 * Gets txif status , clears pending interrupt, also txerif bit
 * @return true 
 */
bool enc28j60_clear_tx_interrupt_flags(void);

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



/**
 * @brief Dump all ENC28J60 registers relevant for signal quality analysis
 * 
 * This function outputs comprehensive register information to help diagnose
 * signal quality issues, packet loss, and communication problems.
 * 
 * The function examines:
 * - Driver statistics (TX/RX errors, packet counts)
 * - Control and status registers (ECON1/2, ESTAT, EIR, EIE)
 * - Buffer control registers (ERXST/ND/RDPT, ETXST/ND, EWRPT)
 * - Packet count and filter control (EPKTCNT, ERXFCON)
 * - MAC control registers (MACON1/3, MAMXFL, etc.)
 * - MAC address and chip revision info
 * - PHY registers (PHCON1/2, PHSTAT1/2) - critical for signal quality
 * - Analysis of common errata-related issues
 * 
 * Call this function when experiencing:
 * - Packet loss or corruption
 * - Intermittent communication failures
 * - Link stability issues
 * - Suspected signal integrity problems
 * 
 * Requires: ENC28J60 driver must be initialized (enc28j60_init() called)
 * 
 * @note This function temporarily changes register banks but restores the
 *       original bank when complete.
 * @note Output goes to printf() - redirect stdout as needed for logging.
 */
void enc28j60_dump_signal_quality_registers(void);


#ifdef __cplusplus
}
#endif

#endif // ENC28J60_DRIVER_H