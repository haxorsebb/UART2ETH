/**
 * @file device_mode.h
 * @brief Device mode configuration - selects between SHARK, PRIMARY, and SECONDARY modes
 * 
 * Device Modes:
 * 
 * SHARK:
 *   - UART Channel 0 (debug) + Channel 1 only
 *   - Channels 2 and 3 disabled and hidden from web interface
 *   - ENC28J60 Ethernet on SPI1
 *   - Total: 2 UARTs (1 data channel)
 * 
 * PRIMARY:
 *   - UART Channel 0 (debug) + Channels 1-4
 *   - ENC28J60 Ethernet on SPI1
 *   - SPI0 connection to SECONDARY device
 *   - Total: 5 UARTs (4 data channels)
 * 
 * SECONDARY:
 *   - UART Channel 0 (debug) + Channels 1-4
 *   - No ENC28J60 (no direct Ethernet)
 *   - SPI1 connection to PRIMARY device
 *   - Total: 5 UARTs (4 data channels)
 * 
 * To select a mode, define one of:
 *   -DDEVICE_MODE_SHARK
 *   -DDEVICE_MODE_PRIMARY
 *   -DDEVICE_MODE_SECONDARY
 * 
 * Or set DEVICE_MODE in CMake.
 */

#ifndef DEVICE_MODE_H
#define DEVICE_MODE_H

// ============================================================================
// Device Mode Selection
// ============================================================================

// Default to SHARK mode if nothing specified
#if !defined(DEVICE_MODE_SHARK) && !defined(DEVICE_MODE_PRIMARY) && !defined(DEVICE_MODE_SECONDARY)
#define DEVICE_MODE_SHARK
#endif

// Validate only one mode is selected
#if (defined(DEVICE_MODE_SHARK) + defined(DEVICE_MODE_PRIMARY) + defined(DEVICE_MODE_SECONDARY)) > 1
#error "Only one device mode can be selected: DEVICE_MODE_SHARK, DEVICE_MODE_PRIMARY, or DEVICE_MODE_SECONDARY"
#endif

// ============================================================================
// Mode-specific Configuration
// ============================================================================

#ifdef DEVICE_MODE_SHARK
    #define DEVICE_MODE_NAME            "SHARK"
    #define DEVICE_NUM_UART_CHANNELS    2       // Channel 0 (debug) + Channel 4
    #define DEVICE_NUM_DATA_CHANNELS    1       // Only Channel 4 for data
    #define DEVICE_HAS_ETHERNET         1       // Has ENC28J60
    #define DEVICE_HAS_SPI_LINK         0       // No SPI link to other device
    #define DEVICE_CHANNEL_1_ENABLED    0       // HW UART1 disabled (GPIO24/25 not used)
    #define DEVICE_CHANNEL_2_ENABLED    0
    #define DEVICE_CHANNEL_3_ENABLED    0
    #define DEVICE_CHANNEL_4_ENABLED    1       // PIO UART on GPIO5(TX)/GPIO4(RX)
#endif

#ifdef DEVICE_MODE_PRIMARY
    #define DEVICE_MODE_NAME            "PRIMARY"
    #define DEVICE_NUM_UART_CHANNELS    5       // Channel 0 (debug) + Channels 1-4
    #define DEVICE_NUM_DATA_CHANNELS    4       // Channels 1-4 for data
    #define DEVICE_HAS_ETHERNET         1       // Has ENC28J60 on SPI1
    #define DEVICE_HAS_SPI_LINK         1       // SPI0 link to SECONDARY
    #define DEVICE_CHANNEL_1_ENABLED    1
    #define DEVICE_CHANNEL_2_ENABLED    1
    #define DEVICE_CHANNEL_3_ENABLED    1
    #define DEVICE_CHANNEL_4_ENABLED    1
#endif

#ifdef DEVICE_MODE_SECONDARY
    #define DEVICE_MODE_NAME            "SECONDARY"
    #define DEVICE_NUM_UART_CHANNELS    5       // Channel 0 (debug) + Channels 1-4
    #define DEVICE_NUM_DATA_CHANNELS    4       // Channels 1-4 for data
    #define DEVICE_HAS_ETHERNET         0       // No ENC28J60
    #define DEVICE_HAS_SPI_LINK         1       // SPI1 link to PRIMARY
    #define DEVICE_CHANNEL_1_ENABLED    1
    #define DEVICE_CHANNEL_2_ENABLED    1
    #define DEVICE_CHANNEL_3_ENABLED    1
    #define DEVICE_CHANNEL_4_ENABLED    1
#endif

// ============================================================================
// Derived Configuration
// ============================================================================

// Maximum channel index (0-based)
#define DEVICE_MAX_CHANNEL_INDEX    (DEVICE_NUM_UART_CHANNELS - 1)

// Helper macro to check if a channel is available in current mode
#define DEVICE_CHANNEL_AVAILABLE(ch) ( \
    ((ch) == 0) || \
    ((ch) == 1 && DEVICE_CHANNEL_1_ENABLED) || \
    ((ch) == 2 && DEVICE_CHANNEL_2_ENABLED) || \
    ((ch) == 3 && DEVICE_CHANNEL_3_ENABLED) || \
    ((ch) == 4 && DEVICE_CHANNEL_4_ENABLED) \
)

// ============================================================================
// GPIO Pin Assignments (mode-independent for now, can be customized per mode)
// ============================================================================

// UART Channel 0 (Debug) - PL011 UART0
#define DEVICE_UART0_TX_GPIO        0
#define DEVICE_UART0_RX_GPIO        1

// UART Channel 1 - PL011 UART1 (moved to GPIO24/25 due to board wiring)
#define DEVICE_UART1_TX_GPIO        24
#define DEVICE_UART1_RX_GPIO        25

// UART Channel 2 - PIO UART (SHARK: not used)
#define DEVICE_UART2_TX_GPIO        14
#define DEVICE_UART2_RX_GPIO        15

// UART Channel 3 - PIO UART (SHARK: not used)
#define DEVICE_UART3_TX_GPIO        22
#define DEVICE_UART3_RX_GPIO        23

// UART Channel 4 - PIO UART (used for selftest output, GPIO4/5 swapped due to board wiring)
#define DEVICE_UART4_TX_GPIO        5
#define DEVICE_UART4_RX_GPIO        4

// ============================================================================
// SPI Configuration
// ============================================================================

// SPI1 - ENC28J60 Ethernet (SHARK, PRIMARY) or link to PRIMARY (SECONDARY)
#define DEVICE_SPI1_SCK_GPIO        10
#define DEVICE_SPI1_MOSI_GPIO       11
#define DEVICE_SPI1_MISO_GPIO       12
#define DEVICE_SPI1_CS_GPIO         13

// SPI0 - Link to SECONDARY (PRIMARY only)
#define DEVICE_SPI0_SCK_GPIO        2
#define DEVICE_SPI0_MOSI_GPIO       3
#define DEVICE_SPI0_MISO_GPIO       4   // Note: conflicts with UART4 RX - needs review
#define DEVICE_SPI0_CS_GPIO         5   // Note: conflicts with UART4 TX - needs review

#endif // DEVICE_MODE_H
