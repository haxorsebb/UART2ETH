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

#include "network/enc28j60_driver.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "log_manager.h"
#include <string.h>
#include <stdio.h>

// Driver state (Arduino-style state management)
static enc28j60_state_t g_enc28j60_state = {0};
static bool g_driver_initialized = false;
static uint8_t g_current_bank = 0xFF; // Track current register bank (Arduino pattern)
static const uint8_t* g_local_mac = NULL; // MAC address storage (Arduino pattern)

// SPI configuration (RP2350-specific)
#define SPI_PORT spi0
#define SPI_BAUDRATE 20000000  // 20MHz SPI clock (max for ENC28J60)

// Buffer addresses (exact Arduino reference values)
#define RX_BUF_START 0x0000
#define RX_BUF_END   0x0FFF  // 4KB RX buffer (Arduino reference)
#define TX_BUF_START 0x1200  // TX buffer start (Arduino reference)

// Maximum frame length (Arduino reference)
#define MAX_MAC_LENGTH 1518

// Bank definitions (Arduino reference)
#define ERXTX_BANK   0x00  // Bank 0 - Buffer control registers
#define EPKTCNT_BANK 0x01  // Bank 1 - Packet count and filter control
#define MACONX_BANK  0x02  // Bank 2 - MAC control registers
#define MAADRX_BANK  0x03  // Bank 3 - MAC address registers

// Interrupt handling state
static volatile bool g_interrupt_pending = false;
static volatile uint8_t g_last_interrupt_status = 0;

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
static void enc28j60_interrupt_handler(uint gpio, uint32_t events);
static bool enc28j60_is_mac_mii_register(uint8_t reg);
static uint8_t enc28j60_read_register_internal(uint8_t reg);
static void enc28j60_write_register_internal(uint8_t reg, uint8_t value);
static void enc28j60_set_register_bank_internal(uint8_t new_bank);
static uint16_t enc28j60_read_phy_register(uint8_t phy_reg);

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
            return reg <= 0x05 || reg == ENC28J60_MISTAT; // MAADR2 is 0x05
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
    sleep_us(1);  // Setup time (Arduino reference)
}

/**
 * @brief SPI chip deselect (Arduino reference timing)
 */
static void enc28j60_arch_spi_deselect(void) {
    sleep_us(1);  // Hold time (Arduino reference)
    gpio_put(ENC28J60_CS_PIN, 1);
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
 * @brief Soft reset (Arduino reference exact sequence)
 */
void enc28j60_reset(void) {
    printf("ENC28J60: Performing soft reset\n");
    
    // Ensure CS is high before reset
    gpio_put(ENC28J60_CS_PIN, 1);
    sleep_us(10);
    
    // Perform SPI soft reset command (Arduino reference sequence)
    enc28j60_arch_spi_select();
    enc28j60_spi_transfer(ENC28J60_SOFT_RESET);
    enc28j60_arch_spi_deselect();
    
    // Reset bank tracking - after reset, we're in bank 0 (Arduino reference)
    g_current_bank = ERXTX_BANK;
    
    // Arduino reference: "Workaround for erratum #2" + wait for reset
    sleep_us(1000);  // 1ms delay (Arduino reference)
    
    printf("ENC28J60: Soft reset complete\n");
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_RESET, 0);
}

/**
 * @brief Wait for oscillator ready (Arduino reference implementation)
 */
static bool enc28j60_wait_for_osc_ready(void) {
    uint32_t timeout = 50000;  // 50ms timeout
    
    // Ensure we're in bank 0 for ESTAT register
    g_current_bank = ERXTX_BANK;
    
    printf("ENC28J60: Waiting for oscillator ready\n");
    
    while (timeout > 0) {
        uint8_t estat = enc28j60_read_register_internal(ENC28J60_ESTAT);
        
        if (estat != 0xFF && (estat & ENC28J60_ESTAT_CLKRDY)) {
            printf("ENC28J60: Oscillator ready\n");
            return true;
        }
        
        sleep_us(100);  // Wait 100us between attempts
        timeout -= 100;
    }
    
    printf("ENC28J60: Oscillator timeout\n");
    return false;
}

