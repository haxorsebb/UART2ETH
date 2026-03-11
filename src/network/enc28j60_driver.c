/**
 * @file enc28j60_driver.c
 * @brief ENC28J60 Ethernet Controller SPI Driver Implementation
 * 
 * Refactored implementation based on verified Arduino reference code.
 * This implementation closely follows the working Arduino patterns
 * while adapting them for the RP2350 platform.
 * 
 * Key Arduino Patterns Implemented:
 * - Exact bank switching logic from Arduino reference
 * - Precise MAC/MII register detection for dummy byte handling
 * - Proper SPI timing and chip select management
 * - Correct initialization sequence order
 * - Accurate register access patterns
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 * - ENC28J60 Datasheet Rev. B7
 * - Arduino ENC28J60 library (verified working implementation)
 */

#include "debug.h"
#include "network/enc28j60_driver.h"
#include "utils/selftest.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "log_manager.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "core1_timer.h"

#include "pico/multicore.h"


// Driver state (Arduino-style state management)
static enc28j60_state_t g_enc28j60_state = {0};
static bool g_driver_initialized = false;
static uint8_t g_current_bank = 0xFF; // Track current register bank (Arduino pattern)
static const uint8_t* g_local_mac = NULL; // MAC address storage (Arduino pattern)

// SPI configuration (RP2350-specific)
#define SPI_PORT spi1
#define SPI_BAUDRATE 20000000  // 20MHz SPI clock (max for ENC28J60)

// Buffer addresses (shamelessly lifted from linux kernel)
#define TX_BUF_START	0x1A00
#define TX_BUF_END		0x1FFF
#define RX_BUF_START	0x0000
#define RX_BUF_END		0x19FF

// Maximum frame length (Arduino reference)
#define MAX_MAC_LENGTH 1518

// Bank definitions (Arduino reference)
#define ERXTX_BANK   0x00  // Bank 0 - Buffer control registers
#define EPKTCNT_BANK 0x01  // Bank 1 - Packet count and filter control
#define MACONX_BANK  0x02  // Bank 2 - MAC control registers
#define MAADRX_BANK  0x03  // Bank 3 - MAC address registers

// Interrupt handling state
static volatile bool g_interrupt_pending = false;
static volatile uint32_t g_interrupt_time = 0 ;
        
static volatile uint8_t g_last_interrupt_status = 0;
static volatile bool g_is_ready = false;


// Private function declarations
static void enc28j60_spi_init(void);
static void enc28j60_gpio_init(void);
static uint8_t enc28j60_spi_transfer(uint8_t data);
static void enc28j60_arch_spi_select(void);
static void enc28j60_arch_spi_deselect(void);
static bool enc28j60_wait_for_osc_ready(void);
static void enc28j60_configure_buffers(void);
static void enc28j60_configure_mac(void);
static void enc28j60_configure_phy(void);
static void enc28j60_enable_interrupts(void);
static void enc28j60_block_interrupt(void);
static void enc28j60_unblock_interrupt(void);
static void enc28j60_interrupt_handler(uint gpio, uint32_t events);
static void enc28j60_clear_interrupt_pending(void);
static bool enc28j60_is_mac_mii_register(uint8_t reg);
static uint8_t enc28j60_read_register_internal(uint8_t reg);
static void enc28j60_write_register_internal(uint8_t reg, uint8_t value);
static void enc28j60_set_register_bank_internal(uint8_t new_bank);
static uint16_t enc28j60_read_phy_register(uint8_t phy_reg);
static void enc28j60_write_phy_register(uint8_t phy_reg, uint16_t value);
/**
 * @brief reset tx after timeout
 */
static void enc28j60_reset_tx_logic(void);

/**
 * Check if ENC28J60 is ready for operation
 * @return true if ready, false otherwise
 */
bool enc28j60_is_ready(void)
{
    return g_is_ready;
}
/**
 * @brief Check if register is MAC or MII type (Arduino reference exact logic)
 */
static bool enc28j60_is_mac_mii_register(uint8_t reg) {
    /* MAC or MII register (otherwise, ETH register)? */
    /* Arduino reference implementation exactly */
    switch (g_current_bank) {
        case MACONX_BANK:  // Bank 2
            return reg < ENC28J60_EIE;
        case MAADRX_BANK:  // Bank 3
            return reg <= ENC28J60_MAADR2 || reg == ENC28J60_MISTAT; // MAADR2 is 0x05
        case ERXTX_BANK:   // Bank 0
        case EPKTCNT_BANK: // Bank 1
        default:
            return false;
    }
}

/**
 * @brief Set register bank (Arduino reference exact implementation)
 */
static void enc28j60_set_register_bank_internal(uint8_t new_bank) {
    if (new_bank > 3) {
        return; // Invalid bank
    }
    
    if (g_current_bank != new_bank) {
        // Arduino reference: direct ECON1 manipulation with proper masking
        uint8_t econ1 = enc28j60_read_register_internal(ENC28J60_ECON1);
        econ1 = (econ1 & 0xFC) | (new_bank & 0x03);
        enc28j60_write_register_internal(ENC28J60_ECON1, econ1);
        g_current_bank = new_bank;
    }
}

/**
 * @brief Read control register (Arduino reference exact timing and logic)
 */
static uint8_t enc28j60_read_register_internal(uint8_t reg) {
    uint8_t result;
    
    enc28j60_arch_spi_select();
    
    // Send read command (Arduino reference)
    enc28j60_spi_transfer(ENC28J60_READ_CTRL_REG | (reg & 0x1F));
    
    // For MAC/MII registers, send dummy byte first (Arduino reference)
    if (enc28j60_is_mac_mii_register(reg)) {
        enc28j60_spi_transfer(0x00);  // Dummy byte
    }
    
    // Read register value
    result = enc28j60_spi_transfer(0x00);
    
    enc28j60_arch_spi_deselect();
    
    return result;
}

/**
 * @brief Write control register (Arduino reference exact implementation)
 */
static void enc28j60_write_register_internal(uint8_t reg, uint8_t value) {
    enc28j60_arch_spi_select();
    
    // Send write command (Arduino reference)
    enc28j60_spi_transfer(ENC28J60_WRITE_CTRL_REG | (reg & 0x1F));
    
    // Send register value
    enc28j60_spi_transfer(value);
    
    enc28j60_arch_spi_deselect();
}

/**
 * @brief SPI chip select (Arduino reference timing)
 */
static void enc28j60_arch_spi_select(void) {
    gpio_put(ENC28J60_CS_PIN, 0);
}

/**
 * @brief SPI chip deselect (Arduino reference timing)
 */
static void enc28j60_arch_spi_deselect(void) {
    sleep_us(1);  // Hold time (Arduino reference)
    gpio_put(ENC28J60_CS_PIN, 1);
    sleep_us(1);  // Hold time (Arduino reference)
}

/**
 * @brief Transfer single byte over SPI
 */
static uint8_t enc28j60_spi_transfer(uint8_t data) {
    uint8_t received;
    spi_write_read_blocking(SPI_PORT, &data, &received, 1);
    return received;
}

/**
 * @brief Hard reset
 */
void enc28j60_reset(void) {
    
    gpio_put(ENC28J60_RESET_PIN, 0);

     //pull enc from reset
    sleep_us(20);
    gpio_put(ENC28J60_RESET_PIN, 1);
    
    // Reset bank tracking - after reset, we're in bank 0 (Arduino reference)
    g_current_bank = ERXTX_BANK;
    
    // Arduino reference: "Workaround for erratum #2" + wait for reset (increased for RP2350)
    sleep_us(2000);  // 2ms delay (increased for RP2350 timing)
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_WARN, LOG_EVENT_NETWORK_RESET, 0);
}

/**
 * @brief Wait for oscillator ready (Arduino reference implementation)
 */
static bool enc28j60_wait_for_osc_ready(void) {
    uint32_t timeout = 50000;  // 50ms timeout
    
    // Ensure we're in bank 0 for ESTAT register
    g_current_bank = ERXTX_BANK;
    
    DEBUG_ONLY({ printf("ENC28J60: Waiting for oscillator ready\n"); });
    sleep_us(1000);  // Errata #2

    while (timeout > 0) {
        uint8_t estat = enc28j60_read_register_internal(ENC28J60_ESTAT);
        
        if (estat != 0xFF && (estat & ENC28J60_ESTAT_CLKRDY)) {
            DEBUG_ONLY({ 
                printf("ENC28J60: Oscillator ready, estat: %d, timeout: %d\n", estat, timeout); 
            });
#ifdef FACTORY_INTERNAL_VERSION
            selftest_puts("RP2354 SELFTEST: ENC28J60 OSC PASS\r\n");
#endif
            return true;
        }
        
        sleep_us(100);  // Wait 100us between attempts
        timeout -= 100;
    }
    
    DEBUG_ONLY({ 
        printf("ENC28J60: Oscillator timeout: %d\n", timeout); 
    });
#ifdef FACTORY_INTERNAL_VERSION
    selftest_puts("RP2354 SELFTEST: ENC28J60 OSC FAILED\r\n");
#endif
    return false;
}

/**
 * @brief Configure buffer memory layout (Arduino reference exact sequence)
 */
static void enc28j60_configure_buffers(void) {
    DEBUG_ONLY({ printf("ENC28J60: Configuring buffers\n"); });
    
    // Set up receive buffer (must be in Bank 0) - exact Arduino order
    enc28j60_set_register_bank_internal(ERXTX_BANK);
    
    // Set RX buffer start
    enc28j60_write_register_internal(ENC28J60_ERXSTL, RX_BUF_START & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ERXSTH, (RX_BUF_START >> 8) & 0xFF);
    
    // Set RX buffer end
    enc28j60_write_register_internal(ENC28J60_ERXNDL, RX_BUF_END & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ERXNDH, (RX_BUF_END >> 8) & 0xFF);
    
    // Set initial read pointer to start (ERDPT)
    enc28j60_write_register_internal(ENC28J60_ERDPTL, RX_BUF_START & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ERDPTH, (RX_BUF_START >> 8) & 0xFF);
    
    // Set RX read pointer (ERXRDPT) - Arduino reference
    enc28j60_write_register_internal(ENC28J60_ERXRDPTL, RX_BUF_END & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ERXRDPTH, (RX_BUF_END >> 8) & 0xFF);
        
    // Set up transmit buffer boundaries (Linux kernel pattern)
    enc28j60_write_register_internal(ENC28J60_ETXSTL, TX_BUF_START & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ETXSTH, (TX_BUF_START >> 8) & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ETXNDL, TX_BUF_END & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ETXNDH, (TX_BUF_END >> 8) & 0xFF);

    
    // Configure receive filters in Bank 1 (Arduino reference)
    enc28j60_set_register_bank_internal(EPKTCNT_BANK);
    // ERXFCON: Enable unicast (UCEN), CRC check (CRCEN), multicast (MCEN), and broadcast (BCEN) for DHCP
    uint8_t erxfcon_value = ENC28J60_ERXFCON_UCEN | ENC28J60_ERXFCON_CRCEN | ENC28J60_ERXFCON_MCEN | ENC28J60_ERXFCON_BCEN;
    enc28j60_write_register_internal(ENC28J60_ERXFCON, erxfcon_value);
    
    

    DEBUG_ONLY({ printf("ENC28J60: Buffer configuration complete\n"); });
}

