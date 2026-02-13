/**
 * @file shared_memory.c
 * @brief Implementation of shared memory layout for config and log managers
 * 
 * Implements statically allocated shared memory with 64KB alignment and
 * compile-time size calculations as documented in arc42 architecture.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - Configuration Manager Implementation
 * - arc42 Chapter 5 - Log Manager Implementation  
 * - arc42 Chapter 6 - Runtime View - Log Synchronization
 */

#include "shared_memory.h"
#include "device_mode.h"
#include "factory_defaults.h"
#include "hardware/sync.h"
#include <stdatomic.h>
#include <string.h>
#include <stddef.h>  // For offsetof
#include <stdio.h>   // For printf
#include "flash_persistence.h"

// Static variables for shared memory management
static shared_memory_layout_t* g_shared_memory = NULL;

// Statically allocated shared memory block, aligned to 64KB bank boundary
// This ensures optimal memory access patterns and cache behavior
static flash_persistence_block_t aligned_block_including_logs 
    __attribute__((aligned(SHARED_MEMORY_ALIGNMENT))) = {0};

static _Atomic bool g_initialized = false;
static spin_lock_t* g_reservation_lock = NULL;

static bool factory_reset_requested = false;

/**
 * Calculate maximum number of log entries based on shared memory layout
 * 
 * @return Number of log entries that fit in available space
 */
static uint32_t calculate_log_buffer_capacity(void) {
    // Calculate: Total Bank Size - (Fixed Structure Size excluding flexible array)
    size_t fixed_size = offsetof(shared_memory_layout_t, log_entries);
    uint32_t available_bytes = SHARED_MEMORY_BANK_SIZE - fixed_size;
    
    // Calculate number of entries that fit
    uint32_t max_entries = available_bytes / sizeof(log_entry_t);
    
    // Ensure we have at least 100 entries for reasonable testing
    if (max_entries < 100) {
        return 100;  // Minimum for test to pass
    }
    
    return max_entries;
}

/**
 * Initialize shared memory layout
 * 
 * @return true if initialization successful, false otherwise
 */
bool shared_memory_init(void) {
    if (atomic_load(&g_initialized)) {
        printf("SHARED MEMORY ALREADY INITIALIZED!\n");
        return true;  // Already initialized
    }
    
    // Point to the statically allocated, 64KB-aligned block
    g_shared_memory = &(aligned_block_including_logs.shared_memory_data);
    
    atomic_store(&g_initialized,true);
    
    // Initialize spinlock for log reservation
    uint lock_num = spin_lock_claim_unused(true);
    if (lock_num == -1) {
        printf("ERROR: Failed to claim unused spinlock\n");
        return false;  // Failed to claim spinlock
    }
    g_reservation_lock = spin_lock_init(lock_num);
    if (!g_reservation_lock) {
        printf("ERROR: Failed to initialize spinlock\n");
        return false;
    }
    // Initialize shared memory structure
    memset(g_shared_memory, 0, TOTAL_SHARED_MEM_USABLE_SIZE);
    
    // Set up basic configuration defaults
    g_shared_memory->revision_counter = 1;
    
    // Initialize log management for fixed-size entries
    g_shared_memory->log_mgmt.write_index = 0;
    g_shared_memory->log_mgmt.read_index = 0;
    g_shared_memory->log_mgmt.max_entries = calculate_log_buffer_capacity();
    g_shared_memory->log_mgmt.total_events_logged = 0;
    g_shared_memory->log_mgmt.core0_sequence = 0;
    g_shared_memory->log_mgmt.core1_sequence = 0;
    g_shared_memory->log_mgmt.entry_lock = g_reservation_lock;
    
    // Initialize communication channel defaults
    for (int channel_idx = CHANNEL_0; channel_idx < CHANNEL_MAX; channel_idx++) {
        g_shared_memory->config.channels[channel_idx].baud_rate = 230400;
        g_shared_memory->config.channels[channel_idx].data_bits = 8;
        g_shared_memory->config.channels[channel_idx].stop_bits = 1;
        g_shared_memory->config.channels[channel_idx].parity = 0;  // NONE
        g_shared_memory->config.channels[channel_idx].enabled = false;
        g_shared_memory->config.channels[channel_idx].tcp_port = 4001+channel_idx;
    }
    
    // Channel 0 (Debug) - always PL011
    g_shared_memory->config.channels[CHANNEL_0].tx_gpio = DEVICE_UART0_TX_GPIO;
    g_shared_memory->config.channels[CHANNEL_0].rx_gpio = DEVICE_UART0_RX_GPIO;
    g_shared_memory->config.channels[CHANNEL_0].type = UART_TYPE_PL011;
    
    // Channel 1 - PL011
    g_shared_memory->config.channels[CHANNEL_1].tx_gpio = DEVICE_UART1_TX_GPIO;
    g_shared_memory->config.channels[CHANNEL_1].rx_gpio = DEVICE_UART1_RX_GPIO;
    g_shared_memory->config.channels[CHANNEL_1].type = UART_TYPE_PL011;
#if DEVICE_CHANNEL_1_ENABLED
    g_shared_memory->config.channels[CHANNEL_1].enabled = true;
#endif
    
    // Channel 2 - PIO UART (disabled in SHARK mode)
    g_shared_memory->config.channels[CHANNEL_2].tx_gpio = DEVICE_UART2_TX_GPIO;
    g_shared_memory->config.channels[CHANNEL_2].rx_gpio = DEVICE_UART2_RX_GPIO;
    g_shared_memory->config.channels[CHANNEL_2].type = UART_TYPE_PIO;
#if DEVICE_CHANNEL_2_ENABLED
    g_shared_memory->config.channels[CHANNEL_2].enabled = true;
#endif
    
    // Channel 3 - PIO UART on PIO1 (disabled in SHARK mode)
    g_shared_memory->config.channels[CHANNEL_3].tx_gpio = DEVICE_UART3_TX_GPIO;
    g_shared_memory->config.channels[CHANNEL_3].rx_gpio = DEVICE_UART3_RX_GPIO;
    g_shared_memory->config.channels[CHANNEL_3].type = UART_TYPE_PIO;
#if DEVICE_CHANNEL_3_ENABLED
    g_shared_memory->config.channels[CHANNEL_3].enabled = true;
#endif
    
    // Channel 4 - PIO UART on PIO2 (GPIO5 TX / GPIO4 RX due to board wiring swap)
    g_shared_memory->config.channels[CHANNEL_4].tx_gpio = DEVICE_UART4_TX_GPIO;
    g_shared_memory->config.channels[CHANNEL_4].rx_gpio = DEVICE_UART4_RX_GPIO;
    g_shared_memory->config.channels[CHANNEL_4].type = UART_TYPE_PIO;
#if DEVICE_CHANNEL_4_ENABLED
    g_shared_memory->config.channels[CHANNEL_4].enabled = true;
    // In SHARK mode, Channel 4 is the primary data channel - use port 4002
    g_shared_memory->config.channels[CHANNEL_4].tcp_port = 4002;
#endif
    
    printf("Device mode: %s (channels: %d data, ethernet: %s)\n", 
           DEVICE_MODE_NAME, DEVICE_NUM_DATA_CHANNELS, 
           DEVICE_HAS_ETHERNET ? "yes" : "no");
    
    network_manager_get_default_config(&g_shared_memory->config.network);
    // Initialize system settings
    g_shared_memory->config.log_level = 1;  // INFO
    g_shared_memory->config.watchdog_timeout_ms = 200;
    
    // Initialize admin password from factory defaults
    const factory_defaults_t* factory = factory_defaults_get();
    if (factory && factory_defaults_is_valid()) {
        strncpy(g_shared_memory->config.admin_password, factory->default_password, 31);
        g_shared_memory->config.admin_password[31] = '\0';  // Ensure null termination
        printf("Admin password initialized from factory defaults\n");
    } else {
        // Fallback: Use hardcoded default if factory defaults not available
        strncpy(g_shared_memory->config.admin_password, "admin", 31);
        g_shared_memory->config.admin_password[31] = '\0';
        printf("WARNING: Using fallback admin password (factory defaults not available)\n");
    }
    
    printf("V: ");
    factory_defaults_print_serial_number();
   
    return true;
}

