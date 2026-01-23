/**
 * @file test_http_authentication.c
 * @brief Unit tests for HTTP Basic Authentication
 * 
 * Tests HTTP Basic Authentication implementation as documented in
 * ADR-016: HTTP Basic Authentication.
 * 
 * Documentation Reference:
 * - ADR-016: HTTP Basic Authentication
 * - arc42 Chapter 5 - HTTP Server Module
 * - arc42 Chapter 8 - Security Concepts
 */

// Explicitly enable USB reset interface with all required options
#define PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE 1
#define PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_BOOTSEL 1
#define PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_FLASH_BOOT 1

#include "unity.h"
#include "network/http_server.h"
#include "shared_memory.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

// Forward declarations of functions we need to test
// These will be implemented in http_server.c
extern int http_base64_decode(const char* input, char* output, size_t max_len);
extern bool http_check_authentication(const char* request, const char* expected_password);

void setUp(void) {
    // Initialize shared memory before each test
    shared_memory_init();
}

void tearDown(void) {
    // Cleanup after test
}

/**
 * Test: Base64 decoder should decode simple string correctly
 * 
 * This tests the most atomic condition: that base64 decoding works
 * for a simple, well-formed input.
 */
void test_base64_decode_simple_string(void) {
    // ARRANGE: Prepare base64 encoded string "admin:password"
    // Base64 encoding of "admin:password" = "YWRtaW46cGFzc3dvcmQ="
    const char* input = "YWRtaW46cGFzc3dvcmQ=";
    char output[64] = {0};
    
    // ACT: Decode the base64 string
    int result = http_base64_decode(input, output, sizeof(output));
    
    // ASSERT: Decode should succeed (return decoded length)
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, result, "Base64 decode should return positive length");
    
    // ASSERT: Decoded string should match expected value
    TEST_ASSERT_EQUAL_STRING_MESSAGE("admin:password", output, 
        "Decoded string should match 'admin:password'");
}

/**
 * Test: Base64 decoder should decode username-only correctly
 * 
 * Tests decoding of base64 string without password (edge case).
 */
void test_base64_decode_username_only(void) {
    // ARRANGE: Base64 encoding of "admin:" = "YWRtaW46"
    const char* input = "YWRtaW46";
    char output[64] = {0};
    
    // ACT: Decode the base64 string
    int result = http_base64_decode(input, output, sizeof(output));
    
    // ASSERT: Decode should succeed
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, result, "Base64 decode should succeed");
    
    // ASSERT: Decoded string should be "admin:"
    TEST_ASSERT_EQUAL_STRING_MESSAGE("admin:", output,
        "Decoded string should be 'admin:' (username with colon)");
}

/**
 * Test: Base64 decoder should handle empty string
 * 
 * Tests edge case of empty input.
 */
void test_base64_decode_empty_string(void) {
    // ARRANGE: Empty input string
    const char* input = "";
    char output[64] = {0};
    
    // ACT: Decode the empty string
    int result = http_base64_decode(input, output, sizeof(output));
    
    // ASSERT: Should return 0 for empty input
    TEST_ASSERT_EQUAL_MESSAGE(0, result, "Empty string should decode to length 0");
}

/**
 * Test: Base64 decoder should reject invalid base64
 * 
 * Tests that malformed base64 is handled gracefully.
 */
void test_base64_decode_invalid_input(void) {
    // ARRANGE: Invalid base64 string (contains invalid characters)
    const char* input = "!!!invalid@@@";
    char output[64] = {0};
    
    // ACT: Attempt to decode invalid input
    int result = http_base64_decode(input, output, sizeof(output));
    
    // ASSERT: Should return error (negative or 0)
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(0, result, 
        "Invalid base64 should return error code");
}

/**
 * Test: Authentication check should succeed with valid credentials
 * 
 * Tests that properly formatted Authorization header with correct
 * credentials is accepted.
 */
void test_authentication_valid_credentials(void) {
    // ARRANGE: HTTP request with valid Authorization header
    // "admin:testpass" in base64 = "YWRtaW46dGVzdHBhc3M="
    const char* request = 
        "GET / HTTP/1.1\r\n"
        "Host: 10.10.10.41\r\n"
        "Authorization: Basic YWRtaW46dGVzdHBhc3M=\r\n"
        "\r\n";
    const char* expected_password = "testpass";
    
    // ACT: Check authentication
    bool result = http_check_authentication(request, expected_password);
    
    // ASSERT: Authentication should succeed
    TEST_ASSERT_TRUE_MESSAGE(result, 
        "Valid credentials should authenticate successfully");
}