/**
 * @brief Configure MAC layer (Arduino reference exact sequence)
 */
static void enc28j60_configure_mac(void) {
    DEBUG_ONLY({ printf("ENC28J60: Configuring MAC\n"); });
    
    enc28j60_block_interrupt();

    // Configure Bank 2 MAC registers - follow Arduino exact sequence
    enc28j60_set_register_bank_internal(MACONX_BANK);
    
    // Arduino step 1: Turn on reception and flow control
    // CRITICAL FIX: Use direct register write instead of bit field operations for MAC registers
    uint8_t macon1_value = ENC28J60_MACON1_MARXEN | ENC28J60_MACON1_TXPAUS | ENC28J60_MACON1_RXPAUS;
    enc28j60_write_register_internal(ENC28J60_MACON1, macon1_value);
    DEBUG_ONLY({ 
        printf("ENC28J60: MACON1 = 0x%02X (should be 0x0D)\n", macon1_value); 
    });
    
    // Arduino step 2: Set padding, crc, full duplex
    // CRITICAL FIX: Use direct register write instead of bit field operations for MAC registers
    uint8_t macon3_value = ENC28J60_MACON3_PADCFG_FULL | ENC28J60_MACON3_TXCRCEN | 
                           ENC28J60_MACON3_FULDPX | ENC28J60_MACON3_FRMLNEN;
    enc28j60_write_register_internal(ENC28J60_MACON3, macon3_value);
    DEBUG_ONLY({ 
        printf("ENC28J60: MACON3 = 0x%02X (should be 0xF3)\n", macon3_value); 
    });
    
    // Arduino step 3: Set maximum frame length
    enc28j60_write_register_internal(ENC28J60_MAMXFLL, MAX_MAC_LENGTH & 0xFF);
    enc28j60_write_register_internal(ENC28J60_MAMXFLH, (MAX_MAC_LENGTH >> 8) & 0xFF);
    
    // Arduino step 4: Set back-to-back inter packet gap
    enc28j60_write_register_internal(ENC28J60_MABBIPG, 0x15);
    
    // Arduino step 5: Set non-back-to-back inter packet gap
    enc28j60_write_register_internal(ENC28J60_MAIPGL, 0x12);
    
    // Verify MAC register configuration by reading back the values
    enc28j60_set_register_bank_internal(MACONX_BANK);
    uint8_t macon1_verify = enc28j60_read_register_internal(ENC28J60_MACON1);
    uint8_t macon3_verify = enc28j60_read_register_internal(ENC28J60_MACON3);
    
    if (macon1_verify != macon1_value) {
        printf("ENC28J60: WARNING! MACON1 verification failed (expected 0x%02X, got 0x%02X)\n", macon1_value, macon1_verify);
    }
    if (macon3_verify != macon3_value) {
        printf("ENC28J60: WARNING! MACON3 verification failed (expected 0x%02X, got 0x%02X)\n", macon3_value, macon3_verify);
    }
    
    // Set MAC address if available
    if (g_local_mac != NULL) {
        enc28j60_set_register_bank_internal(MAADRX_BANK);
        enc28j60_write_register_internal(ENC28J60_MAADR6, g_local_mac[5]);
        enc28j60_write_register_internal(ENC28J60_MAADR5, g_local_mac[4]);
        enc28j60_write_register_internal(ENC28J60_MAADR4, g_local_mac[3]);
        enc28j60_write_register_internal(ENC28J60_MAADR3, g_local_mac[2]);
        enc28j60_write_register_internal(ENC28J60_MAADR2, g_local_mac[1]);
        enc28j60_write_register_internal(ENC28J60_MAADR1, g_local_mac[0]);
    }
    
    enc28j60_unblock_interrupt();

    DEBUG_ONLY({ printf("ENC28J60: MAC configuration complete\n"); });
}

/**
 * @brief Configure PHY layer (minimal for basic operation)
 */
static void enc28j60_configure_phy(void) {
    DEBUG_ONLY({ printf("ENC28J60: Configuring PHY for full duplex\n"); });

    // PHCON1.PDPXMD must match MACON3.FULDPX (datasheet Section 3.3.1).
    // The PHY may default to half duplex, and errata item 16 warns that
    // the LED auto-polarity detection can reset PDPXMD to the wrong state.
    // Explicitly set full duplex to match the MAC configuration.
    enc28j60_write_phy_register(ENC28J60_PHCON1, ENC28J60_PHCON1_PDPXMD);

    // Verify the write
    uint16_t phcon1 = enc28j60_read_phy_register(ENC28J60_PHCON1);
    if (phcon1 & ENC28J60_PHCON1_PDPXMD) {
        DEBUG_ONLY({ printf("ENC28J60: PHY full duplex confirmed (PHCON1=0x%04X)\n", phcon1); });
    } else {
        printf("ENC28J60: WARNING! PHY duplex mismatch - PHCON1=0x%04X, expected PDPXMD set\n", phcon1);
    }
}

/**
 * @brief Initialize ENC28J60 driver (Arduino reference exact sequence)
 */
bool enc28j60_init(void) {
//#define FACTORY_INTERNAL_VERSION    
    if (g_driver_initialized) {
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_INIT, 1);
        return true;
    }
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, 0);
    
    // Initialize SPI and GPIO first (RP2350-specific)
    //selftest output
#ifdef FACTORY_INTERNAL_VERSION
    selftest_puts("RP2354 SELFTEST: INIT ENC28J60\r\n");
#endif
    enc28j60_spi_init();
    enc28j60_gpio_init();
    
    // Ensure CS is deasserted and interface stable (increased for RP2350)
    gpio_put(ENC28J60_CS_PIN, 1);
    sleep_ms(25);
    
    // Arduino reference initialization sequence:
    
    // 1. Perform software reset
    
#ifdef FACTORY_INTERNAL_VERSION
    selftest_puts("RP2354 SELFTEST: RESET ENC28J60\r\n");
#endif
    enc28j60_reset();
    
    // 2. Wait for oscillator ready
    if (!enc28j60_wait_for_osc_ready()) {
        DEBUG_ONLY({ 
            printf("ENC28J60: Oscillator not ready - init failed\n"); 
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, 1);
        return false;
    }
    
    // 3. Configure buffer memory layout
    enc28j60_configure_buffers();
    
    // 3a. Initialize TX FIFO boundaries (Linux kernel approach)
    enc28j60_set_register_bank_internal(ERXTX_BANK);
    enc28j60_write_register_internal(ENC28J60_ETXSTL, TX_BUF_START & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ETXSTH, (TX_BUF_START >> 8) & 0xFF);
    DEBUG_ONLY({ printf("ENC28J60: TX FIFO initialized - Start: 0x%04X, End: 0x%04X\n", TX_BUF_START, TX_BUF_END); });
    
    // 4. Configure MAC layer
    enc28j60_configure_mac();
    
    // 5. Configure PHY layer
    enc28j60_configure_phy();
    
    // 6. Turn on autoincrement for buffer access (Arduino reference)
    enc28j60_set_register_bank_internal(ERXTX_BANK);
    enc28j60_set_register_bits(ENC28J60_ECON2, ENC28J60_ECON2_AUTOINC);
    
    // 7. Verify chip communication by reading revision ID
    enc28j60_set_register_bank_internal(MAADRX_BANK);
    uint8_t revid = enc28j60_read_register_internal(ENC28J60_EREVID);
    DEBUG_ONLY({ printf("ENC28J60: Chip revision = 0x%02X\n", revid); });

    // Initialize driver state
    memset(&g_enc28j60_state, 0, sizeof(g_enc28j60_state));
    g_enc28j60_state.initialized = true;
    g_enc28j60_state.next_packet_ptr = RX_BUF_START;
    
    g_driver_initialized = true;
    
    // 8. Enable interrupts BEFORE enabling reception
    // This ensures interrupt handling is fully ready when packets arrive
    enc28j60_enable_interrupts();
    
    //    9. Turn on reception (FINAL STEP - all infrastructure ready)
    // Once RXEN is set, packets can start arriving and triggering interrupts
    enc28j60_set_register_bank_internal(ERXTX_BANK);
    enc28j60_write_register_internal(ENC28J60_ECON1, ENC28J60_ECON1_RXEN);
    
    // Verify initialization by checking ECON1
    uint8_t econ1_check = enc28j60_read_register_internal(ENC28J60_ECON1);
    if ((econ1_check & ENC28J60_ECON1_RXEN) == 0) {
        DEBUG_ONLY({ 
            printf("ENC28J60: RXEN verification failed\n");
        });
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, 2);
        return false;
    }
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, 2);

    g_is_ready = true;
    
    return true;
}

/**
 * @brief Deinitialize ENC28J60 driver
 */
void enc28j60_deinit(void) {
    if (!g_driver_initialized) {
        return;
    }
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, 0);
    
    // Disable interrupts
    enc28j60_block_interrupt();
    
    // Disable packet reception
    enc28j60_set_register_bank_internal(ERXTX_BANK);
    enc28j60_clear_register_bits(ENC28J60_ECON1, ENC28J60_ECON1_RXEN);
    
    // Reset the chip
    enc28j60_reset();
    
    // Clear driver state
    memset(&g_enc28j60_state, 0, sizeof(g_enc28j60_state));
    g_driver_initialized = false;
    g_current_bank = 0xFF;
    g_local_mac = NULL;
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_DEINIT, 1);
}

/**
 * @brief Get driver state for diagnostics
 */
const enc28j60_state_t* enc28j60_get_state(void) {
    return &g_enc28j60_state;
}

/**
 * @brief Set register bank (public interface)
 */