/**
 * @brief Configure buffer memory layout (Arduino reference exact sequence)
 */
static void enc28j60_configure_buffers(void) {
    printf("ENC28J60: Configuring buffers\n");
    
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
    
    // Configure receive filters in Bank 1 (Arduino reference)
    enc28j60_set_register_bank_internal(EPKTCNT_BANK);
    // ERXFCON: Enable unicast (UCEN), CRC check (CRCEN), multicast (MCEN), and broadcast (BCEN) for DHCP
    uint8_t erxfcon_value = ENC28J60_ERXFCON_UCEN | ENC28J60_ERXFCON_CRCEN | ENC28J60_ERXFCON_MCEN | ENC28J60_ERXFCON_BCEN;
    enc28j60_write_register_internal(ENC28J60_ERXFCON, erxfcon_value);
    
    printf("ENC28J60: Buffer configuration complete\n");
}

/**
 * @brief Configure MAC layer (Arduino reference exact sequence)
 */
static void enc28j60_configure_mac(void) {
    printf("ENC28J60: Configuring MAC\n");
    
    // Configure Bank 2 MAC registers - follow Arduino exact sequence
    enc28j60_set_register_bank_internal(MACONX_BANK);
    
    // Arduino step 1: Turn on reception and flow control
    enc28j60_set_register_bits(ENC28J60_MACON1, 
        ENC28J60_MACON1_MARXEN | ENC28J60_MACON1_TXPAUS | ENC28J60_MACON1_RXPAUS);
    
    // Arduino step 2: Set padding, crc, full duplex
    enc28j60_set_register_bits(ENC28J60_MACON3, 
        ENC28J60_MACON3_PADCFG_FULL | ENC28J60_MACON3_TXCRCEN | 
        ENC28J60_MACON3_FULDPX | ENC28J60_MACON3_FRMLNEN);
    
    // Arduino step 3: Set maximum frame length
    enc28j60_write_register_internal(ENC28J60_MAMXFLL, MAX_MAC_LENGTH & 0xFF);
    enc28j60_write_register_internal(ENC28J60_MAMXFLH, (MAX_MAC_LENGTH >> 8) & 0xFF);
    
    // Arduino step 4: Set back-to-back inter packet gap
    enc28j60_write_register_internal(ENC28J60_MABBIPG, 0x15);
    
    // Arduino step 5: Set non-back-to-back inter packet gap
    enc28j60_write_register_internal(ENC28J60_MAIPGL, 0x12);
    
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
    
    printf("ENC28J60: MAC configuration complete\n");
}

/**
 * @brief Configure PHY layer (minimal for basic operation)
 */
static void enc28j60_configure_phy(void) {
    printf("ENC28J60: PHY configuration (using defaults)\n");
    // Use default PHY settings for basic operation
    // No PHY register writes needed for minimal functionality
}

/**
 * @brief Initialize ENC28J60 driver (Arduino reference exact sequence)
 */
