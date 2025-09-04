/**
 * @file test_gpio_loopback.c
 * @brief GPIO loopback connectivity test for GPIO 4 and GPIO 5
 * 
 * Tests if GPIO 4 (output) and GPIO 5 (input) are physically connected
 * by writing different values and reading them back.
 * 
 * This helps diagnose hardware connectivity issues for UART1 loopback testing.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>

#define GPIO_OUTPUT_PIN 4   // GPIO 4 - UART1 TX
#define GPIO_INPUT_PIN  5   // GPIO 5 - UART1 RX

/**
 * @brief Test GPIO loopback connectivity
 * @return true if GPIO pins are connected, false otherwise
 */
bool test_gpio_loopback_connection(void) {
    printf("=== GPIO Loopback Connectivity Test ===\n");
    printf("Testing GPIO %d (output) -> GPIO %d (input)\n", GPIO_OUTPUT_PIN, GPIO_INPUT_PIN);
    
    // Configure GPIO 4 as output
    gpio_init(GPIO_OUTPUT_PIN);
    gpio_set_dir(GPIO_OUTPUT_PIN, GPIO_OUT);
    printf("GPIO %d configured as OUTPUT\n", GPIO_OUTPUT_PIN);
    
    // Configure GPIO 5 as input with pull-down
    gpio_init(GPIO_INPUT_PIN);
    gpio_set_dir(GPIO_INPUT_PIN, GPIO_IN);
    gpio_pull_down(GPIO_INPUT_PIN);  // Pull-down to ensure clean low state
    printf("GPIO %d configured as INPUT with pull-down\n", GPIO_INPUT_PIN);
    
    // Wait for GPIO to stabilize
    sleep_ms(10);
    
    bool connection_ok = true;
    int test_count = 0;
    int pass_count = 0;
    
    printf("\nRunning connectivity tests...\n");
    
    // Test 1: Output LOW, expect LOW input
    printf("Test 1: Setting GPIO %d = LOW... ", GPIO_OUTPUT_PIN);
    gpio_put(GPIO_OUTPUT_PIN, false);
    sleep_ms(5);  // Short delay for signal stabilization
    bool input_low = gpio_get(GPIO_INPUT_PIN);
    test_count++;
    if (!input_low) {
        printf("PASS (GPIO %d reads LOW)\n", GPIO_INPUT_PIN);
        pass_count++;
    } else {
        printf("FAIL (GPIO %d reads HIGH, expected LOW)\n", GPIO_INPUT_PIN);
        connection_ok = false;
    }
    
    // Test 2: Output HIGH, expect HIGH input
    printf("Test 2: Setting GPIO %d = HIGH... ", GPIO_OUTPUT_PIN);
    gpio_put(GPIO_OUTPUT_PIN, true);
    sleep_ms(5);  // Short delay for signal stabilization
    bool input_high = gpio_get(GPIO_INPUT_PIN);
    test_count++;
    if (input_high) {
        printf("PASS (GPIO %d reads HIGH)\n", GPIO_INPUT_PIN);
        pass_count++;
    } else {
        printf("FAIL (GPIO %d reads LOW, expected HIGH)\n", GPIO_INPUT_PIN);
        connection_ok = false;
    }
    
    // Test 3: Multiple toggle test
    printf("Test 3: Multiple toggle test (10 cycles)... ");
    bool toggle_ok = true;
    for (int i = 0; i < 10; i++) {
        bool output_state = (i % 2) == 0;
        gpio_put(GPIO_OUTPUT_PIN, output_state);
        sleep_ms(2);
        bool input_state = gpio_get(GPIO_INPUT_PIN);
        
        if (input_state != output_state) {
            toggle_ok = false;
            break;
        }
    }
    test_count++;
    if (toggle_ok) {
        printf("PASS (All 10 toggles matched)\n");
        pass_count++;
    } else {
        printf("FAIL (Toggle mismatch detected)\n");
        connection_ok = false;
    }
    
    // Test 4: Timing test - rapid changes
    printf("Test 4: Rapid signal changes (100 cycles)... ");
    bool timing_ok = true;
    for (int i = 0; i < 100; i++) {
        gpio_put(GPIO_OUTPUT_PIN, true);
        sleep_us(100);  // 100 microseconds
        if (!gpio_get(GPIO_INPUT_PIN)) {
            timing_ok = false;
            break;
        }
        
        gpio_put(GPIO_OUTPUT_PIN, false);
        sleep_us(100);
        if (gpio_get(GPIO_INPUT_PIN)) {
            timing_ok = false;
            break;
        }
    }
    test_count++;
    if (timing_ok) {
        printf("PASS (All rapid changes detected)\n");
        pass_count++;
    } else {
        printf("FAIL (Rapid signal changes not detected properly)\n");
        connection_ok = false;
    }
    
    // Summary
    printf("\n=== Test Results Summary ===\n");
    printf("Tests passed: %d/%d\n", pass_count, test_count);
    printf("GPIO Connection Status: %s\n", connection_ok ? "CONNECTED" : "NOT CONNECTED");
    
    if (!connection_ok) {
        printf("\nDiagnosis:\n");
        printf("- Check physical jumper wire between GPIO %d and GPIO %d\n", GPIO_OUTPUT_PIN, GPIO_INPUT_PIN);
        printf("- Verify connections are secure and not loose\n");
        printf("- Ensure no other components are interfering with these pins\n");
    } else {
        printf("\nHardware connectivity verified - GPIO loopback is working!\n");
        printf("The UART1 issue must be in the software configuration or UART driver.\n");
    }
    
    return connection_ok;
}