void enc28j60_set_bank(uint8_t bank) {
    if (!g_driver_initialized) {
        return;
    }
    enc28j60_set_register_bank_internal(bank);
}

/**
 * @brief Set bits in control register (Arduino reference exact logic)
 */
void enc28j60_set_register_bits(uint8_t reg, uint8_t mask) {
    if (!g_driver_initialized) {
        return;
    }
    
    // For MAC/MII registers, we need to read-modify-write (Arduino reference)
    if (enc28j60_is_mac_mii_register(reg)) {
        uint8_t value = enc28j60_read_register_internal(reg);
        enc28j60_write_register_internal(reg, value | mask);
    } else {
        // Use bit field set command for ETH registers (Arduino reference)
        enc28j60_arch_spi_select();
        enc28j60_spi_transfer(ENC28J60_BIT_FIELD_SET | (reg & 0x1F));
        enc28j60_spi_transfer(mask);
        enc28j60_arch_spi_deselect();
    }
}

/**
 * @brief Clear bits in control register (Arduino reference exact logic)
 */
void enc28j60_clear_register_bits(uint8_t reg, uint8_t mask) {
    if (!g_driver_initialized) {
        return;
    }
    
    // For MAC/MII registers, we need to read-modify-write (Arduino reference)
    if (enc28j60_is_mac_mii_register(reg)) {
        uint8_t value = enc28j60_read_register_internal(reg);
        enc28j60_write_register_internal(reg, value & ~mask);
    } else {
        // Use bit field clear command for ETH registers (Arduino reference)
        enc28j60_arch_spi_select();
        enc28j60_spi_transfer(ENC28J60_BIT_FIELD_CLR | (reg & 0x1F));
        enc28j60_spi_transfer(mask);
        enc28j60_arch_spi_deselect();
    }
}

/**
 * @brief Read control register with bank switching
 */
uint8_t enc28j60_read_register_bank(uint8_t reg, uint8_t bank) {
    if (!g_driver_initialized) {
        return 0xFF;
    }
    
    enc28j60_set_register_bank_internal(bank);
    return enc28j60_read_register_internal(reg);
}

/**
 * @brief Write control register with bank switching
 */
void enc28j60_write_register_bank(uint8_t reg, uint8_t bank, uint8_t value) {
    if (!g_driver_initialized) {
        return;
    }
    
    enc28j60_set_register_bank_internal(bank);
    enc28j60_write_register_internal(reg, value);
}

/**
 * @brief Read control register (current bank)
 */
uint8_t enc28j60_read_register(uint8_t reg) {
    if (!g_driver_initialized) {
        return 0xFF;
    }
    
    return enc28j60_read_register_internal(reg);
}

/**
 * @brief Write control register (current bank)
 */
void enc28j60_write_register(uint8_t reg, uint8_t value) {
    if (!g_driver_initialized) {
        return;
    }
    
    enc28j60_write_register_internal(reg, value);
}

/**
 * @brief Set MAC address (Arduino reference mapping)
 */
void enc28j60_set_mac_address(const uint8_t mac_addr[6]) {
    if (!g_driver_initialized || !mac_addr) {
        return;
    }
    
    g_local_mac = mac_addr;  // Store reference for later use
    
    // Set MAC address registers in Bank 3 (Arduino reference mapping)
    enc28j60_write_register_bank(ENC28J60_MAADR6, MAADRX_BANK, mac_addr[5]);
    enc28j60_write_register_bank(ENC28J60_MAADR5, MAADRX_BANK, mac_addr[4]);
    enc28j60_write_register_bank(ENC28J60_MAADR4, MAADRX_BANK, mac_addr[3]);
    enc28j60_write_register_bank(ENC28J60_MAADR3, MAADRX_BANK, mac_addr[2]);
    enc28j60_write_register_bank(ENC28J60_MAADR2, MAADRX_BANK, mac_addr[1]);
    enc28j60_write_register_bank(ENC28J60_MAADR1, MAADRX_BANK, mac_addr[0]);
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_CONFIG, 0);
}

/**
 * @brief Get MAC address (Arduino reference mapping)
 */
void enc28j60_get_mac_address(uint8_t mac_addr[6]) {
    if (!g_driver_initialized || !mac_addr) {
        return;
    }
    
    // Read MAC address registers from Bank 3 (Arduino reference mapping)
    mac_addr[0] = enc28j60_read_register_bank(ENC28J60_MAADR1, MAADRX_BANK);
    mac_addr[1] = enc28j60_read_register_bank(ENC28J60_MAADR2, MAADRX_BANK);
    mac_addr[2] = enc28j60_read_register_bank(ENC28J60_MAADR3, MAADRX_BANK);
    mac_addr[3] = enc28j60_read_register_bank(ENC28J60_MAADR4, MAADRX_BANK);
    mac_addr[4] = enc28j60_read_register_bank(ENC28J60_MAADR5, MAADRX_BANK);
    mac_addr[5] = enc28j60_read_register_bank(ENC28J60_MAADR6, MAADRX_BANK);
}

// RP2350-specific initialization functions

/**
 * @brief Initialize SPI interface (RP2350-specific)
 */
