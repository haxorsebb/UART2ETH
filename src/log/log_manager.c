/**
 * @file log_manager.c
 * @brief Implementation of fixed-size entry log manager system
 * 
 * Implements dual-core event logging with fixed-size entries as documented
 * in arc42 architecture.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Log Manager Implementation (Fixed-Size Entry System)
 * - arc42 Chapter 6 - Runtime View - Fixed-Size Entry Pattern
 */

#include "log_manager.h"
#include "shared_memory.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include <stdio.h>
#include <string.h>




// Global error state
static log_error_t g_last_error = LOG_ERROR_NONE;

// Static variables for log manager
static bool g_log_initialized = false;
static shared_memory_layout_t* g_shared_layout = NULL;

// String lookup table for event formatting
static const char* const event_format_strings[] = {
    // System events (0-99)
    [LOG_EVENT_SYSTEM_BOOT] = "System boot sequence initiated",
    [LOG_EVENT_SYSTEM_CLOCK] = "System clock: %u Hz",
    [LOG_EVENT_SYSTEM_READY] = "System ready for operation",
    [LOG_EVENT_WATCHDOG_RESET] = "Watchdog reset occurred - timeout %u ms",
    [LOG_EVENT_MEMORY_INIT] = "Memory subsystem initialized - %u KB available",
    [LOG_EVENT_CORE1_STARTING] = "UART2ETH Core1 starting",
    [LOG_EVENT_CORE1_LAUNCHED] = "Core1 launched successfully", 
    [LOG_EVENT_CORE0_STARTING] = "Starting Core0 UART processing",
    [LOG_EVENT_SHARED_MEMORY_INIT] = "Shared memory initialized successfully",
    [LOG_EVENT_STATE_MACHINE_INIT] = "Event-driven state machine initialized successfully",
    [LOG_EVENT_LOG_MANAGER_INIT] = "Log manager initialized successfully",
    [LOG_EVENT_SYSTEM_ERROR] = "FATAL ERROR: System initialization failed - code %u",
    [LOG_EVENT_CORE_EXIT_ERROR] = "ERROR: Core%u main loop exited unexpectedly",
    [LOG_EVENT_INIT_PHASE] = "MAIN_STATE_INIT - Initializing system phase %u",
    [LOG_EVENT_CONFIG_PHASE] = "MAIN_STATE_CONFIGURATION - Loading configuration",
    [LOG_EVENT_OPERATIONAL_PHASE] = "MAIN_STATE_OPERATIONAL - Normal operation mode",
    [LOG_EVENT_ERROR_RECOVERY] = "System recovery attempt %u",
    
    // UART events (100-199)
    [LOG_EVENT_UART_INIT] = "UART initialized",
    [LOG_EVENT_UART_HW_INIT] = "UART hardware initialized successfully",
    [LOG_EVENT_UART_DATA_AVAIL] = "UART data available - starting processing",
    [LOG_EVENT_UART_PROCESSING] = "Processing UART data - cycles remaining: %u",
    [LOG_EVENT_UART_COMPLETE] = "UART data processing complete",
    [LOG_EVENT_UART_RECOVERY] = "UART recovery attempt %u",
    [LOG_EVENT_UART_CHANNELS] = "Initializing %u UART channels",
    
    // UART Channel-specific events (single parameter - byte count/error code)
    [LOG_EVENT_UART0_DATA_RX] = "UART channel 0 received %u bytes",
    [LOG_EVENT_UART0_DATA_TX] = "UART channel 0 transmitted %u bytes", 
    [LOG_EVENT_UART0_ERROR] = "UART channel 0 error code %u",
    [LOG_EVENT_UART0_OVERFLOW] = "UART channel 0 buffer overflow - %u bytes lost",
    [LOG_EVENT_UART1_DATA_RX] = "UART channel 1 received %u bytes",
    [LOG_EVENT_UART1_DATA_TX] = "UART channel 1 transmitted %u bytes",
    [LOG_EVENT_UART1_ERROR] = "UART channel 1 error code %u", 
    [LOG_EVENT_UART1_OVERFLOW] = "UART channel 1 buffer overflow - %u bytes lost",
    [LOG_EVENT_UART2_DATA_RX] = "UART channel 2 received %u bytes",
    [LOG_EVENT_UART2_DATA_TX] = "UART channel 2 transmitted %u bytes",
    [LOG_EVENT_UART2_ERROR] = "UART channel 2 error code %u",
    [LOG_EVENT_UART2_OVERFLOW] = "UART channel 2 buffer overflow - %u bytes lost", 
    [LOG_EVENT_UART3_DATA_RX] = "UART channel 3 received %u bytes",
    [LOG_EVENT_UART3_DATA_TX] = "UART channel 3 transmitted %u bytes",
    [LOG_EVENT_UART3_ERROR] = "UART channel 3 error code %u",
    [LOG_EVENT_UART3_OVERFLOW] = "UART channel 3 buffer overflow - %u bytes lost",
    
    // Generic UART events
    [LOG_EVENT_UART_INIT_GENERIC] = "UART channel %u initialized",
    
    // Network events (200-299)
    [LOG_EVENT_TCP_CONNECT] = "TCP connection established on port %u",
    [LOG_EVENT_TCP_DISCONNECT] = "TCP connection closed on port %u",
    [LOG_EVENT_NETWORK_ERROR] = "Network error code %u",
    [LOG_EVENT_TCP_DATA_RX_BYTES] = "TCP received %u bytes",
    [LOG_EVENT_TCP_DATA_RX_PORT] = "TCP data received on port %u",
    [LOG_EVENT_TCP_DATA_TX_BYTES] = "TCP transmitted %u bytes", 
    [LOG_EVENT_TCP_DATA_TX_PORT] = "TCP data transmitted on port %u",
    [LOG_EVENT_NETWORK_INIT] = "Network interface initialized successfully",
    [LOG_EVENT_NETWORK_UP] = "Network interface is now available",
    [LOG_EVENT_NETWORK_DOWN] = "Network interface went down",
    [LOG_EVENT_NETWORK_AVAILABLE] = "Network interface available",
    [LOG_EVENT_NETWORK_STATUS] = "Network status check - available: %u",
    [LOG_EVENT_NETWORK_IP_CONFIGURED_1] = "Network IP configured - 1. Octet: %u",
    [LOG_EVENT_NETWORK_IP_CONFIGURED_2] = "Network IP configured - 2. Octet: %u",
    [LOG_EVENT_NETWORK_IP_CONFIGURED_3] = "Network IP configured - 3. Octet: %u",
    [LOG_EVENT_NETWORK_IP_CONFIGURED_4] = "Network IP configured - 4. Octet: %u",
    [LOG_EVENT_NETWORK_OPERATIONS] = "Processing network operations (TCP/IP, sockets)",
    [LOG_EVENT_CONNECTION_CHECK] = "Connection check - active connections: %u",
    [LOG_EVENT_NETWORK_DEINIT] = "Network interface deinitialized",
    [LOG_EVENT_NETWORK_TX] = "Network transmit operation",
    [LOG_EVENT_NETWORK_RX] = "Network receive operation", 
    [LOG_EVENT_NETWORK_RESET] = "Network interface reset - reason code %u",
    [LOG_EVENT_NETWORK_CONFIG] = "Network configuration updated - parameter %u",
    
    // Configuration events (300-399)
    [LOG_EVENT_CONFIG_CHANGED] = "Configuration parameter %u changed",
    [LOG_EVENT_CONFIG_SAVED] = "Configuration saved to flash - revision %u",
    [LOG_EVENT_CONFIG_LOADED] = "Configuration loaded from flash - revision %u",
    [LOG_EVENT_FACTORY_RESET] = "Factory reset completed - all flash blocks invalidated",
    [LOG_EVENT_FLASH_INIT] = "Flash persistence initialized: partition ID=%u",
    [LOG_EVENT_FLASH_PARTITION_ID] = "Found partition ID %u",
    [LOG_EVENT_FLASH_PARTITION_OFFSET] = "Partition offset 0x%x",
    [LOG_EVENT_FLASH_READ] = "Reading block %u from flash",
    [LOG_EVENT_FLASH_WRITE] = "Writing flash block %u",
    [LOG_EVENT_FLASH_WRITE_SUCCESS] = "Writing flash block %u SUCCESSFULL",
    [LOG_EVENT_FLASH_WRITE_FAILED] = "Writing flash block %u FAILED!",
    [LOG_EVENT_FLASH_WRITE_SECTOR_UPDATE] = "sector %u needs an update",
    [LOG_EVENT_FLASH_CHKSUM] = "Checksumming flash block %u",
    [LOG_EVENT_FLASH_BLOCK_SCAN] = "Scanning ring buffer for valid config blocks",
    [LOG_EVENT_FLASH_BLOCK_NUMBER] = "Block %u is valid",
    [LOG_EVENT_FLASH_BLOCK_REVISION] = "Block revision %u",
    [LOG_EVENT_FLASH_BLOCK_INVALID] = "Block invalid - reason code %u", 
    [LOG_EVENT_FLASH_BEST_BLOCK_NUMBER] = "Best block is %u",
    [LOG_EVENT_FLASH_BEST_BLOCK_REV] = "Best block revision %u",
    [LOG_EVENT_FLASH_ERASE] = "Invalidating flash sector %u",
    [LOG_EVENT_FLASH_ERASE_FAILED] = "Invalidating flash sector failed, ret: %d",
    [LOG_EVENT_FLASH_PROGRAM] = "Programming flash sector %u",
    [LOG_EVENT_FLASH_PROGRAM_FAILED] = "Programming flash sector failed, ret: %d",
    [LOG_EVENT_FLASH_READ_FAILED] = "Reading block %u from flash failed",    
    [LOG_EVENT_PERSISTENCE_START] = "Starting persistence operation",
    [LOG_EVENT_PERSISTENCE_END] = "Persistence operation completed",
    [LOG_EVENT_PERSISTENCE_NEEDED] = "Persistence needed - configuration changed",
    [LOG_EVENT_SHARED_MEMORY_REINIT] = "Shared memory re-initialized with factory defaults",
    [LOG_EVENT_FACTORY_PARTITION_NOT_FOUND] = "Factory defaults partition not found",
    [LOG_EVENT_FACTORY_PARTITION_TOO_SMALL] = "Factory defaults partition too small - size %u bytes",
    [LOG_EVENT_FACTORY_DEFAULTS_CHECKSUM_FAILED] = "Factory defaults checksum validation failed",
    [LOG_EVENT_FACTORY_DEFAULTS_LOADED] = "Factory defaults loaded - board type %u",
    [LOG_EVENT_FACTORY_DEFAULTS_INVALID] = "Factory defaults invalid - using built-in defaults",
    [LOG_EVENT_FACTORY_DEFAULTS_APPLIED] = "Factory defaults applied to configuration - board type %u",
    [LOG_EVENT_FACTORY_DEFAULTS_APPLY_FAILED] = "Failed to apply factory defaults - shared memory unavailable",
    
    // OTA events (400-499)
    [LOG_EVENT_OTA_START] = "OTA update started - size %u bytes",
    [LOG_EVENT_OTA_COMPLETE] = "OTA update completed - version %u",
    [LOG_EVENT_OTA_ERROR] = "OTA update failed - error code %u",

    // PERSISTENCE events (500-599)
    [LOG_EVENT_PERSISTENCE_INIT_SUCCESS] = "Persistence management system initialized.",
    [LOG_EVENT_PERSISTENCE_INIT_FAIL] = "Persistence management system init FAILED!",

    // LOGGING events (600-699)
    [LOG_EVENT_LOGGING_INIT_SUCCESS] = "Log management system initialized.",
    [LOG_EVENT_LOGGING_INIT_FAIL] = "Log management system init FAILED!",

    // Main state change events (700-703)
    // IMPORTANT: These MUST match main_state_t enum order exactly and be complete
    [LOG_MAIN_STATE_INIT] = "Main state changed to MAIN_STATE_INIT",
    [LOG_MAIN_STATE_CONFIGURATION] = "Main state changed to MAIN_STATE_CONFIGURATION",
    [LOG_MAIN_STATE_OPERATIONAL] = "Main state changed to MAIN_STATE_OPERATIONAL",
    [LOG_MAIN_STATE_ERROR] = "Main state changed to MAIN_STATE_ERROR",

    // Core0 substate change events (720-730)
    // IMPORTANT: These MUST match core0_substate_t enum order exactly and be complete
    [LOG_CORE0_INIT_UART] = "Core0 substate changed to CORE0_INIT_UART",
    [LOG_CORE0_INIT_COMPLETE] = "Core0 substate changed to CORE0_INIT_COMPLETE",
    [LOG_CORE0_INIT_IDLE] = "Core0 substate changed to CORE0_INIT_IDLE",
    [LOG_CORE0_INIT_ERROR] = "Core0 substate changed to CORE0_INIT_ERROR",
    [LOG_CORE0_CONFIG_UART] = "Core0 substate changed to CORE0_CONFIG_UART",
    [LOG_CORE0_CONFIG_COMPLETE] = "Core0 substate changed to CORE0_CONFIG_COMPLETE",
    [LOG_CORE0_CONFIG_IDLE] = "Core0 substate changed to CORE0_CONFIG_IDLE",
    [LOG_CORE0_CONFIG_ERROR] = "Core0 substate changed to CORE0_CONFIG_ERROR",
    [LOG_CORE0_IDLE] = "Core0 substate changed to CORE0_IDLE (old state %u)",
    [LOG_CORE0_UART_ACTIVE] = "Core0 substate changed to CORE0_UART_ACTIVE",
    [LOG_CORE0_RINGBUFFER_ACTIVE] = "Core0 substate changed to CORE0_RINGBUFFER_ACTIVE",
    [LOG_CORE0_UART_ERROR] = "Core0 substate changed to CORE0_UART_ERROR",
    [LOG_CORE0_IDLE_INVALID_EVENT] = "Core0 substate stayed in CORE0_IDLE (invalid event %u)",
    [LOG_CORE0_EVENT_WITHOUT_STATE_CHANGE] = "Core0 substate stayed in %u",
    
    // Core1 substate change events (750-773)
    // IMPORTANT: These MUST match core1_substate_t enum order exactly and be complete
    [LOG_CORE1_INIT_PERISTENCE] = "Core1 substate changed to CORE1_INIT_PERISTENCE",
    [LOG_CORE1_INIT_LOGGING] = "Core1 substate changed to CORE1_INIT_LOGGING",
    [LOG_CORE1_INIT_NET] = "Core1 substate changed to CORE1_INIT_NET",
    [LOG_CORE1_INIT_WAIT_FOR_LINK] = "Core1 substate changed to CORE1_INIT_WAIT_FOR_LINK",
    [LOG_CORE1_INIT_COMPLETE] = "Core1 substate changed to CORE1_INIT_COMPLETE",
    [LOG_CORE1_INIT_IDLE] = "Core1 substate changed to CORE1_INIT_IDLE",
    [LOG_CORE1_CONFIG_NET] = "Core1 substate changed to CORE1_CONFIG_NET",
    [LOG_CORE1_CONFIG_COMPLETE] = "Core1 substate changed to CORE1_CONFIG_COMPLETE",
    [LOG_CORE1_CONFIG_NET_WAIT_FOR_DHCP] = "Core1 substate changed to CORE1_CONFIG_NET_WAIT_FOR_DHCP",
    [LOG_CORE1_CONFIG_NET_CHECK_DHCP] = "Core1 substate changed to CORE1_CONFIG_NET_CHECK_DHCP",
    [LOG_CORE1_CONFIG_IDLE] = "Core1 substate changed to CORE1_CONFIG_IDLE",
    [LOG_CORE1_CONFIG_LOG_ACTIVE] = "Core1 substate changed to CORE1_CONFIG_LOG_ACTIVE",
    [LOG_CORE1_CONFIG_ERROR] = "Core1 substate changed to CORE1_CONFIG_ERROR",
    [LOG_CORE1_NET_LINK_CHANGE] = "Core1 substate changed to CORE1_NET_LINK_CHANGE",
    [LOG_CORE1_NET_CONNECTED] = "Core1 substate changed to CORE1_NET_CONNECTED",
    [LOG_CORE1_NET_DISCONNECTED] = "Core1 substate changed to CORE1_NET_DISCONNECTED",
    [LOG_CORE1_NET_IDLE] = "Core1 substate changed to CORE1_NET_IDLE",
    [LOG_CORE1_NET_ACTIVE_RECEIVE] = "Core1 substate changed to CORE1_NET_ACTIVE_RECEIVE",
    [LOG_CORE1_NET_ACTIVE_SEND] = "Core1 substate changed to CORE1_NET_ACTIVE_SEND",
    [LOG_CORE1_PERSISTENCE_ACTIVE] = "Core1 substate changed to CORE1_PERSISTENCE_ACTIVE",
    [LOG_CORE1_LOG_ACTIVE] = "Core1 substate changed to CORE1_LOG_ACTIVE",
    [LOG_CORE1_RINGBUFFER_ACTIVE] = "Core1 substate changed to CORE1_RINGBUFFER_ACTIVE",
    [LOG_CORE1_IDLE] = "Core1 substate changed to CORE1_IDLE",
    [LOG_CORE1_INIT_ERROR] = "Core1 substate changed to CORE1_INIT_ERROR",
    [LOG_CORE1_SHUTDOWN] = "Core1 substate changed to CORE1_SHUTDOWN",
};