bool enc28j60_init(void) {
    printf("ENC28J60: Starting initialization\n");
    
    if (g_driver_initialized) {
        printf("ENC28J60: Already initialized\n");
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_INIT, 1);
        return true;
    }
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, 0);
    
    // Initialize SPI and GPIO first (RP2350-specific)
    enc28j60_spi_init();
    enc28j60_gpio_init();
    
    // Ensure CS is deasserted and interface stable
    gpio_put(ENC28J60_CS_PIN, 1);
    sleep_ms(10);
    
    // Arduino reference initialization sequence:
    
    // 1. Perform software reset
    enc28j60_reset();
    
    // 2. Wait for oscillator ready
    if (!enc28j60_wait_for_osc_ready()) {
        printf("ENC28J60: Oscillator not ready - init failed\n");
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, 1);
        return false;
    }
    
    // 3. Configure buffer memory layout
    enc28j60_configure_buffers();
    
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
    printf("ENC28J60: Chip revision = 0x%02X\n", revid);
    
    // 8. Enable interrupts BEFORE enabling reception
    // This ensures interrupt handling is fully ready when packets arrive
    enc28j60_enable_interrupts();
    
    // 9. Turn on reception (FINAL STEP - all infrastructure ready)
    // Once RXEN is set, packets can start arriving and triggering interrupts
    enc28j60_set_register_bank_internal(ERXTX_BANK);
    enc28j60_write_register_internal(ENC28J60_ECON1, ENC28J60_ECON1_RXEN);
    
    // Verify initialization by checking ECON1
    uint8_t econ1_check = enc28j60_read_register_internal(ENC28J60_ECON1);
    if ((econ1_check & ENC28J60_ECON1_RXEN) == 0) {
        printf("ENC28J60: RXEN verification failed\n");
        log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_ERROR, LOG_EVENT_NETWORK_ERROR, 2);
        return false;
    }
    
    // Initialize driver state
    memset(&g_enc28j60_state, 0, sizeof(g_enc28j60_state));
    g_enc28j60_state.initialized = true;
    g_enc28j60_state.next_packet_ptr = RX_BUF_START;
    
    g_driver_initialized = true;
    
    printf("ENC28J60: Initialization complete\n");
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_INFO, LOG_EVENT_NETWORK_INIT, 2);
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
    gpio_set_irq_enabled(ENC28J60_INTERRUPT_PIN, GPIO_IRQ_EDGE_FALL, false);
    printf("ENC28J60: GPIO interrupts disabled\n");
    
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
 * @brief Check if ENC28J60 is ready for operation
 */
bool enc28j60_is_ready(void) {
    if (!g_driver_initialized) {
        return false;
    }
    
    // Check if oscillator is ready
    enc28j60_set_register_bank_internal(ERXTX_BANK);
    uint8_t estat = enc28j60_read_register_internal(ENC28J60_ESTAT);
    return (estat != 0xFF) && (estat & ENC28J60_ESTAT_CLKRDY);
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
        // Read interrupt status to clear the interrupt
        g_last_interrupt_status = enc28j60_read_register_internal(ENC28J60_EIR);
        g_interrupt_pending = true;
        
        printf("INT: ENC28J60 interrupt triggered, EIR=0x%02X\n", g_last_interrupt_status);
    }
}

/**
 * @brief Initialize GPIO pins (RP2350-specific)
 */
static void enc28j60_gpio_init(void) {
    // Initialize CS pin as output (high = deselected)
    gpio_init(ENC28J60_CS_PIN);
    gpio_set_dir(ENC28J60_CS_PIN, GPIO_OUT);
    gpio_put(ENC28J60_CS_PIN, 1);
    
    // Initialize interrupt pin as input with pull-up
    gpio_init(ENC28J60_INTERRUPT_PIN);
    gpio_set_dir(ENC28J60_INTERRUPT_PIN, GPIO_IN);
    gpio_pull_up(ENC28J60_INTERRUPT_PIN);
    
    // Set up interrupt handler for falling edge (active low interrupt)
    gpio_set_irq_enabled_with_callback(ENC28J60_INTERRUPT_PIN, GPIO_IRQ_EDGE_FALL, 
                                       true, &enc28j60_interrupt_handler);
    
    printf("ENC28J60: GPIO interrupt handler configured for pin %d\n", ENC28J60_INTERRUPT_PIN);
}

/**
 * @brief Enable ENC28J60 interrupts
 */