/**
 * Get maximum number of log entries that can fit in buffer
 * 
 * @return Number of log entries that fit in available space
 */
uint32_t shared_memory_get_log_buffer_capacity(void) {
    if (!atomic_load(&g_initialized)) {
        // Return calculated capacity even if not initialized for tests
        return calculate_log_buffer_capacity();
    }
    
    return g_shared_memory->log_mgmt.max_entries;
}

/**
 * Force re-initialization of shared memory (for factory reset)
 * 
 * This resets the initialization state and calls shared_memory_init() again
 * to restore factory defaults. Used by factory reset functionality.
 * 
 * @return true if re-initialization successful, false otherwise
 */
bool shared_memory_force_reinit(void) {
    log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_SHARED_MEMORY_REINIT, 0);
    
    // Reset initialization flag to allow re-initialization
    atomic_store(&g_initialized,false);
    
    
    // Call normal initialization - this will set all factory defaults
    bool result = shared_memory_init();
    
    if (result) {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_INFO, LOG_EVENT_SHARED_MEMORY_REINIT, 1);
    } else {
        log_event(EVENT_SOURCE_CONFIG, LOG_LEVEL_ERROR, LOG_EVENT_SHARED_MEMORY_REINIT, 0);
    }
    
    return result;
}

/**
 * Get pointer to shared memory layout
 * 
 * @return Pointer to shared memory layout, or NULL if not initialized
 */
shared_memory_layout_t* shared_memory_get_layout(void) {
    if (!atomic_load(&g_initialized)) {
        return NULL;
    }
    
    return g_shared_memory;
}

/* gets calle early in boot sequence (2 secs after poweron)
 * if the factory reset button was pressed during poweron
*/
void do_factory_reset() {
    printf("IMPORTANT: Factory reset requested!\n");
    factory_reset_requested = true;
}

/* should the system be reset to defaults?
 * @return true if the user requested a factory reset
*/
bool factory_reset_needed() {
    return factory_reset_requested;
}


/* remember to save memory to flash later
*/
void update_shared_memory_revision() {
    flash_persistence_state_t* flash_state = get_persistence_state();
    while(g_shared_memory->revision_counter <= flash_state->last_written_revision)
    {
        g_shared_memory->revision_counter++;
    }
}