// Static lookup table for event source names - fixed width, pre-centered
// 14 characters wide to accommodate longest source name "CORE0 SUBSTATE"
static const char* const event_source_strings[] = {
    "    SYSTEM    ",  // 0 - EVENT_SOURCE_SYSTEM 
    "    UART0     ",  // 1 - EVENT_SOURCE_UART0
    "    UART1     ",  // 2 - EVENT_SOURCE_UART1  
    "    UART2     ",  // 3 - EVENT_SOURCE_UART2
    "    UART3     ",  // 4 - EVENT_SOURCE_UART3
    "   NETWORK    ",  // 5 - EVENT_SOURCE_NETWORK
    "   CONFIG     ",  // 6 - EVENT_SOURCE_CONFIG
    "     OTA      ",  // 7 - EVENT_SOURCE_OTA
    "   WATCHDOG   ",  // 8 - EVENT_SOURCE_WATCHDOG
    " PERSISTENCE  ",  // 9 - EVENT_SOURCE_PERSISTENCE
    "   LOGGING    ",  // 10 - EVENT_SOURCE_LOGGING
    "     UI       ",  // 11 - EVENT_SOURCE_UI
    "     ANE      ",  // 12 - EVENT_SOURCE_ANE
    "    IMOUTO    ",  // 13 - EVENT_SOURCE_IMOUTO
    "  MAIN STATE  ",  // 14 - EVENT_SOURCE_MAIN_STATE
    "CORE0 SUBSTATE",  // 15 - EVENT_SOURCE_CORE0_SUBSTATE
    "CORE1 SUBSTATE"   // 16 - EVENT_SOURCE_CORE1_SUBSTATE
};