static void enc28j60_enable_interrupts(void) {
    // Clear any pending interrupts first
    enc28j60_clear_register_bits(ENC28J60_EIR, 0xFF);
    
    // Enable global interrupts and specific interrupt sources
    uint8_t eie = ENC28J60_EIE_INTIE | ENC28J60_EIE_PKTIE | ENC28J60_EIE_LINKIE;
    enc28j60_write_register_internal(ENC28J60_EIE, eie);
    
    printf("ENC28J60: Interrupts enabled (EIE=0x%02X)\n", eie);
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

/**
 * @brief Send Ethernet packet (Arduino reference implementation)
 */
bool enc28j60_send_packet(const enc28j60_packet_t* packet) {
    if (!g_driver_initialized || !packet || !packet->data || 
        packet->length == 0 || !packet->valid) {
        return false;
    }
    
    if (packet->length > ENC28J60_MAX_FRAME_SIZE) {
        g_enc28j60_state.tx_errors++;
        return false;
    }
    
    enc28j60_set_register_bank_internal(ERXTX_BANK);
    
    // Set up the transmit buffer pointer (Arduino reference)
    enc28j60_write_register_internal(ENC28J60_ETXSTL, TX_BUF_START & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ETXSTH, (TX_BUF_START >> 8) & 0xFF);
    enc28j60_write_register_internal(ENC28J60_EWRPTL, TX_BUF_START & 0xFF);
    enc28j60_write_register_internal(ENC28J60_EWRPTH, (TX_BUF_START >> 8) & 0xFF);
    
    // Write per-packet control byte (Arduino reference)
    uint8_t control_byte = 0x00;
    enc28j60_write_buffer(&control_byte, 1);
    
    // Write packet data
    enc28j60_write_buffer(packet->data, packet->length);
    
    // Set TX end pointer
    uint16_t tx_end = TX_BUF_START + packet->length;
    enc28j60_write_register_internal(ENC28J60_ETXNDL, tx_end & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ETXNDH, (tx_end >> 8) & 0xFF);
    
    // Clear any previous TX interrupt
    enc28j60_clear_register_bits(ENC28J60_EIR, ENC28J60_EIR_TXIF);
    
    // Start transmission (Arduino reference)
    enc28j60_set_register_bits(ENC28J60_ECON1, ENC28J60_ECON1_TXRTS);
    
    g_enc28j60_state.packets_sent++;
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_TX, packet->length);
    
    return true;
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
bool enc28j60_has_rx_packet(void) {
    if (!g_driver_initialized) {
        return false;
    }
    
    enc28j60_set_register_bank_internal(EPKTCNT_BANK);
    uint8_t pktcnt = enc28j60_read_register_internal(ENC28J60_EPKTCNT);
    return pktcnt > 0;
}

/**
 * @brief Receive Ethernet packet (Arduino reference implementation)
 */
bool enc28j60_receive_packet(enc28j60_packet_t* packet, uint16_t max_length) {
    if (!g_driver_initialized || !packet || !packet->data || max_length == 0) {
        return false;
    }
    
    if (!enc28j60_has_rx_packet()) {
        return false;
    }
    
    enc28j60_set_register_bank_internal(ERXTX_BANK);
    
    // Set read pointer to next packet
    enc28j60_write_register_internal(ENC28J60_ERDPTL, g_enc28j60_state.next_packet_ptr & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ERDPTH, (g_enc28j60_state.next_packet_ptr >> 8) & 0xFF);
    
    // Read packet header (6 bytes) - Arduino reference
    uint8_t packet_header[6];
    enc28j60_read_buffer(packet_header, 6);
    
    // Extract next packet pointer and packet length from header
    uint16_t next_packet = packet_header[0] | (packet_header[1] << 8);
    uint16_t packet_length = packet_header[2] | (packet_header[3] << 8);
    
    // Validate packet length
    if (packet_length > max_length || packet_length > ENC28J60_MAX_FRAME_SIZE) {
        g_enc28j60_state.rx_errors++;
        return false;
    }
    
    // Read packet data
    enc28j60_read_buffer(packet->data, packet_length);
    
    packet->length = packet_length;
    packet->valid = true;
    
    // Update next packet pointer for next receive
    g_enc28j60_state.next_packet_ptr = next_packet;
    
    // Free up the receive buffer space (Errata #14 workaround) - Arduino reference
    uint16_t rx_read_ptr;
    if (next_packet == RX_BUF_START) {
        rx_read_ptr = RX_BUF_END;
    } else {
        rx_read_ptr = next_packet - 1;
    }
    
    enc28j60_write_register_internal(ENC28J60_ERXRDPTL, rx_read_ptr & 0xFF);
    enc28j60_write_register_internal(ENC28J60_ERXRDPTH, (rx_read_ptr >> 8) & 0xFF);
    
    // Decrement packet count
    enc28j60_set_register_bits(ENC28J60_ECON2, ENC28J60_ECON2_PKTDEC);
    
    g_enc28j60_state.packets_received++;
    
    log_event(EVENT_SOURCE_NETWORK, LOG_LEVEL_DEBUG, LOG_EVENT_NETWORK_RX, packet_length);
    
    return true;
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
bool enc28j60_process_interrupts(void) {
    if (!g_interrupt_pending || !g_driver_initialized) {
        return false;
    }
    
    printf("INT: Processing interrupt, status=0x%02X\n", g_last_interrupt_status);
    
    // Process packet receive interrupt
    if (g_last_interrupt_status & ENC28J60_EIR_PKTIF) {
        printf("INT: Packet received interrupt\n");
    }
    
    // Process link status change interrupt
    if (g_last_interrupt_status & ENC28J60_EIR_LINKIF) {
        printf("INT: Link status change interrupt\n");
        bool link_up = enc28j60_get_link_status();
        printf("INT: New link status = %s\n", link_up ? "UP" : "DOWN");
    }
    
    // Clear the interrupt pending flag
    g_interrupt_pending = false;
    
    return true;
}

/**
 * @brief Read PHY register via MII interface (Arduino reference implementation)
 */
static uint16_t enc28j60_read_phy_register(uint8_t phy_reg) {
    if (!g_driver_initialized) {
        return 0xFFFF;
    }
    
    // Set bank 2 for MII access (Arduino reference)
    enc28j60_set_register_bank_internal(MACONX_BANK);
    
    // Set the PHY register address to read
    enc28j60_write_register_internal(ENC28J60_MIREGADR, phy_reg);
    
    // Start the PHY read operation
    enc28j60_write_register_internal(ENC28J60_MICMD, ENC28J60_MICMD_MIIRD);
    
    // Wait for the PHY read to complete (Arduino reference)
    // MISTAT is in Bank 3
    uint32_t timeout = 1000;  // 1ms timeout
    while (timeout > 0) {
        enc28j60_set_register_bank_internal(MAADRX_BANK);  // Bank 3 for MISTAT
        uint8_t mistat = enc28j60_read_register_internal(ENC28J60_MISTAT);
        if ((mistat & ENC28J60_MISTAT_BUSY) == 0) {
            break;  // Read complete
        }
        sleep_us(1);
        timeout--;
    }
    
    // Return to Bank 2 for reading result
    enc28j60_set_register_bank_internal(MACONX_BANK);
    
    // Clear the read command
    enc28j60_write_register_internal(ENC28J60_MICMD, 0x00);
    
    // Read the result from MIRDL and MIRDH registers
    uint8_t low_byte = enc28j60_read_register_internal(ENC28J60_MIRDL);
    uint8_t high_byte = enc28j60_read_register_internal(ENC28J60_MIRDH);
    
    return (high_byte << 8) | low_byte;
}

/**
 * @brief Get link status by reading PHY register (Arduino reference implementation)
 */
bool enc28j60_get_link_status(void) {
    if (!g_driver_initialized) {
        return false;
    }
    
    // Read PHY Status Register 2 (PHSTAT2) and check link status bit
    // Arduino reference: phyread(MACSTAT2) & 0x400
    uint16_t phstat2 = enc28j60_read_phy_register(ENC28J60_PHSTAT2);
    
    // Check bit 10 (0x0400) - Link Status bit
    bool link_up = (phstat2 & ENC28J60_PHSTAT2_LSTAT) != 0;
    
    return link_up;
}