static void enc28j60_spi_init(void) {
    // Initialize SPI at 20MHz (max for ENC28J60)
    spi_init(SPI_PORT, SPI_BAUDRATE);
    
    // Configure SPI pins
    gpio_set_function(ENC28J60_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(ENC28J60_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(ENC28J60_MISO_PIN, GPIO_FUNC_SPI);
    
    // Configure SPI format (Mode 0, 8 bits) - Arduino reference
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

/**
 * @brief ENC28J60 interrupt handler
 */
static void enc28j60_interrupt_handler(uint gpio, uint32_t events) {
    if (gpio == ENC28J60_INTERRUPT_PIN) {
        //TBD: to not read SPI in interrupt (might interrupt send/receive) Read interrupt status to clear the interrupt
        //g_last_interrupt_status = enc28j60_read_register_internal(ENC28J60_EIR);
        g_interrupt_time = to_ms_since_boot(get_absolute_time());
        g_interrupt_pending = true;
        
        //enc28j60_clear_interrupts(0xFF);    //clear all, we have a copy
    }
    return;
}
 
/**
 * @brief Initialize GPIO pins (RP2350-specific)
 */
static void enc28j60_gpio_init(void) {
    // Initialize CS pin as output (high = deselected)
    gpio_init(ENC28J60_CS_PIN);
    gpio_set_function(ENC28J60_CS_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(ENC28J60_CS_PIN, GPIO_OUT);
    gpio_pull_up(ENC28J60_CS_PIN);
    gpio_set_slew_rate(ENC28J60_CS_PIN, GPIO_SLEW_RATE_FAST);
    
    gpio_put(ENC28J60_CS_PIN, 1);

    // Initialize interrupt pin as input with pull-up
    gpio_init(ENC28J60_INTERRUPT_PIN);
    gpio_set_function(ENC28J60_INTERRUPT_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(ENC28J60_INTERRUPT_PIN, GPIO_IN);
    gpio_pull_up(ENC28J60_INTERRUPT_PIN);
    
    
    // Initialize RESET pin as output (low = reset)
    gpio_init(ENC28J60_RESET_PIN);
    gpio_set_function(ENC28J60_RESET_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(ENC28J60_RESET_PIN, GPIO_OUT);
    gpio_set_input_enabled(ENC28J60_RESET_PIN, false);

    gpio_put(ENC28J60_RESET_PIN, 0);    //active low reset, keep in reset until software is ready

    // Set up interrupt handler for falling edge (active low interrupt)
    gpio_set_irq_enabled_with_callback(ENC28J60_INTERRUPT_PIN, GPIO_IRQ_EDGE_FALL, 
                                       true, &enc28j60_interrupt_handler);                                       
    
    DEBUG_ONLY({ printf("ENC28J60: GPIO interrupt handler configured for pin %d\n", ENC28J60_INTERRUPT_PIN); });
    
}

/**
 * @brief Enable ENC28J60 interrupts
 */
static void enc28j60_enable_interrupts(void) {

    enc28j60_set_register_bank_internal(ERXTX_BANK);
    
    // Clear any pending interrupts first
    enc28j60_clear_register_bits(ENC28J60_EIR, 0xFF);
    
    // Enable global interrupts and specific interrupt sources
    uint8_t eie = ENC28J60_EIE_INTIE /*ENABLE INTERRUPTS*/ 
                | ENC28J60_EIE_PKTIE /* PACKET RECEIVED */
                | ENC28J60_EIE_LINKIE /* LINK STATUS CHANGED */
                | ENC28J60_EIE_TXIE /* TRANSMIT COMPLETED -> ERRATA 12,13 */ 
                | ENC28J60_EIE_TXERIE /* TRANSMIT ERROR -> ERRATA 12,13 */ ;
    enc28j60_write_register_internal(ENC28J60_EIE, eie);
    
    DEBUG_ONLY({ printf("ENC28J60: Interrupts enabled (EIE=0x%02X)\n", eie); });
}

/**
 * @brief get enable ENC28J60 interrupts
 */
static uint8_t enc28j60_get_enabled_interrupts(void) {

    enc28j60_set_register_bank_internal(ERXTX_BANK);
    // Enable global interrupts and specific interrupt sources
    uint8_t eie=0;
    eie = enc28j60_read_register_internal(ENC28J60_EIE);
    
    DEBUG_ONLY({printf("ENC28J60: Interrupts currently enabled (EIE=0x%02X)\n", eie);});

    return eie;
}


/**
 * @brief unblock the interrupt gpio pin
 */
static void enc28j60_unblock_interrupt(void) {
    // DEBUG_ONLY({printf("ENC28J60: UNBLOCKING IRQ!\n");});
    gpio_set_irq_enabled(ENC28J60_INTERRUPT_PIN, GPIO_IRQ_EDGE_FALL, true);

}

/**
 * @brief block the interrupt gpio pin to avoid interrupts
 */
 static void enc28j60_block_interrupt(void) {
    // DEBUG_ONLY({printf("ENC28J60: BLOCKING IRQ!\n");});
    gpio_set_irq_enabled(ENC28J60_INTERRUPT_PIN, GPIO_IRQ_EDGE_FALL, false);
}


// Remaining packet and buffer functions to be implemented with Arduino patterns...

/**
 * @brief Read data from buffer memory (Arduino reference implementation)
 */
void enc28j60_read_buffer(uint8_t* buffer, uint16_t length) {
    if (!g_driver_initialized || !buffer || length == 0) {
        return;
    }
    
    enc28j60_arch_spi_select();
    
    // Send read buffer memory command (Arduino reference)
    enc28j60_spi_transfer(ENC28J60_READ_BUF_MEM);
    
    // Read data
    for (uint16_t i = 0; i < length; i++) {
        buffer[i] = enc28j60_spi_transfer(0x00);
    }
    
    enc28j60_arch_spi_deselect();
}

/**
 * @brief Write data to buffer memory (Arduino reference implementation)
 */
void enc28j60_write_buffer(const uint8_t* buffer, uint16_t length) {
    if (!g_driver_initialized || !buffer || length == 0) {
        return;
    }
    
    enc28j60_arch_spi_select();
    
    // Send write buffer memory command (Arduino reference)
    enc28j60_spi_transfer(ENC28J60_WRITE_BUF_MEM);
    
    // Write data
    for (uint16_t i = 0; i < length; i++) {
        enc28j60_spi_transfer(buffer[i]);
    }
    
    enc28j60_arch_spi_deselect();
}

uint32_t get_interrupt_ms()
{
    return g_interrupt_time;
}

/**
 * @brief Dump all transmission-relevant registers for debugging
 */
static void enc28j60_dump_tx_registers(const char* phase) {
    (void)phase;  // Suppress unused warning
    // DISABLED: Too verbose for normal operation. Uncomment for deep debugging.
#if 0
    DEBUG_ONLY({ 
        printf("=== TX Register Dump - %s ===\n", phase);
        
        // Ensure we're in the right bank for each register
        enc28j60_set_register_bank_internal(ERXTX_BANK);
        uint8_t econ1 = enc28j60_read_register_internal(ENC28J60_ECON1);
        uint8_t eir = enc28j60_read_register_internal(ENC28J60_EIR);
        uint8_t eie = enc28j60_read_register_internal(ENC28J60_EIE);
        uint8_t estat = enc28j60_read_register_internal(ENC28J60_ESTAT);
        
        uint16_t etxst = (enc28j60_read_register_internal(ENC28J60_ETXSTH) << 8) | 
                        enc28j60_read_register_internal(ENC28J60_ETXSTL);
        uint16_t etxnd = (enc28j60_read_register_internal(ENC28J60_ETXNDH) << 8) | 
                        enc28j60_read_register_internal(ENC28J60_ETXNDL);
        uint16_t ewrpt = (enc28j60_read_register_internal(ENC28J60_EWRPTH) << 8) | 
                        enc28j60_read_register_internal(ENC28J60_EWRPTL);
        
        printf("  ECON1: 0x%02X (TXRTS=%d, RXEN=%d, TXRST=%d)\n", 
            econ1, 
            (econ1 & ENC28J60_ECON1_TXRTS) ? 1 : 0,
            (econ1 & ENC28J60_ECON1_RXEN) ? 1 : 0,
            (econ1 & ENC28J60_ECON1_TXRST) ? 1 : 0);
        
        printf("  EIR:   0x%02X (TXIF=%d, TXERIF=%d, PKTIF=%d, LINKIF=%d)\n", 
            eir,
            (eir & ENC28J60_EIR_TXIF) ? 1 : 0,
            (eir & ENC28J60_EIR_TXERIF) ? 1 : 0,
            (eir & ENC28J60_EIR_PKTIF) ? 1 : 0,
            (eir & ENC28J60_EIR_LINKIF) ? 1 : 0);
        
        printf("  EIE:   0x%02X (INTIE=%d, TXIE=%d, TXERIE=%d)\n", 
            eie,
            (eie & ENC28J60_EIE_INTIE) ? 1 : 0,
            (eie & ENC28J60_EIE_TXIE) ? 1 : 0,
            (eie & ENC28J60_EIE_TXERIE) ? 1 : 0);
        
        printf("  ESTAT: 0x%02X (TXABRT=%d, CLKRDY=%d)\n", 
            estat,
            (estat & ENC28J60_ESTAT_TXABRT) ? 1 : 0,
            (estat & ENC28J60_ESTAT_CLKRDY) ? 1 : 0);
        
        printf("  ETXST: 0x%04X, ETXND: 0x%04X, EWRPT: 0x%04X\n", etxst, etxnd, ewrpt);
        printf("  TX Buffer: Start=0x%04X, End=0x%04X (Size=%d bytes)\n", 
            TX_BUF_START, TX_BUF_END, TX_BUF_END - TX_BUF_START + 1);
        printf("==================================\n");
    });
#endif
}


/**
 * @brief Send Ethernet packet (Arduino reference implementation)
 */
bool enc28j60_send_packet(const enc28j60_packet_t* packet) {
    if (!g_driver_initialized || !packet || !packet->data || 
        packet->length == 0 || !packet->valid) {
        DEBUG_ONLY({  printf("ENC28J60: can not send. NOT INITIALIZED\n" ); });
    
        return false;
    }
    
    if (packet->length > ENC28J60_MAX_FRAME_SIZE) {
        g_enc28j60_state.tx_errors++;
        DEBUG_ONLY({ printf("ENC28J60: can not send. TOO LONG\n" ); });
        return false;
    }
    
    bool transfer_complete = false;
    bool transfer_complete_interrupt_flag = false;
    bool transfer_error_interrupt_flag = false;
    uint32_t debug_counter __attribute__((unused)) = 0;

    int retries = 3;
    while(retries--)
    {
        // DEBUG_ONLY({printf("ENC28J60: Starting transmission of %d byte packet\n", packet->length);});
        if( retries < 2)
        {
            DEBUG_ONLY({ printf("ENC28J60: Starting transmission of %d byte packet TXERIF: %d\n", packet->length, transfer_error_interrupt_flag); });
        }
        enc28j60_block_interrupt();
        
        enc28j60_set_register_bank_internal(ERXTX_BANK);
        
        // DEBUGGING: Dump registers before transmission
        DEBUG_ONLY({enc28j60_dump_tx_registers("BEFORE TX");});
        
        //lifted from linux kernel
        // Set write pointer to start of transmit buffer
        enc28j60_write_register_internal(ENC28J60_EWRPTL, TX_BUF_START & 0xFF);
        enc28j60_write_register_internal(ENC28J60_EWRPTH, (TX_BUF_START >> 8) & 0xFF);

        // Set TX end pointer BEFORE writing data (Linux kernel approach)
        uint16_t tx_end = TX_BUF_START + packet->length;
        enc28j60_write_register_internal(ENC28J60_ETXNDL, tx_end & 0xFF);
        enc28j60_write_register_internal(ENC28J60_ETXNDH, (tx_end >> 8) & 0xFF);

        // Now write control byte and packet data
        uint8_t control_byte = 0x00;
        enc28j60_write_buffer(&control_byte, 1);
        enc28j60_write_buffer(packet->data, packet->length);

        // COMPREHENSIVE TX STATE RESET - clear all TX-related flags and state
        enc28j60_clear_register_bits(ENC28J60_EIR, ENC28J60_EIR_TXIF | ENC28J60_EIR_TXERIF);
        
        // Ensure we're in the right bank for ECON1
        enc28j60_set_register_bank_internal(ERXTX_BANK);
        
        // Make sure TXRTS is clear and no reset is pending  
        uint8_t econ1_before = enc28j60_read_register_internal(ENC28J60_ECON1);
        if (econ1_before & (ENC28J60_ECON1_TXRTS | ENC28J60_ECON1_TXRST)) {
            // Force clear TXRTS and TXRST if set
            enc28j60_clear_register_bits(ENC28J60_ECON1, ENC28J60_ECON1_TXRTS | ENC28J60_ECON1_TXRST);
            sleep_us(10); // Wait for clear to take effect
        }
        
        //wait for everything to settle
        sleep_us(5);
        
        // Start transmission (Arduino reference)
        enc28j60_set_register_bits(ENC28J60_ECON1, ENC28J60_ECON1_TXRTS);

        // CHECK FOR IMMEDIATE COMPLETION (transmission often completes in microseconds!)
        // Wait 50µs - enough time for short packets to complete and SPI to settle
        sleep_us(50);
        
        uint8_t econ1_immediate = enc28j60_read_register_internal(ENC28J60_ECON1);
        sleep_us(5);  // Small delay between SPI operations for reliability
        uint8_t eir_immediate = enc28j60_read_register_internal(ENC28J60_EIR);
        
        bool immediate_complete = (econ1_immediate & ENC28J60_ECON1_TXRTS) == 0;
        bool immediate_txif = (eir_immediate & ENC28J60_EIR_TXIF) != 0;
        bool immediate_txerif = (eir_immediate & ENC28J60_EIR_TXERIF) != 0;

        if (immediate_complete && immediate_txif) {
            transfer_complete = true;
            transfer_complete_interrupt_flag = true;
            transfer_error_interrupt_flag = immediate_txerif;
            
            // Clear the interrupt flags manually since we caught them before interrupt handler
            enc28j60_clear_register_bits(ENC28J60_EIR, ENC28J60_EIR_TXIF | ENC28J60_EIR_TXERIF);
            
            goto transfer_finished;
        } else if (immediate_txerif) {
            DEBUG_ONLY({ printf("ENC28J60: TX error detected immediately\n"); });
            transfer_complete = immediate_complete;
            transfer_complete_interrupt_flag = false;
            transfer_error_interrupt_flag = true;
            goto transfer_finished;
        }

        // TX timeout: 10ms is sufficient for 10Mbps Ethernet
        // A 1518-byte frame at 10Mbps takes ~1.2ms, plus overhead
        core1_timer_set(CORE1_TIMER_NETWORK_TX_TIMEOUT, 10);
        enc28j60_unblock_interrupt();

        //wait for interrupt, either ENC28J60_EIR_TXIF (good case) or CORE1_TIMER_NETWORK_TX_TIMEOUT (bad case)
        uint32_t poll_timeout = 1000; // Simple timeout fallback
        while( !core1_timer_is_expired(CORE1_TIMER_NETWORK_TX_TIMEOUT) && poll_timeout-- > 0) {
            sleep_us(100); // Replace __wfi() with simple delay
            // block to read registers
            enc28j60_block_interrupt();
                
            uint8_t econ1 = enc28j60_read_register_internal(ENC28J60_ECON1);
            enc28j60_process_interrupts(true); // read g_last_interrupt_status fresh, interrupts are blocked!

            enc28j60_unblock_interrupt();

            transfer_complete = (econ1 & ENC28J60_ECON1_TXRTS) == 0;
            transfer_complete_interrupt_flag = g_last_interrupt_status & ENC28J60_EIR_TXIF;
            transfer_error_interrupt_flag = g_last_interrupt_status & ENC28J60_EIR_TXERIF;

            if(transfer_complete || transfer_complete_interrupt_flag){ 
                goto transfer_finished;
            }
        }

        //timer expired, reset and retry
        DEBUG_ONLY({ printf("tx transfer timed out, retry\n"); });
        enc28j60_reset_tx_logic();

    /*    TBD: this was the original arduino control flow. TBD: use or remove
    // Wait for transmission to complete (Arduino reference - synchronous!)
    uint32_t timeout = 5000; // 5ms timeout Hardware transmission: ~1230μs max (for 1518 byte frame at 10Mbps)
    while (timeout > 0) {
        ++debug_counter;
        
        uint8_t econ1 = enc28j60_read_register_internal(ENC28J60_ECON1);
        enc28j60_process_interrupts(true); // read g_last_interrupt_status fresh, interrupts are blocked!
        uint16_t ewrpt = (enc28j60_read_register_internal(ENC28J60_EWRPTH) << 8) | 
                     enc28j60_read_register_internal(ENC28J60_EWRPTL);

        transfer_complete = (econ1 & ENC28J60_ECON1_TXRTS) == 0;
        transfer_complete_interrupt_flag = g_last_interrupt_status & ENC28J60_EIR_TXIF;
        transfer_error_interrupt_flag = g_last_interrupt_status & ENC28J60_EIR_TXERIF;

        
        // DEBUGGING: Print status every 1000 iterations (every ~1ms)
        DEBUG_ONLY({
            if (++debug_counter < 10) {
                printf("  TX Poll #%u: ECON1=0x%02X(TXRTS=%d) EIR=0x%02X(TXIF=%d,TXERIF=%d ewrpt=0x%04X) timeout=%u\n",
                       debug_counter, econ1, (econ1 & ENC28J60_ECON1_TXRTS) ? 1 : 0,
                       g_last_interrupt_status, transfer_complete_interrupt_flag ? 1 : 0, 
                       transfer_error_interrupt_flag ? 1 : 0, ewrpt, timeout);
            }
        });
        
        
        // Check all completion conditions
        if (transfer_complete) {
            DEBUG_ONLY( {printf("  TX Complete: TXRTS cleared after %u iterations\n", debug_counter);});
            break; // Original TXRTS cleared (if bug doesn't occur)
        }
        

        transfer_complete_interrupt_flag = g_last_interrupt_status & ENC28J60_EIR_TXIF;
        transfer_error_interrupt_flag = g_last_interrupt_status & ENC28J60_EIR_TXERIF;

        if (transfer_complete_interrupt_flag) {
            DEBUG_ONLY( {printf("  TX Complete: TXIF flag set after %u iterations\n", debug_counter);});
            break; // SUCCESS: Transmission completed
        }
        
        if (transfer_error_interrupt_flag) {
            DEBUG_ONLY( {printf("  TX Error: TXERIF flag set after %u iterations\n", debug_counter);});
            break; // ERROR: Transmission failed
        }
        sleep_us(1);
        timeout--;
    }
    if(timeout == 0)
    {
        DEBUG_ONLY({ printf("SEND TIMEOUT! packet->length: %d, iterations: %u\n", packet->length, debug_counter); });
        DEBUG_ONLY({ enc28j60_dump_tx_registers("TIMEOUT"); });
        enc28j60_reset_tx_logic();
    }
    DEBUG_ONLY( {printf("ENC28J60: send took: %d us\n", 5000- timeout ); });
    */
    }

transfer_finished:
    enc28j60_block_interrupt();
    bool other_error = false;
    bool collision_error = false;
                
    bool tx_success = (transfer_complete && (!other_error) && (!collision_error) ) ;
    if(!tx_success)
    {
        // Check for transmission errors (Arduino reference)
        uint8_t estat = enc28j60_read_register_internal(ENC28J60_ESTAT);

        other_error = (estat & ENC28J60_ESTAT_TXABRT);
        collision_error = (g_last_interrupt_status & ENC28J60_EIR_TXERIF) && (!transfer_complete); // errata #15, if transfer_complete, probably collision false positive
        
        if(collision_error && transfer_complete)
        {
            // In switched full-duplex environment, treat collision errors as false
            g_enc28j60_state.likely_false_collisions++;
        }
    }   
    
    // DISABLED: Too verbose for normal operation. Uncomment for deep debugging.
#if 0
    DEBUG_ONLY({
        printf("total elapsed since interrupt: %d\n",to_ms_since_boot(get_absolute_time()) - g_interrupt_time);

        enc28j60_dump_tx_registers("FINAL");
    
        printf("=== TX Result Analysis ===\n");
        printf("  other_error: %d, collision_error: %d\n", 
            other_error ? 1 : 0, collision_error ? 1 : 0);
        printf("  transfer_complete: %d, transfer_complete_interrupt_flag: %d, transfer_error_interrupt_flag: %d\n",
            transfer_complete ? 1 : 0, transfer_complete_interrupt_flag ? 1 : 0, transfer_error_interrupt_flag ? 1 : 0);
        printf("  tx_success: %d\n", tx_success ? 1 : 0);
        
        // DETAILED FAILURE ANALYSIS
        if (!tx_success) {
            uint8_t estat = enc28j60_read_register_internal(ENC28J60_ESTAT);
            printf("  ESTAT detailed: 0x%02X (TXABRT=%d, CLKRDY=%d)\n", 
                estat, 
                (estat & ENC28J60_ESTAT_TXABRT) ? 1 : 0,
                (estat & ENC28J60_ESTAT_CLKRDY) ? 1 : 0);
                
            if (transfer_complete_interrupt_flag && !transfer_complete) {
                printf("  DIAGNOSIS: TXIF set but TXRTS not cleared - unusual state\n");
            } else if (!transfer_complete_interrupt_flag && !transfer_error_interrupt_flag) {
                printf("  DIAGNOSIS: No interrupt flags - transmission never started properly\n");
            } else if (transfer_error_interrupt_flag) {
                printf("  DIAGNOSIS: TXERIF set - transmission error occurred\n");
            } else if (transfer_complete_interrupt_flag && transfer_complete) {
                printf("  DIAGNOSIS: Normal completion but success flag not set - check success criteria\n");
            }
            
            bool final_link = enc28j60_get_link_status();
            printf("  Final link status: %s\n", final_link ? "UP" : "DOWN");
        }
        printf("========================\n");
    });
#endif
    
    //refresh flags
    enc28j60_process_interrupts(true);
    enc28j60_clear_tx_interrupt_flags();
    enc28j60_unblock_interrupt();

    if (tx_success) {
        g_enc28j60_state.packets_sent++;
        // DEBUG_ONLY({ printf("ENC28J60: TX SUCCESS - packet sent\n"); });
        //log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_TX, packet->length);
        return true;
    } else {
        g_enc28j60_state.tx_errors++;
        DEBUG_ONLY({ printf("ENC28J60: TX FAILED - packet aborted\n"); });
        return false;
    }
}

/**
 * @brief reset tx after timeout with complete chip soft reset
 * !SEE ERRATA!
 */
static void enc28j60_reset_tx_logic(void) {
    enc28j60_set_register_bank_internal(ERXTX_BANK);
    enc28j60_set_register_bits(ENC28J60_ECON1, ENC28J60_ECON1_TXRST);
    enc28j60_clear_register_bits(ENC28J60_ECON1, ENC28J60_ECON1_TXRST);
    
    // Re-initialize TX FIFO after reset
    enc28j60_write_register_internal(ENC28J60_ETXSTL, TX_BUF_START & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ETXSTH, (TX_BUF_START >> 8) & 0xFF);
}

/**
 * @brief Check if packet transmission is complete
 */
bool enc28j60_is_tx_complete(void) {
    if (!g_driver_initialized) {
        return false;
    }
    
    enc28j60_set_register_bank_internal(ERXTX_BANK);
    
    // Check if TXRTS bit is cleared (transmission complete)
    uint8_t econ1 = enc28j60_read_register_internal(ENC28J60_ECON1);
    return (econ1 & ENC28J60_ECON1_TXRTS) == 0;
}

/**
 * @brief Check if received packet is available
 */
uint8_t enc28j60_has_rx_packet(void) {
    if (!g_driver_initialized) {
        return false;
    }

    enc28j60_set_register_bank_internal(EPKTCNT_BANK);
    uint8_t pktcnt = enc28j60_read_register_internal(ENC28J60_EPKTCNT);

    if( 0 == pktcnt )
    {
        g_last_interrupt_status &= ~ENC28J60_EIR_PKTIF;
        enc28j60_clear_interrupt_pending();
    }
    else {
        /* if we have any packets left, we should make sure they get processed*/
        g_last_interrupt_status |= ENC28J60_EIR_PKTIF;
        g_interrupt_pending = true;
    }
    
    return pktcnt;
}

/**
 * @brief Receive Ethernet packet (Arduino reference implementation)
 */
bool enc28j60_receive_packet(enc28j60_packet_t* packet, uint16_t max_length) {
    
    packet->valid = false; //always false until EVERYTHING OK

    // Process packet receive interrupt

    if (!g_driver_initialized || !packet || !packet->data || max_length == 0) {
        return false;
    }
    
    enc28j60_block_interrupt();

    enc28j60_set_register_bank_internal(ERXTX_BANK);
    
    // CRITICAL FIX: Set ERDPT to point to the next packet location BEFORE reading
    // Without this, ERDPT is at wrong position after the first packet
    enc28j60_write_register_internal(ENC28J60_ERDPTL, g_enc28j60_state.next_packet_ptr & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ERDPTH, (g_enc28j60_state.next_packet_ptr >> 8) & 0xFF);
    
    // Read packet header (6 bytes) - Arduino reference
    // Format: [next_pkt_lo][next_pkt_hi][byte_count_lo][byte_count_hi][status_lo][status_hi]
    uint8_t packet_header[6];
    enc28j60_read_buffer(packet_header, 6);
    
    // Extract next packet pointer and receive byte count from header
    uint16_t next_packet = packet_header[0] | (packet_header[1] << 8);
    uint16_t rx_byte_count = packet_header[2] | (packet_header[3] << 8);
    uint16_t rx_status = packet_header[4] | (packet_header[5] << 8);
    
    // Check receive status bits
    // Bit 7 (0x80): Received OK
    // Bit 4 (0x10): CRC Error  
    // Bit 5 (0x20): Length Check Error
    bool received_ok = (rx_status & 0x80) != 0;
    bool crc_error = (rx_status & 0x10) != 0;
    
    // CRITICAL: rx_byte_count INCLUDES the 4-byte CRC! Subtract it for lwIP.
    // The Ethernet frame passed to lwIP should NOT include the CRC.
    uint16_t packet_length = rx_byte_count - 4;  // Subtract CRC
    
    // DISABLED: Too verbose. Uncomment for packet debugging.
#if 0
    DEBUG_ONLY({
        printf("ENC28J60 RX: byte_count=%d, frame_len=%d, next=0x%04X, status=0x%04X (OK=%d, CRC_ERR=%d)\n", 
               rx_byte_count, packet_length, next_packet, rx_status, received_ok, crc_error);
    });
#endif
    
    // Validate packet - must be received OK and reasonable length
    if (received_ok && !crc_error && 
        packet_length >= 14 && // Minimum Ethernet frame (no payload)
        packet_length <= max_length && 
        packet_length <= ENC28J60_MAX_FRAME_SIZE) {
        
        // Read packet data (excluding CRC - hardware keeps it but we don't need it)
        enc28j60_read_buffer(packet->data, packet_length);

        packet->length = packet_length;
        packet->valid = true;
    }
    else {
        g_enc28j60_state.rx_errors++;
        DEBUG_ONLY({ 
            printf("ENC28J60: invalid packet received, wrong size: %d", packet_length); 
        });
    }
    

    /* Note: We don't need to read past the CRC bytes because we set ERDPT 
     * to the correct position at the start of each packet read */
    
    // CRITICAL FIX: Save the ORIGINAL next_packet pointer BEFORE Errata #14 adjustment
    // This is where ERDPT should point when reading the NEXT packet
    g_enc28j60_state.next_packet_ptr = next_packet;
    
    // Free up the receive buffer space (Errata #14 workaround) - Arduino reference
    // This adjustment is ONLY for ERXRDPT, NOT for ERDPT positioning
    uint16_t erxrdpt_value;
    if (next_packet == RX_BUF_START) {
        erxrdpt_value = RX_BUF_END;
    } else {
        erxrdpt_value = next_packet - 1;
    }

    // Update ERXRDPT to free buffer space (using adjusted value)
    // DEBUG_ONLY({ printf("ENC28J60: Setting ERXRDPT register to 0x%04X\n", erxrdpt_value); });
    
    enc28j60_write_register_internal(ENC28J60_ERXRDPTL, erxrdpt_value & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ERXRDPTH, (erxrdpt_value >> 8) & 0xFF);
    
    // Decrement packet count
    enc28j60_set_register_bits(ENC28J60_ECON2, ENC28J60_ECON2_PKTDEC);
    // DEBUG_ONLY({ printf("ENC28J60: Decremented ECON2 register\n"); });
    
    //update packet counts and interrupt flags
    enc28j60_has_rx_packet();

    enc28j60_unblock_interrupt();

    g_enc28j60_state.packets_received++;
    
    return packet->valid;
}

/**
 * @brief Check interrupt status
 */
uint8_t enc28j60_get_interrupt_status(void) {
    if (!g_driver_initialized) {
        return 0x00;
    }
    
    enc28j60_set_register_bank_internal(ERXTX_BANK);
    return enc28j60_read_register_internal(ENC28J60_EIR);
}

/**
 * @brief Clear interrupt flags
 */
void enc28j60_clear_interrupts(uint8_t flags) {

    if (!g_driver_initialized) {
        return;
    }
    //DEBUG_ONLY({ printf("Clearing interrupt flag in register %d", flags); });

    enc28j60_set_register_bank_internal(ERXTX_BANK);
    enc28j60_clear_register_bits(ENC28J60_EIR, flags);
}

/**
 * @brief Check if interrupt is pending
 */
bool enc28j60_has_pending_interrupt(void) {
    return g_interrupt_pending;
}

/**
 * @brief Process pending interrupts
 */
bool enc28j60_process_interrupts(bool forced) {
    if (!forced && (!g_interrupt_pending || !g_driver_initialized) ) {
        return false;
    }
    //we keep the bits around in g_last_interrupt_status as we might need several rounds to handle everything
    g_last_interrupt_status |= enc28j60_get_interrupt_status();    
    
    //clear all in the hardware
    enc28j60_clear_interrupts(0xFF);    

    // DISABLED: Too verbose for normal operation. Uncomment for deep debugging.
#if 0
    DEBUG_ONLY({
    if(g_last_interrupt_status!=0)
    {
        printf("Processing pending interrupt flags:\n ENC28J60_EIR_LINKIF: %d\n ENC28J60_EIR_PKTIF: %d\n ENC28J60_EIR_RXERIF: %d\n ENC28J60_EIR_TXERIF: %d\n ENC28J60_EIR_TXIF: %d\n ",
            g_last_interrupt_status & ENC28J60_EIR_LINKIF,
            g_last_interrupt_status & ENC28J60_EIR_PKTIF,
            g_last_interrupt_status & ENC28J60_EIR_RXERIF,
            g_last_interrupt_status & ENC28J60_EIR_TXERIF,
            g_last_interrupt_status & ENC28J60_EIR_TXIF);
    }
    });
#endif
    return true;
}
/**
 * @brief Clear g_interrupt_pending if every flag was handled
 */
 void enc28j60_clear_interrupt_pending(void)
 {
    // no interrupt unhandled?
    if(0 == ( g_last_interrupt_status & (ENC28J60_EIR_LINKIF 
                                        |ENC28J60_EIR_PKTIF
                                        |ENC28J60_EIR_RXERIF
                                        |ENC28J60_EIR_TXERIF
                                        |ENC28J60_EIR_TXIF                                        
                                         ) )) {
        if(!g_interrupt_pending)
        {
            // Clear the interrupt pending flag
            g_interrupt_pending = false;
            // DEBUG: Interrupt pending flag cleared (removed spam)
        }
    }
    else {
        // DISABLED: Too verbose. Uncomment for deep debugging.
#if 0
        DEBUG_ONLY({
        printf("Checking remaining interrupt flags:\n ENC28J60_EIR_LINKIF: %d\n ENC28J60_EIR_PKTIF: %d\n ENC28J60_EIR_RXERIF: %d\n ENC28J60_EIR_TXERIF: %d\n ENC28J60_EIR_TXIF: %d\n",
            g_last_interrupt_status & ENC28J60_EIR_LINKIF,
            g_last_interrupt_status & ENC28J60_EIR_PKTIF,
            g_last_interrupt_status & ENC28J60_EIR_RXERIF,
            g_last_interrupt_status & ENC28J60_EIR_TXERIF,
            g_last_interrupt_status & ENC28J60_EIR_TXIF);
        });
#endif
    }
 }



/**
 * @brief check if LINKIF is set
 */
bool enc28j60_has_link_change_pending(void)
{
    return (g_last_interrupt_status & ENC28J60_EIR_LINKIF);
}

/**
 * @brief Process LINKIF interrupts
 */
bool enc28j60_process_linkif_interrupt(void)
{
    bool link_up = false;
    // Process link status change interrupt
    if (g_last_interrupt_status & ENC28J60_EIR_LINKIF) {
        link_up = enc28j60_get_link_status();
        
        //clear the bit 
        g_last_interrupt_status &= ~ENC28J60_EIR_LINKIF;
        enc28j60_clear_interrupt_pending();
    }
    return link_up;
}


/**
 * @brief check if TXIF is set
 */
bool enc28j60_has_txif_pending(void)
{
    return (g_last_interrupt_status & ENC28J60_EIR_TXIF);
}

/**
 * @brief check if TXIF is set
 */
bool enc28j60_has_txerif_pending(void)
{
    return (g_last_interrupt_status & ENC28J60_EIR_TXERIF);
}


/**
 * @brief clear TXIF and TXEIF interrupts
 */
bool enc28j60_clear_tx_interrupt_flags(void)
{
    // Process packet transmitted interrupt
    if (g_last_interrupt_status & (ENC28J60_EIR_TXIF | ENC28J60_EIE_TXERIE) ) {
        
        //clear the bits 
        g_last_interrupt_status &= ~(ENC28J60_EIR_TXIF | ENC28J60_EIE_TXERIE);
        enc28j60_clear_interrupt_pending();
    }
    return true;
}


/**
 * @brief Read PHY register via MII interface (Arduino reference implementation)
 */
static uint16_t enc28j60_read_phy_register(uint8_t phy_reg) {
    if (!g_driver_initialized) {
        return 0xFFFF;
    }
    enc28j60_block_interrupt();

    // Set bank 2 for MII access (Arduino reference)
    enc28j60_set_register_bank_internal(MACONX_BANK);
    
    // Set the PHY register address to read
    enc28j60_write_register_internal(ENC28J60_MIREGADR, phy_reg);
    
    // Start the PHY read operation
    enc28j60_write_register_internal(ENC28J60_MICMD, ENC28J60_MICMD_MIIRD);
    
    // Wait for the PHY read to complete (Arduino reference)
    // MISTAT is in Bank 3
    enc28j60_set_register_bank_internal(MAADRX_BANK);  // Bank 3 for MISTAT
        
    uint32_t timeout = 1000;  // 1ms timeout
    while (timeout > 0) {
        uint8_t mistat = enc28j60_read_register_internal(ENC28J60_MISTAT);
        if ((mistat & ENC28J60_MISTAT_BUSY) == 0) {
            break;  // Read complete
        }
        sleep_us(1);
        timeout--;
    }
    
    if(timeout == 0)
    {
        // TBD: we should do something to mitigate the error
        DEBUG_ONLY({ 
            printf("ENC28J60: TIMEOUT AFTER READING PHY REGISTER: %X, timeout: %d", phy_reg,timeout); 
        });
    }
    // Return to Bank 2 for reading result
    enc28j60_set_register_bank_internal(MACONX_BANK);
    
    // Clear the read command
    enc28j60_write_register_internal(ENC28J60_MICMD, 0x00);
    
    // Read the result from MIRDL and MIRDH registers
    uint8_t low_byte = enc28j60_read_register_internal(ENC28J60_MIRDL);
    uint8_t high_byte = enc28j60_read_register_internal(ENC28J60_MIRDH);
    
    enc28j60_unblock_interrupt();
    

    return (high_byte << 8) | low_byte;
}

/**
 * @brief Write PHY register via MII interface
 *
 * Per ENC28J60 datasheet Section 3.3.2:
 * 1. Write PHY register address to MIREGADR
 * 2. Write low byte to MIWRL
 * 3. Write high byte to MIWRH (triggers the write)
 * 4. Wait for MISTAT.BUSY to clear
 *
 * @param phy_reg PHY register address (e.g. ENC28J60_PHCON1)
 * @param value   16-bit value to write
 */
static void enc28j60_write_phy_register(uint8_t phy_reg, uint16_t value) {
    if (!g_driver_initialized) {
        return;
    }
    enc28j60_block_interrupt();

    // Set bank 2 for MII access
    enc28j60_set_register_bank_internal(MACONX_BANK);

    // Set the PHY register address
    enc28j60_write_register_internal(ENC28J60_MIREGADR, phy_reg);

    // Write low byte first, then high byte (high byte triggers the write)
    enc28j60_write_register_internal(ENC28J60_MIWRL, (uint8_t)(value & 0xFF));
    enc28j60_write_register_internal(ENC28J60_MIWRH, (uint8_t)(value >> 8));

    // Wait for the PHY write to complete
    // MISTAT is in Bank 3
    enc28j60_set_register_bank_internal(MAADRX_BANK);

    uint32_t timeout = 1000;  // 1ms timeout
    while (timeout > 0) {
        uint8_t mistat = enc28j60_read_register_internal(ENC28J60_MISTAT);
        if ((mistat & ENC28J60_MISTAT_BUSY) == 0) {
            break;
        }
        sleep_us(1);
        timeout--;
    }

    if (timeout == 0) {
        printf("ENC28J60: TIMEOUT writing PHY register 0x%02X\n", phy_reg);
    }

    enc28j60_unblock_interrupt();
}

/**
 * @brief Get link status by reading PHY register (Arduino reference implementation)
 */
bool enc28j60_get_link_status(void) {
    static uint32_t last_print_time = 0;
    static bool last_link_state = false;
    
    if (!g_driver_initialized) {
        return false;
    }
    // Read PHY Status Register 2 (PHSTAT2) and check link status bit
    // Arduino reference: phyread(MACSTAT2) & 0x400
    uint16_t phstat2 = enc28j60_read_phy_register(ENC28J60_PHSTAT2);
    
    // Check bit 10 (0x0400) - Link Status bit
    bool link_up = (phstat2 & ENC28J60_PHSTAT2_LSTAT) != 0;
    
    // Only print on state change or every 5 seconds to reduce spam
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (link_up != last_link_state || (now - last_print_time) > 5000) {
        //printf("ENC28J60: link state: %d (PHSTAT2=0x%04X)\n", link_up, phstat2);
        last_print_time = now;
        last_link_state = link_up;
    }
    
    return link_up;
}



/**
 * @brief Read PHY register via MII interface (copy from driver)
 * Note: This is a copy of the internal function from enc28j60_driver.c
 */
static uint16_t enc28j60_read_phy_register_dump(uint8_t phy_reg) {
    // Set bank 2 for MII access
    enc28j60_set_bank(2);
    
    // Set the PHY register address to read
    enc28j60_write_register(0x14, phy_reg); // MIREGADR
    
    // Start the PHY read operation
    enc28j60_write_register(0x12, 0x01); // MICMD = MIIRD
    
    // Wait for the PHY read to complete
    // MISTAT is in Bank 3
    enc28j60_set_bank(3);
        
    uint32_t timeout = 1000;  // 1ms timeout
    while (timeout > 0) {
        uint8_t mistat = enc28j60_read_register(0x0A); // MISTAT
        if ((mistat & 0x01) == 0) { // BUSY bit
            break;  // Read complete
        }
        timeout--;
    }
    
    if (timeout == 0) {
        printf("  WARNING: PHY register 0x%02X read timeout!\n", phy_reg);
        return 0xFFFF;
    }
    
    // Return to Bank 2 for reading result
    enc28j60_set_bank(2);
    
    // Clear the read command
    enc28j60_write_register(0x12, 0x00); // MICMD = 0
    
    // Read the result from MIRDL and MIRDH registers
    uint8_t low_byte = enc28j60_read_register(0x18);  // MIRDL
    uint8_t high_byte = enc28j60_read_register(0x19); // MIRDH
    
    return (high_byte << 8) | low_byte;
}

/**
 * @brief Dump all ENC28J60 registers relevant for signal quality analysis
 * 
 * This function outputs comprehensive register information to help diagnose
 * signal quality issues, packet loss, and communication problems.
 * Call this function when experiencing packet loss or communication issues.
 */
void enc28j60_dump_signal_quality_registers(void) {
    if (!enc28j60_is_ready()) {
        printf("ENC28J60 Signal Quality Dump: Driver not ready!\n");
        return;
    }
    
    printf("\n");
    printf("================================================================================\n");
    printf("                    ENC28J60 SIGNAL QUALITY ANALYSIS DUMP\n");
    printf("================================================================================\n");
    
    // Get driver statistics first
    const enc28j60_state_t* state = enc28j60_get_state();
    
    printf("\n--- DRIVER STATISTICS ---\n");
    printf("  Initialized:              %s\n", state->initialized ? "YES" : "NO");
    printf("  Packets Sent:             %" PRIu32 "\n", state->packets_sent);
    printf("  Packets Received:         %" PRIu32 "\n", state->packets_received);
    printf("  TX Errors:                %" PRIu32 "\n", state->tx_errors);
    printf("  RX Errors:                %" PRIu32 "\n", state->rx_errors);
    printf("  False Collisions:         %" PRIu32 "\n", state->likely_false_collisions);
    printf("  Next Packet Pointer:      0x%04X\n", state->next_packet_ptr);
    
    // === CONTROL AND STATUS REGISTERS (Available in all banks) ===
    printf("\n--- CONTROL/STATUS REGISTERS (All Banks) ---\n");
    
    // Read control registers (save current bank first)
    uint8_t saved_bank = enc28j60_read_register(0x1F) & 0x03; // Current ECON1 bank bits
    
    uint8_t econ1 = enc28j60_read_register(0x1F);  // ECON1
    uint8_t econ2 = enc28j60_read_register(0x1E);  // ECON2  
    uint8_t estat = enc28j60_read_register(0x1D);  // ESTAT
    uint8_t eir = enc28j60_read_register(0x1C);    // EIR
    uint8_t eie = enc28j60_read_register(0x1B);    // EIE
    
    printf("  ECON1 (0x1F):             0x%02X\n", econ1);
    printf("    TXRTS (Transmit Req):   %s\n", (econ1 & 0x08) ? "ACTIVE" : "idle");
    printf("    RXEN (Receive Enable):  %s\n", (econ1 & 0x04) ? "ENABLED" : "disabled"); 
    printf("    TXRST (TX Reset):       %s\n", (econ1 & 0x80) ? "RESET" : "normal");
    printf("    RXRST (RX Reset):       %s\n", (econ1 & 0x40) ? "RESET" : "normal");
    printf("    Current Bank:           %d\n", econ1 & 0x03);
    
    printf("  ECON2 (0x1E):             0x%02X\n", econ2);
    printf("    AUTOINC (Auto Incr):    %s\n", (econ2 & 0x80) ? "ENABLED" : "disabled");
    printf("    PKTDEC (Packet Dec):    %s\n", (econ2 & 0x40) ? "ACTIVE" : "idle");
    
    printf("  ESTAT (0x1D):             0x%02X\n", estat);
    printf("    CLKRDY (Clock Ready):   %s\n", (estat & 0x01) ? "READY" : "NOT READY");
    printf("    TXABRT (TX Abort):      %s\n", (estat & 0x02) ? "ABORTED" : "normal");
    
    printf("  EIR (0x1C):               0x%02X\n", eir);
    printf("    PKTIF (RX Packet):      %s\n", (eir & 0x40) ? "PENDING" : "none");
    printf("    LINKIF (Link Change):   %s\n", (eir & 0x10) ? "PENDING" : "none");
    printf("    TXIF (TX Complete):     %s\n", (eir & 0x08) ? "PENDING" : "none");
    printf("    TXERIF (TX Error):      %s\n", (eir & 0x02) ? "ERROR" : "none");
    printf("    RXERIF (RX Error):      %s\n", (eir & 0x01) ? "ERROR" : "none");
    
    printf("  EIE (0x1B):               0x%02X\n", eie);
    printf("    INTIE (Global Int):     %s\n", (eie & 0x80) ? "ENABLED" : "disabled");
    printf("    PKTIE (RX Int):         %s\n", (eie & 0x40) ? "ENABLED" : "disabled");
    printf("    LINKIE (Link Int):      %s\n", (eie & 0x10) ? "ENABLED" : "disabled");
    printf("    TXIE (TX Int):          %s\n", (eie & 0x08) ? "ENABLED" : "disabled");
    printf("    TXERIE (TX Err Int):    %s\n", (eie & 0x02) ? "ENABLED" : "disabled");
    
    // === BANK 0 - BUFFER CONTROL REGISTERS ===
    printf("\n--- BANK 0 - BUFFER CONTROL REGISTERS ---\n");
    enc28j60_set_bank(0);
    
    uint16_t erxst = (enc28j60_read_register(0x09) << 8) | enc28j60_read_register(0x08);   // ERXST
    uint16_t erxnd = (enc28j60_read_register(0x0B) << 8) | enc28j60_read_register(0x0A);   // ERXND
    uint16_t erxrdpt = (enc28j60_read_register(0x0D) << 8) | enc28j60_read_register(0x0C); // ERXRDPT
    uint16_t erdpt = (enc28j60_read_register(0x01) << 8) | enc28j60_read_register(0x00);   // ERDPT
    uint16_t ewrpt = (enc28j60_read_register(0x03) << 8) | enc28j60_read_register(0x02);   // EWRPT
    uint16_t etxst = (enc28j60_read_register(0x05) << 8) | enc28j60_read_register(0x04);   // ETXST
    uint16_t etxnd = (enc28j60_read_register(0x07) << 8) | enc28j60_read_register(0x06);   // ETXND
    
    printf("  RX Buffer Configuration:\n");
    printf("    ERXST (RX Start):       0x%04X\n", erxst);
    printf("    ERXND (RX End):         0x%04X\n", erxnd);
    printf("    ERXRDPT (RX Read Ptr):  0x%04X", erxrdpt);
    if (erxrdpt % 2 == 0 && erxrdpt != 0) {
        printf(" *** WARNING: Even ERXRDPT value (Errata #14) ***");
    }
    printf("\n");
    printf("    ERDPT (Read Ptr):       0x%04X\n", erdpt);
    printf("    RX Buffer Size:         %d bytes\n", erxnd - erxst + 1);
    
    printf("  TX Buffer Configuration:\n");
    printf("    ETXST (TX Start):       0x%04X\n", etxst);
    printf("    ETXND (TX End):         0x%04X\n", etxnd);
    printf("    EWRPT (Write Ptr):      0x%04X\n", ewrpt);
    printf("    TX Buffer Size:         %d bytes\n", etxnd - etxst + 1);
    
    // === BANK 1 - PACKET COUNT AND FILTER CONTROL ===
    printf("\n--- BANK 1 - PACKET COUNT AND FILTER CONTROL ---\n");
    enc28j60_set_bank(1);
    
    uint8_t epktcnt = enc28j60_read_register(0x19);  // EPKTCNT
    uint8_t erxfcon = enc28j60_read_register(0x18);  // ERXFCON
    
    printf("  EPKTCNT (Packet Count):   %d\n", epktcnt);
    printf("  ERXFCON (RX Filter):      0x%02X\n", erxfcon);
    printf("    UCEN (Unicast):         %s\n", (erxfcon & 0x80) ? "ENABLED" : "disabled");
    printf("    ANDOR (Filter Logic):   %s\n", (erxfcon & 0x40) ? "OR" : "AND");
    printf("    CRCEN (CRC Check):      %s\n", (erxfcon & 0x20) ? "ENABLED" : "disabled");
    printf("    MCEN (Multicast):       %s\n", (erxfcon & 0x02) ? "ENABLED" : "disabled");
    printf("    BCEN (Broadcast):       %s\n", (erxfcon & 0x01) ? "ENABLED" : "disabled");
    
    // === BANK 2 - MAC CONTROL REGISTERS ===
    printf("\n--- BANK 2 - MAC CONTROL REGISTERS ---\n");
    enc28j60_set_bank(2);
    
    uint8_t macon1 = enc28j60_read_register(0x00);   // MACON1
    uint8_t macon3 = enc28j60_read_register(0x02);   // MACON3
    uint16_t mamxfl = (enc28j60_read_register(0x0B) << 8) | enc28j60_read_register(0x0A); // MAMXFL
    uint8_t mabbipg = enc28j60_read_register(0x04);  // MABBIPG
    uint8_t maipgl = enc28j60_read_register(0x06);   // MAIPGL
    
    printf("  MACON1 (MAC Control 1):   0x%02X\n", macon1);
    printf("    MARXEN (RX Enable):     %s\n", (macon1 & 0x01) ? "ENABLED" : "disabled");
    printf("    RXPAUS (RX Pause):      %s\n", (macon1 & 0x04) ? "ENABLED" : "disabled");
    printf("    TXPAUS (TX Pause):      %s\n", (macon1 & 0x08) ? "ENABLED" : "disabled");
    
    printf("  MACON3 (MAC Control 3):   0x%02X\n", macon3);
    printf("    FULDPX (Full Duplex):   %s\n", (macon3 & 0x01) ? "ENABLED" : "disabled");
    printf("    FRMLNEN (Frame Len):    %s\n", (macon3 & 0x02) ? "ENABLED" : "disabled");
    printf("    TXCRCEN (TX CRC):       %s\n", (macon3 & 0x10) ? "ENABLED" : "disabled");
    printf("    PADCFG (Padding):       0x%02X\n", (macon3 & 0xE0) >> 5);
    
    printf("  MAMXFL (Max Frame Len):   %d bytes\n", mamxfl);
    printf("  MABBIPG (Back-to-Back):   %d\n", mabbipg);
    printf("  MAIPGL (Non-Back-to-Back): %d\n", maipgl);
    
    // === BANK 3 - MAC ADDRESS AND CHIP INFO ===
    printf("\n--- BANK 3 - MAC ADDRESS AND CHIP INFO ---\n");
    enc28j60_set_bank(3);
    
    uint8_t mac_addr[6];
    mac_addr[0] = enc28j60_read_register(0x04);  // MAADR1
    mac_addr[1] = enc28j60_read_register(0x05);  // MAADR2  
    mac_addr[2] = enc28j60_read_register(0x02);  // MAADR3
    mac_addr[3] = enc28j60_read_register(0x03);  // MAADR4
    mac_addr[4] = enc28j60_read_register(0x00);  // MAADR5
    mac_addr[5] = enc28j60_read_register(0x01);  // MAADR6
    
    uint8_t erevid = enc28j60_read_register(0x12);   // EREVID
    
    printf("  MAC Address:              %02X:%02X:%02X:%02X:%02X:%02X\n", 
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    printf("  EREVID (Chip Revision):   0x%02X", erevid);
    switch(erevid) {
        case 0x02: printf(" (Rev B1)"); break;
        case 0x04: printf(" (Rev B4)"); break; 
        case 0x05: printf(" (Rev B5)"); break;
        case 0x06: printf(" (Rev B7)"); break;
        default: printf(" (Unknown)"); break;
    }
    printf("\n");
    
    // === PHY REGISTERS (Signal Quality Critical) ===
    printf("\n--- PHY REGISTERS (Signal Quality Critical) ---\n");
    
    uint16_t phcon1 = enc28j60_read_phy_register_dump(0x00);   // PHCON1
    uint16_t phstat1 = enc28j60_read_phy_register_dump(0x01);  // PHSTAT1
    uint16_t phcon2 = enc28j60_read_phy_register_dump(0x10);   // PHCON2
    uint16_t phstat2 = enc28j60_read_phy_register_dump(0x11);  // PHSTAT2
    
    printf("  PHCON1 (PHY Control 1):   0x%04X\n", phcon1);
    printf("    PDPXMD (Duplex Mode):   %s\n", (phcon1 & 0x0100) ? "Full Duplex" : "Half Duplex");
    printf("    PLOOPBK (Loopback):     %s\n", (phcon1 & 0x4000) ? "ENABLED" : "disabled");
    
    printf("  PHSTAT1 (PHY Status 1):   0x%04X\n", phstat1);
    printf("    LLSTAT (Link Status):   %s\n", (phstat1 & 0x0004) ? "LINKED" : "not linked");
    printf("    JBSTAT (Jabber):        %s\n", (phstat1 & 0x0002) ? "DETECTED" : "none");
    
    printf("  PHCON2 (PHY Control 2):   0x%04X\n", phcon2);
    printf("    HDLDIS (Half-Dup Dis):  %s\n", (phcon2 & 0x0100) ? "DISABLED" : "enabled");
    printf("    FRCLNK (Force Link):    %s\n", (phcon2 & 0x4000) ? "FORCED" : "normal");
    
    printf("  PHSTAT2 (PHY Status 2):   0x%04X\n", phstat2);
    printf("    LSTAT (Link Status):    %s\n", (phstat2 & 0x0400) ? "LINKED" : "not linked");
    printf("    DPXSTAT (Duplex Stat):  %s\n", (phstat2 & 0x0200) ? "Full Duplex" : "Half Duplex");
    printf("    TXSTAT (TX Status):     %s\n", (phstat2 & 0x2000) ? "TRANSMITTING" : "idle");
    printf("    RXSTAT (RX Status):     %s\n", (phstat2 & 0x1000) ? "RECEIVING" : "idle");
    printf("    COLSTAT (Collision):    %s\n", (phstat2 & 0x0800) ? "DETECTED" : "none");
    printf("    PLRITY (Polarity):      %s\n", (phstat2 & 0x0020) ? "CORRECTED" : "normal");
    
    // === SIGNAL QUALITY ANALYSIS ===
    printf("\n--- SIGNAL QUALITY ANALYSIS ---\n");
    
    // Check for common signal quality issues based on errata
    bool issues_found = false;
    
    if (!(estat & 0x01)) {
        printf("  *** CRITICAL: Clock not ready (ESTAT.CLKRDY = 0) ***\n");
        issues_found = true;
    }
    
    if (estat & 0x02) {
        printf("  *** ERROR: Transmit abort detected (ESTAT.TXABRT = 1) ***\n");
        issues_found = true;
    }
    
    if (eir & 0x02) {
        printf("  *** ERROR: Transmit error interrupt pending (EIR.TXERIF = 1) ***\n");
        issues_found = true;
    }
    
    if (eir & 0x01) {
        printf("  *** ERROR: Receive error interrupt pending (EIR.RXERIF = 1) ***\n");
        issues_found = true;
    }
    
    if (erxrdpt % 2 == 0 && erxrdpt != 0) {
        printf("  *** WARNING: ERXRDPT has even value (Errata #14 - may corrupt RX buffer) ***\n");
        issues_found = true;
    }
    
    if (!(phstat1 & 0x0004) || !(phstat2 & 0x0400)) {
        printf("  *** WARNING: PHY reports link down - check cable/connection ***\n");
        issues_found = true;
    }
    
    if (phstat1 & 0x0002) {
        printf("  *** WARNING: Jabber condition detected - possible signal integrity issue ***\n");
        issues_found = true;
    }
    
    if (phstat2 & 0x0020) {
        printf("  *** INFO: Polarity correction active (Errata #7 - verify TPIN+/- wiring) ***\n");
        issues_found = true;
    }
    
    if (state->tx_errors > 0) {
        printf("  *** WARNING: %d transmit errors recorded ***\n", (int)state->tx_errors);
        issues_found = true;
    }
    
    if (state->rx_errors > 0) {
        printf("  *** WARNING: %d receive errors recorded ***\n", (int)state->rx_errors);
        issues_found = true;
    }
    
    if (state->likely_false_collisions > 0) {
        printf("  *** INFO: %d false collision detections (Errata #15) ***\n", (int)state->likely_false_collisions);
        issues_found = true;
    }
    
    if (!issues_found) {
        printf("  No obvious signal quality issues detected.\n");
        printf("  If experiencing packet loss, consider:\n");
        printf("  - Check RBIAS resistor value for your chip revision (Errata #8)\n");
        printf("  - Verify TPIN+/TPIN- wiring polarity (Errata #7)\n");
        printf("  - Check SPI clock frequency >= 8MHz (Errata #1)\n");
        printf("  - Monitor for intermittent issues over time\n");
    }
    
    // Restore original bank
    enc28j60_set_bank(saved_bank);
    
    printf("\n================================================================================\n");
    printf("                         END SIGNAL QUALITY ANALYSIS\n");
    printf("================================================================================\n\n");
}