// Human-readable log level strings - fixed width
static const char* const log_level_strings[] = {
    "TRACE",  // 0 - LOG_LEVEL_TRACE
    "DEBUG",  // 1 - LOG_LEVEL_DEBUG
    "INFO ",  // 2 - LOG_LEVEL_INFO  
    "WARN ",  // 3 - LOG_LEVEL_WARN
    "ERROR"   // 4 - LOG_LEVEL_ERROR
};

// Array bounds checking constants
#define EVENT_SOURCE_STRINGS_COUNT (sizeof(event_source_strings) / sizeof(event_source_strings[0]))
#define LOG_LEVEL_STRINGS_COUNT (sizeof(log_level_strings) / sizeof(log_level_strings[0]))

/**
 * Validate that shared memory structure is properly initialized
 */
static bool validate_shared_memory(void) {
    if (!g_shared_layout) {
        g_last_error = LOG_ERROR_NOT_INITIALIZED;
        return false;
    }
    
    // Basic sanity checks on shared memory structure
    if (g_shared_layout->log_mgmt.max_entries == 0 || 
        g_shared_layout->log_mgmt.max_entries > 10000) {  // Reasonable upper bound
        g_last_error = LOG_ERROR_SHARED_MEMORY_CORRUPT;
        return false;
    }
    
    if (g_shared_layout->log_mgmt.write_index >= g_shared_layout->log_mgmt.max_entries ||
        g_shared_layout->log_mgmt.read_index >= g_shared_layout->log_mgmt.max_entries) {
        g_last_error = LOG_ERROR_SHARED_MEMORY_CORRUPT;
        return false;
    }
    
    return true;
}

