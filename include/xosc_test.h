/**
 * @file xosc_test.h
 * @brief XOSC signal verification functions header
 */

#ifndef XOSC_TEST_H
#define XOSC_TEST_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Check XOSC status registers and print diagnostics
 */
void xosc_diagnostic_check(void);

/**
 * @brief Measure XOSC frequency using built-in frequency counter
 */
void measure_xosc_frequency(void);

/**
 * @brief Output XOSC clock to GPIO for external measurement
 * @param gpio_pin GPIO pin to output clock (recommend GPIO 22)
 */
void output_xosc_for_measurement(uint gpio_pin);

/**
 * @brief Comprehensive XOSC verification test
 */
void test_xosc_signal(void);

/**
 * @brief Quick XOSC status check (call this in main loop)
 * @return true if XOSC is stable and enabled, false otherwise
 */
bool is_xosc_working(void);

#endif // XOSC_TEST_H