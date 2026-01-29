/**
 * @file update_manager.h
 * @brief Firmware update module for OTA updates with A/B partition support
 * 
 * Provides firmware update functionality using RP2350's Try-Before-You-Buy (TBYB)
 * mechanism. Handles UF2 block processing, flash operations, and partition management.
 * 
 * Documentation Reference:
 * - ADR-017: Update Module Architecture
 * - arc42 Chapter 5 - Update Manager Building Block
 */

#ifndef UPDATE_MANAGER_H
#define UPDATE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Update state enumeration
 * 
 * Tracks the current state of a firmware upload session.
 */
typedef enum {
    UPDATE_STATE_IDLE,          ///< No update in progress
    UPDATE_STATE_RECEIVING,     ///< Currently receiving firmware data
    UPDATE_STATE_COMPLETE,      ///< Upload completed successfully
    UPDATE_STATE_ERROR          ///< Upload failed
} update_state_t;

/**
 * @brief Reboot reason enumeration
 * 
 * Identifies the reason for system reboot. Logged before reboot
 * and persisted through flash_persistence.
 */
typedef enum {
    REBOOT_REASON_NONE = 0,
    REBOOT_REASON_UPDATE_BUY_FAILED,     ///< Buy failed, bootloader reverts to old image
    REBOOT_REASON_UPDATE_COMPLETE,        ///< Upload complete, reboot to try new image
    REBOOT_REASON_USER_REQUESTED,         ///< User triggered reboot via web UI
    REBOOT_REASON_ERROR_RECOVERY          ///< Unrecoverable error requiring reboot
} reboot_reason_t;

/**
 * @brief Update statistics structure
 */
typedef struct {
    uint32_t bytes_received;        ///< Total bytes received in current upload
    uint32_t bytes_written;         ///< Total bytes written to flash
    uint32_t blocks_processed;      ///< Number of UF2 blocks processed
    uint32_t sectors_erased;        ///< Number of flash sectors erased
    uint32_t last_error_code;       ///< Last error code (0 = no error)
} update_stats_t;

// ============================================================================
// Initialization
// ============================================================================

/**
 * @brief Initialize the update module
 * 
 * Performs one-time initialization including workarea allocation.
 * Safe to call multiple times.
 * 
 * @return true if initialization successful, false otherwise
 */
bool update_init(void);

// ============================================================================
// Upload Handling (called by http_server for firmware uploads)
// ============================================================================

/**
 * @brief Start a new firmware upload session
 * 
 * Initializes upload state, discovers target partition using ROM functions,
 * and validates that the firmware will fit.
 * 
 * @param expected_size Expected total size of firmware in bytes
 * @return true if upload session started, false on error (partition too small, etc.)
 */
bool update_start_upload(uint32_t expected_size);

/**
 * @brief Process a block of firmware data
 * 
 * Writes firmware data to flash. Handles sector erase as needed.
 * Uses flash_safe_execute() for dual-core safety.
 * 
 * @param data Pointer to firmware data (NULL if finished=true and flushing)
 * @param size Size of data in bytes
 * @param finished true if this is the final call (triggers cleanup/verification)
 * @return true if block processed successfully, false on error
 */
bool update_write_block(uint8_t* data, uint32_t size, bool finished);

/**
 * @brief Abort current upload session
 * 
 * Cleans up upload state. Call on connection failure or user cancellation.
 */
void update_abort_upload(void);

/**
 * @brief Get current update state
 * 
 * @return Current update_state_t value
 */
update_state_t update_get_state(void);

/**
 * @brief Get update statistics
 * 
 * @param stats Pointer to stats structure to fill
 */
void update_get_stats(update_stats_t* stats);

// ============================================================================
// Buy Operation (called from CORE1_BUY_UPDATE state)
// ============================================================================

/**
 * @brief Buy (confirm) the current firmware image
 * 
 * Marks the currently running firmware as "good" using rom_explicit_buy().
 * Should be called after successful boot into a new firmware image.
 * Uses flash_safe_execute() for dual-core safety.
 * 
 * @return true if buy succeeded, false if buy failed (triggers reboot to old image)
 */
bool update_buy_current_image(void);

// ============================================================================
// Reboot Handling (called from CORE1_REBOOT_* states)
// ============================================================================

/**
 * @brief Set the reboot reason
 * 
 * Sets the reason that will be logged before reboot.
 * 
 * @param reason The reboot reason
 */
void update_set_reboot_reason(reboot_reason_t reason);

/**
 * @brief Get the current reboot reason
 * 
 * @return Current reboot_reason_t value
 */
reboot_reason_t update_get_reboot_reason(void);

/**
 * @brief Execute system reboot
 * 
 * Performs the actual reboot using SDK watchdog_reboot().
 * This function does not return.
 */
void update_execute_reboot(void);

/**
 * @brief Get human-readable string for reboot reason
 * 
 * @param reason The reboot reason
 * @return Constant string describing the reason
 */
const char* update_reboot_reason_to_string(reboot_reason_t reason);

#endif // UPDATE_MANAGER_H