/**
 * Validate event type is within allowed ranges
 */
static bool validate_event_type(event_type_t event_type) {
    // Check if event type is within any valid range
    if ((event_type >= SYSTEM_EVENT_BASE && event_type <= SYSTEM_EVENT_MAX) ||
        (event_type >= UART_EVENT_BASE && event_type <= UART_EVENT_MAX) ||
        (event_type >= NETWORK_EVENT_BASE && event_type <= NETWORK_EVENT_MAX) ||
        (event_type >= CONFIG_EVENT_BASE && event_type <= CONFIG_EVENT_MAX) ||
        (event_type >= OTA_EVENT_BASE && event_type <= OTA_EVENT_MAX)||
        (event_type >= PERSISTENCE_EVENT_BASE && event_type <= PERSISTENCE_EVENT_MAX)||
        (event_type >= LOGGING_EVENT_BASE && event_type <= LOGGING_EVENT_MAX)||
        (event_type >= STATE_CHANGE_EVENT_BASE && event_type <= STATE_CHANGE_EVENT_MAX)) {
        
        // Additional check: ensure we have a format string for this type
        if (event_type < EVENT_FORMAT_ARRAY_SIZE && event_format_strings[event_type] != NULL) {
            return true;
        } else {
            // Safety debugging output: in-range but missing format string
            printf("[DEBUG] Event type %u in valid range but format string missing or null\n", event_type);
            fflush(stdout);
        }
    } else {
        // Safety debugging output: completely out of range
        printf("[DEBUG] Event type %u out of all valid ranges (SYSTEM:0-99, UART:100-199, NET:200-299, CONFIG:300-399, OTA:400-499, PERSIST:500-599, LOG:600-699, STATE:700-799)\n", event_type);
        fflush(stdout);
    }
    
    g_last_error = LOG_ERROR_INVALID_EVENT_TYPE;
    return false;
}