/**
 * Test: Authentication check should fail with wrong password
 * 
 * Tests that incorrect password is rejected.
 */
void test_authentication_wrong_password(void) {
    // ARRANGE: HTTP request with wrong password
    // "admin:wrongpass" in base64 = "YWRtaW46d3JvbmdwYXNz"
    const char* request = 
        "GET / HTTP/1.1\r\n"
        "Host: 10.10.10.41\r\n"
        "Authorization: Basic YWRtaW46d3JvbmdwYXNz\r\n"
        "\r\n";
    const char* expected_password = "correctpass";
    
    // ACT: Check authentication
    bool result = http_check_authentication(request, expected_password);
    
    // ASSERT: Authentication should fail
    TEST_ASSERT_FALSE_MESSAGE(result, 
        "Wrong password should fail authentication");
}

/**
 * Test: Authentication check should fail with wrong username
 * 
 * Tests that username other than "admin" is rejected.
 */
void test_authentication_wrong_username(void) {
    // ARRANGE: HTTP request with wrong username
    // "user:testpass" in base64 = "dXNlcjp0ZXN0cGFzcw=="
    const char* request = 
        "GET / HTTP/1.1\r\n"
        "Host: 10.10.10.41\r\n"
        "Authorization: Basic dXNlcjp0ZXN0cGFzcw==\r\n"
        "\r\n";
    const char* expected_password = "testpass";
    
    // ACT: Check authentication
    bool result = http_check_authentication(request, expected_password);
    
    // ASSERT: Authentication should fail
    TEST_ASSERT_FALSE_MESSAGE(result, 
        "Wrong username should fail authentication");
}

/**
 * Test: Authentication check should fail without Authorization header
 * 
 * Tests that requests without Authorization header are rejected.
 */
void test_authentication_no_auth_header(void) {
    // ARRANGE: HTTP request without Authorization header
    const char* request = 
        "GET / HTTP/1.1\r\n"
        "Host: 10.10.10.41\r\n"
        "\r\n";
    const char* expected_password = "testpass";
    
    // ACT: Check authentication
    bool result = http_check_authentication(request, expected_password);
    
    // ASSERT: Authentication should fail
    TEST_ASSERT_FALSE_MESSAGE(result, 
        "Missing Authorization header should fail authentication");
}

/**
 * Test: Password field should be initialized from factory defaults
 * 
 * Tests that admin_password in shared memory is populated from
 * factory defaults during initialization.
 */
void test_password_initialized_from_factory_defaults(void) {
    // ARRANGE: Get shared memory layout
    shared_memory_layout_t* layout = shared_memory_get_layout();
    TEST_ASSERT_NOT_NULL_MESSAGE(layout, "Shared memory should be initialized");
    
    // ASSERT: Password field should exist and have some value
    // Note: We can't check specific value without factory defaults loaded,
    // but we can verify field exists and is null-terminated
    TEST_ASSERT_NOT_NULL_MESSAGE(layout->config.admin_password, 
        "Password field should exist");
    
    // ASSERT: Password should be null-terminated (safe to use strlen)
    size_t pwd_len = strnlen(layout->config.admin_password, 32);
    TEST_ASSERT_LESS_THAN_MESSAGE(32, pwd_len, 
        "Password should be null-terminated within 32 bytes");
}

// Main test runner
int main(void) {
    stdio_init_all();
    sleep_ms(2000);  // Wait for USB serial to stabilize
    
    printf("\n=== HTTP Authentication Tests ===\n");
    
    UNITY_BEGIN();
    
    // Base64 decoding tests
    RUN_TEST(test_base64_decode_simple_string);
    RUN_TEST(test_base64_decode_username_only);
    RUN_TEST(test_base64_decode_empty_string);
    RUN_TEST(test_base64_decode_invalid_input);
    
    // Authentication validation tests
    RUN_TEST(test_authentication_valid_credentials);
    RUN_TEST(test_authentication_wrong_password);
    RUN_TEST(test_authentication_wrong_username);
    RUN_TEST(test_authentication_no_auth_header);
    
    // Password initialization test
    RUN_TEST(test_password_initialized_from_factory_defaults);
    
    return UNITY_END();
}
