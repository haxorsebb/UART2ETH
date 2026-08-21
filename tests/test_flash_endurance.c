/**
 * @file test_flash_endurance.c
 * @brief Guard: flash persistence geometry sustains at least 20 years
 *
 * The configuration/log persistence writes into a ring of
 * FLASH_PERSISTENCE_RING_SIZE blocks, so each block - and with it each of
 * its 4K sectors - is erased once every RING_SIZE saves (ADR-006, erase
 * budget). Periodic saves are throttled to at most one per
 * FLASH_PERSISTENCE_MAX_WRITE_INTERVAL_MS. With a sector endurance of
 * 100000 program/erase cycles the worst-case flash lifetime is therefore
 *
 *   lifetime = endurance * RING_SIZE * write_interval
 *
 * This test recomputes that lifetime from the current constants and fails
 * when it drops below 20 years, so a future change to the ring geometry or
 * the write interval cannot silently reduce the endurance below the
 * requirement. Regression guard for the flash wear defect fixed by the
 * 64 x 8K ring layout.
 *
 * Not covered: explicit forced saves (configuration changes via the web
 * interface, reboot flush) bypass the throttle. They are rare
 * administrator actions and not part of the sustained-rate budget.
 *
 * Documentation Reference:
 * - ADR-006: Flash Persistence Strategy (ring geometry, erase budget)
 * - arc42 Chapter 11, R-007/TD register (component lifetime)
 */

#include "unity.h"
#include "flash_persistence.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <stdint.h>

// Endurance assumption and lifetime requirement are single-sourced in
// flash_persistence.h, where a _Static_assert enforces the same property
// at compile time. This test re-verifies it on target and prints the
// numbers for the test log.

void setUp(void) {
}

void tearDown(void) {
}

/**
 * Test: Worst-case sustained write rate keeps every flash sector below its
 * endurance limit for at least 20 years.
 */
void test_flash_lifetime_at_least_20_years(void) {
    // One save erases each sector of exactly one block; a given sector is
    // therefore erased once per RING_SIZE saves. Saves are throttled to
    // one per FLASH_PERSISTENCE_MAX_WRITE_INTERVAL_MS.
    uint64_t seconds_between_saves =
        (uint64_t)FLASH_PERSISTENCE_MAX_WRITE_INTERVAL_MS / 1000ULL;
    uint64_t seconds_between_erases_of_one_sector =
        seconds_between_saves * (uint64_t)FLASH_PERSISTENCE_RING_SIZE;
    uint64_t lifetime_seconds =
        seconds_between_erases_of_one_sector
        * FLASH_PERSISTENCE_SECTOR_ENDURANCE_CYCLES;

    uint64_t required_seconds = FLASH_PERSISTENCE_MIN_LIFETIME_YEARS
                                * FLASH_PERSISTENCE_SECONDS_PER_YEAR;

    printf("Flash endurance: ring=%d blocks, min save interval=%u ms, "
           "endurance=%llu cycles/sector\n",
           FLASH_PERSISTENCE_RING_SIZE,
           (unsigned)FLASH_PERSISTENCE_MAX_WRITE_INTERVAL_MS,
           (unsigned long long)FLASH_PERSISTENCE_SECTOR_ENDURANCE_CYCLES);
    printf("Flash endurance: worst-case lifetime %llu s (~%llu years), "
           "required %llu s (%llu years)\n",
           (unsigned long long)lifetime_seconds,
           (unsigned long long)(lifetime_seconds
                                / FLASH_PERSISTENCE_SECONDS_PER_YEAR),
           (unsigned long long)required_seconds,
           (unsigned long long)FLASH_PERSISTENCE_MIN_LIFETIME_YEARS);

    TEST_ASSERT_TRUE_MESSAGE(lifetime_seconds >= required_seconds,
        "Flash lifetime below 20 years: increase FLASH_PERSISTENCE_RING_SIZE "
        "or FLASH_PERSISTENCE_MAX_WRITE_INTERVAL_MS");
}

/**
 * Test: The lifetime calculation's geometric premise holds - a block is a
 * whole number of erase sectors, so one save erases each of its sectors
 * exactly once.
 */
void test_block_is_whole_number_of_sectors(void) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0,
        FLASH_PERSISTENCE_BLOCK_SIZE % FLASH_PERSISTENCE_PAGE_SIZE,
        "Ring block size must be a multiple of the flash sector size");
    TEST_ASSERT_TRUE_MESSAGE(
        FLASH_PERSISTENCE_BLOCK_SIZE >= FLASH_PERSISTENCE_PAGE_SIZE,
        "Ring block must contain at least one flash sector");
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);  // Allow USB/UART enumeration before test output

    printf("\n=== Flash Endurance Lifetime Tests (ADR-006) ===\n");

    UNITY_BEGIN();
    RUN_TEST(test_flash_lifetime_at_least_20_years);
    RUN_TEST(test_block_is_whole_number_of_sectors);
    return UNITY_END();
}