/**
 * Validate event source is within allowed ranges
 */
static bool validate_event_source(event_source_t event_source) {
    if (event_source >= EVENT_SOURCE_MIN && event_source <= EVENT_SOURCE_MAX) {
        return true;
    }
    
    // Safety debugging output: show what source was invalid
    printf("[DEBUG] Event source %u out of valid range (min:%u, max:%u)\n", 
           event_source, EVENT_SOURCE_MIN, EVENT_SOURCE_MAX);
    fflush(stdout);
    
    g_last_error = LOG_ERROR_INVALID_EVENT_SOURCE;
    return false;
}

/**
 * Get system timestamp (simplified for testing)
 */
static uint32_t get_system_timestamp(void) {
    return (uint32_t)(get_absolute_time() / 1000);  // Convert to milliseconds
}

/**
 * Get and increment the next event sequence number for current core
 * Uses actual core ID in production, simulates based on event source in test environment
 */
static uint32_t get_next_event_sequence(event_source_t event_source) {
    if (!g_shared_layout) return 0;
    
    // Thread-safe access to sequence counters
    uint32_t sequence;
    
    // For testing compatibility: Simulate core assignment based on event source
    // In production, this logic would use get_core_num() or actual hardware assignment
    // Network events = Core 1, all others = Core 0 (maintains test compatibility)
    if (get_core_num()==1) {
        sequence = ++g_shared_layout->log_mgmt.core1_sequence;
    } else {
        sequence = ++g_shared_layout->log_mgmt.core0_sequence;
    }
    
    return sequence;
}

/**
 * Write log entry to circular buffer (atomic operation with spinlock)
 * Uses fast conditional check instead of expensive modulo operation
 */
static bool write_log_entry(const log_entry_t* entry) {
    if (!g_shared_layout || !entry) {
        return false;
    }

    // Critical section: protect shared buffer state
    spin_lock_unsafe_blocking(g_shared_layout->log_mgmt.entry_lock);
    
    // Calculate next write index without expensive modulo
    uint32_t next_write_index = g_shared_layout->log_mgmt.write_index + 1;
    if (next_write_index >= g_shared_layout->log_mgmt.max_entries) {
        next_write_index = 0;
    }
    
    // Check if buffer is full
    if (next_write_index == g_shared_layout->log_mgmt.read_index) {
        // Buffer full - drop oldest entry (move read_index forward)
        g_shared_layout->log_mgmt.read_index += 1;
        if (g_shared_layout->log_mgmt.read_index >= g_shared_layout->log_mgmt.max_entries) {
            g_shared_layout->log_mgmt.read_index = 0;
        }
    }

    // Update write index with fast ring buffer advance
    g_shared_layout->log_mgmt.write_index = next_write_index;
    // Update total count
    g_shared_layout->log_mgmt.total_events_logged++;

    //update revision if needed
    update_shared_memory_revision();

    spin_unlock_unsafe(g_shared_layout->log_mgmt.entry_lock);
    
    /* this is a test to use atomic instead of spinlocks TBD: use or remove

    uint32_t current_write, next_write;
    do {
        current_write = atomic_load(&g_shared_layout->log_mgmt.write_index);
        next_write = (current_write + 1) % g_shared_layout->log_mgmt.max_entries;
        
        // Check if buffer would be full
        if (next_write == atomic_load(&g_shared_layout->log_mgmt.read_index)) {
            return false;  // Buffer full
        }
    } while (!atomic_compare_exchange_weak(&g_shared_layout->log_mgmt.write_index, 
                                          &current_write, next_write));

    */

    // Write entry to buffer
    g_shared_layout->log_entries[next_write_index] = *entry;
    
    
    
    return true;
}