/**
 * @brief Alternative test using different pull resistor configuration
 */
void test_gpio_with_pullup(void) {
    printf("\n=== Alternative Test with Pull-Up ===\n");
    
    // Reconfigure GPIO 5 with pull-up instead of pull-down
    gpio_pull_up(GPIO_INPUT_PIN);
    printf("GPIO %d reconfigured with pull-up resistor\n", GPIO_INPUT_PIN);
    sleep_ms(10);
    
    // Test with pull-up - should read HIGH when disconnected
    printf("Testing with pull-up (should read HIGH if disconnected):\n");
    
    gpio_put(GPIO_OUTPUT_PIN, false);
    sleep_ms(5);
    bool input_with_pullup = gpio_get(GPIO_INPUT_PIN);
    
    if (input_with_pullup) {
        printf("- GPIO %d reads HIGH with pull-up and LOW output\n", GPIO_INPUT_PIN);
        printf("- This suggests the pins are NOT connected\n");
    } else {
        printf("- GPIO %d reads LOW with pull-up and LOW output\n", GPIO_INPUT_PIN);
        printf("- This suggests the pins ARE connected\n");
    }
}

/**
 * @brief Main function for standalone GPIO test
 */
int main(void) {
    // Initialize standard I/O
    stdio_init_all();
    
    // Wait for USB serial connection (optional)
    sleep_ms(2000);
    
    printf("\n\n");
    printf("========================================\n");
    printf("GPIO LOOPBACK CONNECTIVITY TEST\n");
    printf("========================================\n");
    printf("Purpose: Verify GPIO 4 <-> GPIO 5 connection\n");
    printf("Required: Physical jumper wire between pins\n");
    printf("========================================\n\n");
    
    // Run the main connectivity test
    bool connected = test_gpio_loopback_connection();
    
    // Run alternative test with different pull configuration
    test_gpio_with_pullup();
    
    printf("\n========================================\n");
    if (connected) {
        printf("RESULT: GPIO HARDWARE CONNECTION IS OK\n");
        printf("NEXT: Check UART1 software configuration\n");
    } else {
        printf("RESULT: GPIO HARDWARE CONNECTION FAILED\n");
        printf("NEXT: Fix physical wiring between GPIO 4 and GPIO 5\n");
    }
    printf("========================================\n\n");
    
    // Keep the program running for observation
    while (true) {
        sleep_ms(1000);
    }
    
    return 0;
}