/**
 * Read next log entry from circular buffer (eventually consistent)
 * Uses fast conditional check instead of expensive modulo operation
 */
static bool read_log_entry(log_entry_t* entry) {
    if (!g_shared_layout || !entry) {
        return false;
    }
    
    
    // Check if buffer is empty
    if (g_shared_layout->log_mgmt.read_index == g_shared_layout->log_mgmt.write_index) {
        return false;  // No entries to read
    }
    
    // Read entry from buffer
    *entry = g_shared_layout->log_entries[g_shared_layout->log_mgmt.read_index];
    
    // Update read index with fast ring buffer advance
    g_shared_layout->log_mgmt.read_index += 1;
    if (g_shared_layout->log_mgmt.read_index >= g_shared_layout->log_mgmt.max_entries) {
        g_shared_layout->log_mgmt.read_index = 0;
    }
    
    return true;
}

/**
 * Initialize log manager (must be called after shared_memory_init)
 * 
 * @return true if initialization successful, false otherwise
 */
bool log_manager_init(void) {
    if (g_log_initialized) {
        return true;  // Already initialized
    }
    
    // Get shared memory layout
    g_shared_layout = shared_memory_get_layout();
    if (!g_shared_layout) {
        return false;  // Shared memory not initialized
    }
    
    g_log_initialized = true;
    return true;
}

/**
 * Log an event using fixed-size entry system
 * Thread-safe for both cores with per-core sequence numbering
 * 
 * @param event_source Event source (UART0-3, NETWORK, SYSTEM, etc.)
 * @param log_level Log level (DEBUG, INFO, WARN, ERROR)
 * @param event_type Event type from log_event_type_t enum
 * @param extra_value Context-specific parameter (e.g., port number, baud rate)
 * @return true if event was logged, false if buffer full or error
 */
bool log_event(event_source_t event_source, log_level_t log_level, 
               event_type_t event_type, uint32_t extra_value) {
    // Reset error state
    g_last_error = LOG_ERROR_NONE;

    if (!g_log_initialized) {
        g_last_error = LOG_ERROR_NOT_INITIALIZED;
        return false;
    }
    else {
        //return true;
    }
    
    // Check if this log level meets the minimum threshold
    if (log_level < LOG_MINIMUM_LEVEL) {
        // Silently drop logs below minimum level (not an error)
        return true;
    }
    
    // Validate shared memory structure
    if (!validate_shared_memory()) {
        return false;  // Error already set by validate_shared_memory
    }
    
    // Validate input parameters
    if (!validate_event_source(event_source)) {
        return false;  // Error already set by validate_event_source
    }
    
    if (!validate_event_type(event_type)) {
        return false;  // Error already set by validate_event_type
    }
    
    // Validate log level (0-4) - enhanced safety debugging
    if (log_level > LOG_LEVEL_ERROR) {
        // Safety debugging output: show invalid log level
        /* printf("[DEBUG] Log level %u out of valid range (max:%u)\n", log_level, LOG_LEVEL_ERROR);
        fflush(stdout); */
        g_last_error = LOG_ERROR_INVALID_EVENT_TYPE;  // Reuse for simplicity
        return false;
    }
    
    // Create log entry
    log_entry_t entry = {
        .timestamp = get_system_timestamp(),
        .event_source = event_source,
        .event_number = get_next_event_sequence(event_source),
        .log_level = log_level,
        .event_type = event_type,
        .event_extra_value = extra_value
    };
    
    // Write entry to buffer
    bool ret = write_log_entry(&entry);

    return ret;
}

/**
 * Core1 background task: format and print all pending log entries
 * Should be called periodically (100ms) by Core1 only
 * 
 * @return Number of entries processed and formatted
 */
/**
 * Safely format a single log entry with proper validation and sanitization
 */
static bool format_single_log_entry(const log_entry_t* entry, char* output_buffer, size_t buffer_size) {
    if (!entry || !output_buffer || buffer_size == 0) {
        // Safety debugging output: invalid parameters
        if (output_buffer && buffer_size > 0) {
            snprintf(output_buffer, buffer_size, "[SAFETY] format_single_log_entry: invalid parameters (entry=%p, buffer=%p, size=%zu)", 
                     entry, output_buffer, buffer_size);
        }
        return false;
    }
    
    // Validate event type bounds - enhanced safety output
    if (entry->event_type >= EVENT_FORMAT_ARRAY_SIZE) {
        snprintf(output_buffer, buffer_size, "[%08u][%u][%u] EVENT_TYPE_OUT_OF_BOUNDS_%u (max_allowed:%u)",
                entry->timestamp, entry->event_source, entry->event_number, 
                entry->event_type, (unsigned)(EVENT_FORMAT_ARRAY_SIZE - 1));
        return true;
    }
    
    const char* format = event_format_strings[entry->event_type];
    if (!format) {
        snprintf(output_buffer, buffer_size, "[%08u][%u][%u] FORMAT_STRING_NULL_FOR_EVENT_TYPE_%u",
                entry->timestamp, entry->event_source, entry->event_number, entry->event_type);
        return true;
    }
    
    // Create the prefix first - enhanced safety checks
    char prefix[64];
    const char* source_name;
    const char* level_name;
    
    // Safety check for event source with detailed debugging
    if (entry->event_source < EVENT_SOURCE_STRINGS_COUNT) {
        source_name = event_source_strings[entry->event_source];
    } else {
        source_name = "   UNKNOWN   ";
        // Note: We can't printf here as we're in formatting, but the error will be visible in the log line
    }
    
    // Safety check for log level with detailed debugging
    if (entry->log_level < LOG_LEVEL_STRINGS_COUNT) {
        level_name = log_level_strings[entry->log_level];
    } else {
        level_name = "UNKNW";
        // Note: We can't printf here as we're in formatting, but the error will be visible in the log line
    }
    
    int prefix_len = snprintf(prefix, sizeof(prefix), "[%08u][%s][%s][%04u] ",
            entry->timestamp, source_name, level_name, entry->event_number);
    
    if (prefix_len < 0 || prefix_len >= sizeof(prefix)) {
        // Enhanced safety debugging: prefix creation failed
        snprintf(output_buffer, buffer_size, "[%08u][ERR][ERR][%04u] PREFIX_FORMAT_FAILED (src:%u,lvl:%u)",
                entry->timestamp, entry->event_number, entry->event_source, entry->log_level);
        return true;
    }
    
    // For security, we'll create a safe version of format strings that only allows %u
    // and validates the parameter count
    char safe_message[LOG_MAX_FORMAT_LENGTH];
    
    // Enhanced format string validation with detailed debugging
    const char* first_percent = strchr(format, '%');
    if (first_percent && (first_percent[1] == 'u' || first_percent[1] == 'x' )  ) {
        // Safe: exactly one %u parameter
        snprintf(safe_message, sizeof(safe_message), format, entry->event_extra_value);
    } else if (first_percent == NULL) {
        // Safe: no parameters needed
        strncpy(safe_message, format, sizeof(safe_message) - 1);
        safe_message[sizeof(safe_message) - 1] = '\0';
    } else {
        // Enhanced safety: Unsafe format specifiers detected
        strncpy(safe_message, format, sizeof(safe_message) - 1);
        safe_message[sizeof(safe_message) - 1] = '\0';
        
        // Count dangerous format specifiers for debugging
        int percent_count = 0;
        for (const char* p = format; *p; p++) {
            if (*p == '%') percent_count++;
        }
        
        // Replace any % characters with # to prevent format string attacks
        // and add debugging info about what was sanitized
        for (char* p = safe_message; *p; p++) {
            if (*p == '%') {
                *p = '#';
            }
        }
        
        // Append safety warning to the message
        char temp_buffer[sizeof(safe_message)];
        snprintf(temp_buffer, sizeof(temp_buffer), "%s [SANITIZED:%d_percent_chars]", safe_message, percent_count);
        strncpy(safe_message, temp_buffer, sizeof(safe_message) - 1);
        safe_message[sizeof(safe_message) - 1] = '\0';
    }
    
    // Combine prefix and safe message
    snprintf(output_buffer, buffer_size, "%s%s", prefix, safe_message);
    return true;
}

uint32_t log_manager_format_pending(void) {
    // Reset error state
    g_last_error = LOG_ERROR_NONE;
    
    if (!g_log_initialized) {
        g_last_error = LOG_ERROR_NOT_INITIALIZED;
        return 0;
    }
    
    if (!validate_shared_memory()) {
        return 0;  // Error already set by validate_shared_memory
    }
    
    uint32_t formatted_count = 0;
    log_entry_t entry;
    
    // Process up to 10 pending entries per call to prevent backlog
    const uint32_t max_entries_per_call = LOG_MAX_FORMATTED_ENTRIES_PER_CALL;
    
    while (formatted_count < max_entries_per_call && read_log_entry(&entry)) {
        char formatted_msg[LOG_MAX_MESSAGE_LENGTH];
        
        if (format_single_log_entry(&entry, formatted_msg, sizeof(formatted_msg))) {
            // Output to USB-serial stdout
            printf("%s\n", formatted_msg);
            fflush(stdout);
        } else {
            // Enhanced fallback output for formatting errors with more context
            printf("[%08u][%u][%u] FORMAT_ERROR (event_type:%u, log_level:%u, extra_value:%u)\n", 
                   entry.timestamp, entry.event_source, entry.event_number,
                   entry.event_type, entry.log_level, entry.event_extra_value);
            fflush(stdout);
        }
        
        formatted_count++;
    }
    
    return formatted_count;
}

/**
 * Get current log buffer utilization percentage
 * 
 * @return Percentage (0-100) of log entry buffer currently used
 */
uint32_t log_manager_get_utilization(void) {
    if (!g_log_initialized || !g_shared_layout) {
        return 0;
    }
    
    // Read buffer state eventually consistent
    uint32_t write_idx = g_shared_layout->log_mgmt.write_index;
    uint32_t read_idx = g_shared_layout->log_mgmt.read_index;
    uint32_t max_entries = g_shared_layout->log_mgmt.max_entries;
    
    if (max_entries == 0) return 0;
    
    // Fast ring buffer utilization calculation without modulo
    uint32_t used_entries;
    if (write_idx >= read_idx) {
        used_entries = write_idx - read_idx;
    } else {
        used_entries = (max_entries - read_idx) + write_idx;
    }
    
    return (used_entries * 100) / max_entries;
}

/**
 * Get number of log entries currently waiting to be processed
 * 
 * @return Number of pending log entries
 */
uint32_t log_manager_get_pending_count(void) {
    if (!g_log_initialized || !g_shared_layout) {
        return 0;
    }
    
    // Read buffer state atomically  
    uint32_t write_idx = g_shared_layout->log_mgmt.write_index;
    uint32_t read_idx = g_shared_layout->log_mgmt.read_index;
    uint32_t max_entries = g_shared_layout->log_mgmt.max_entries;
    
    // Fast ring buffer count calculation without modulo
    if (write_idx >= read_idx) {
        return write_idx - read_idx;
    } else {
        return (max_entries - read_idx) + write_idx;
    }
}

/**
 * Get total number of events logged since startup
 * 
 * @return Total event count across all cores
 */
uint32_t log_manager_get_total_count(void) {
    if (!g_log_initialized || !g_shared_layout) {
        return 0;
    }
    
    // Read total count without block, eventually consistent
    uint32_t total = g_shared_layout->log_mgmt.total_events_logged;
    
    return total;
}

/**
 * Get current event sequence number for specific core
 * Used for detecting lost events during debugging
 * 
 * @param core_id Core ID (0 or 1)
 * @return Current sequence number for the specified core
 */
uint32_t log_manager_get_core_sequence(uint8_t core_id) {
    if (!g_log_initialized || !g_shared_layout) {
        return 0;
    }
    
    uint32_t sequence = 0;
    
    if (core_id == 0) {
        sequence = g_shared_layout->log_mgmt.core0_sequence;
    } else if (core_id == 1) {
        sequence = g_shared_layout->log_mgmt.core1_sequence;
    }
    return sequence;
}

/**
 * Reset log manager state for testing
 * Clears all counters and reinitializes entry buffer
 * 
 * @return true if reset successful, false otherwise
 */
bool log_manager_reset_for_testing(void) {
    if (!g_log_initialized || !g_shared_layout) {
        return false;
    }
    
    // Reset all state atomically
    uint32_t save = spin_lock_blocking(g_shared_layout->log_mgmt.entry_lock);
    
    // Reset counters
    g_shared_layout->log_mgmt.total_events_logged = 0;
    g_shared_layout->log_mgmt.core0_sequence = 0;
    g_shared_layout->log_mgmt.core1_sequence = 0;
    
    // Reset buffer pointers
    g_shared_layout->log_mgmt.write_index = 0;
    g_shared_layout->log_mgmt.read_index = 0;
    
    // Clear buffer
    memset(g_shared_layout->log_entries, 0, g_shared_layout->log_mgmt.max_entries * sizeof(log_entry_t));
    
    spin_unlock(g_shared_layout->log_mgmt.entry_lock, save);
    return true;
}

/**
 * Get human-readable string for event type (for testing/debugging)
 * 
 * @param event_type Event type from log_event_type_t enum
 * @return Format string for the event type, or NULL if invalid
 */
const char* log_manager_get_event_format_string(event_type_t event_type) {
    // Reset error state
    g_last_error = LOG_ERROR_NONE;
    
    if (!validate_event_type(event_type)) {
        return NULL;  // Error already set by validate_event_type
    }
    
    return event_format_strings[event_type];
}

/**
 * Get the last error that occurred in the log manager
 * 
 * @return Last error code
 */
log_error_t log_manager_get_last_error(void) {
    return g_last_error;
}

/**
 * Get human-readable description of error code
 * 
 * @param error Error code
 * @return Error description string
 */
const char* log_manager_get_error_string(log_error_t error) {
    switch (error) {
        case LOG_ERROR_NONE:
            return "No error";
        case LOG_ERROR_NOT_INITIALIZED:
            return "Log manager not initialized";
        case LOG_ERROR_INVALID_EVENT_TYPE:
            return "Invalid event type";
        case LOG_ERROR_INVALID_EVENT_SOURCE:
            return "Invalid event source";
        case LOG_ERROR_BUFFER_FULL:
            return "Log buffer full";
        case LOG_ERROR_SHARED_MEMORY_CORRUPT:
            return "Shared memory corrupted";
        case LOG_ERROR_SPINLOCK_TIMEOUT:
            return "Spinlock timeout";
        default:
            return "Unknown error";
    }
}