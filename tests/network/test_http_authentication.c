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

// Password change validation is now declared in http_server.h
// No need to redeclare here

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

/**
 * Test: Password validation should succeed with valid inputs
 * 
 * Tests that password change validation accepts properly formatted
 * password change with all criteria met.
 * 
 * Reference: ADR-016 - Password Change Validation Rules
 */
void test_password_change_validation_success(void) {
    // ARRANGE: Valid password change inputs
    const char* current_pwd = "oldpassword";
    const char* new_pwd = "newpassword123";  // 14 chars (valid: 8-31)
    const char* confirm_pwd = "newpassword123";
    const char* stored_pwd = "oldpassword";
    
    // ACT: Validate password change
    password_change_result_t result = http_validate_password_change(
        current_pwd, new_pwd, confirm_pwd, stored_pwd
    );
    
    // ASSERT: Validation should succeed
    TEST_ASSERT_EQUAL_MESSAGE(PWD_CHANGE_OK, result,
        "Valid password change should pass validation");
}

/**
 * Test: Password validation should reject wrong current password
 * 
 * Tests that validation fails when current password doesn't match.
 * 
 * Reference: ADR-016 - Password Change Validation Rules
 */
void test_password_change_wrong_current_password(void) {
    // ARRANGE: Wrong current password
    const char* current_pwd = "wrongpassword";
    const char* new_pwd = "newpassword123";
    const char* confirm_pwd = "newpassword123";
    const char* stored_pwd = "correctpassword";
    
    // ACT: Validate password change
    password_change_result_t result = http_validate_password_change(
        current_pwd, new_pwd, confirm_pwd, stored_pwd
    );
    
    // ASSERT: Validation should fail with CURRENT_WRONG
    TEST_ASSERT_EQUAL_MESSAGE(PWD_CHANGE_CURRENT_WRONG, result,
        "Wrong current password should fail validation");
}

/**
 * Test: Password validation should reject password too short
 * 
 * Tests that validation enforces minimum password length of 8 characters.
 * 
 * Reference: ADR-016 - Password Change Validation Rules
 */
void test_password_change_too_short(void) {
    // ARRANGE: New password too short (7 chars, need 8)
    const char* current_pwd = "oldpass";
    const char* new_pwd = "short12";  // 7 chars (too short)
    const char* confirm_pwd = "short12";
    const char* stored_pwd = "oldpass";
    
    // ACT: Validate password change
    password_change_result_t result = http_validate_password_change(
        current_pwd, new_pwd, confirm_pwd, stored_pwd
    );
    
    // ASSERT: Validation should fail with TOO_SHORT
    TEST_ASSERT_EQUAL_MESSAGE(PWD_CHANGE_TOO_SHORT, result,
        "Password with 7 chars should be rejected as too short");
}

/**
 * Test: Password validation should reject password too long
 * 
 * Tests that validation enforces maximum password length of 31 characters.
 * 
 * Reference: ADR-016 - Password Change Validation Rules
 */
void test_password_change_too_long(void) {
    // ARRANGE: New password too long (32 chars, max is 31)
    const char* current_pwd = "oldpass";
    const char* new_pwd = "12345678901234567890123456789012";  // 32 chars
    const char* confirm_pwd = "12345678901234567890123456789012";
    const char* stored_pwd = "oldpass";
    
    // ACT: Validate password change
    password_change_result_t result = http_validate_password_change(
        current_pwd, new_pwd, confirm_pwd, stored_pwd
    );
    
    // ASSERT: Validation should fail with TOO_LONG
    TEST_ASSERT_EQUAL_MESSAGE(PWD_CHANGE_TOO_LONG, result,
        "Password with 32 chars should be rejected as too long");
}

/**
 * Test: Password validation should reject mismatched confirmation
 * 
 * Tests that new password and confirmation must match exactly.
 * 
 * Reference: ADR-016 - Password Change Validation Rules
 */
void test_password_change_confirmation_mismatch(void) {
    // ARRANGE: New password and confirmation don't match
    const char* current_pwd = "oldpass";
    const char* new_pwd = "newpassword123";
    const char* confirm_pwd = "newpassword456";  // Different!
    const char* stored_pwd = "oldpass";
    
    // ACT: Validate password change
    password_change_result_t result = http_validate_password_change(
        current_pwd, new_pwd, confirm_pwd, stored_pwd
    );
    
    // ASSERT: Validation should fail with NO_MATCH
    TEST_ASSERT_EQUAL_MESSAGE(PWD_CHANGE_NO_MATCH, result,
        "Mismatched password confirmation should fail validation");
}

/**
 * Test: Password validation should reject empty current password
 * 
 * Tests that all fields must be non-empty.
 * 
 * Reference: ADR-016 - Password Change Validation Rules
 */
void test_password_change_empty_current_password(void) {
    // ARRANGE: Empty current password
    const char* current_pwd = "";
    const char* new_pwd = "newpassword123";
    const char* confirm_pwd = "newpassword123";
    const char* stored_pwd = "oldpass";
    
    // ACT: Validate password change
    password_change_result_t result = http_validate_password_change(
        current_pwd, new_pwd, confirm_pwd, stored_pwd
    );
    
    // ASSERT: Validation should fail with EMPTY_FIELD
    TEST_ASSERT_EQUAL_MESSAGE(PWD_CHANGE_EMPTY_FIELD, result,
        "Empty current password should fail validation");
}

/**
 * Test: Password validation should reject empty new password
 * 
 * Tests that all fields must be non-empty.
 * 
 * Reference: ADR-016 - Password Change Validation Rules
 */
void test_password_change_empty_new_password(void) {
    // ARRANGE: Empty new password
    const char* current_pwd = "oldpass";
    const char* new_pwd = "";
    const char* confirm_pwd = "newpassword123";
    const char* stored_pwd = "oldpass";
    
    // ACT: Validate password change
    password_change_result_t result = http_validate_password_change(
        current_pwd, new_pwd, confirm_pwd, stored_pwd
    );
    
    // ASSERT: Validation should fail with EMPTY_FIELD
    TEST_ASSERT_EQUAL_MESSAGE(PWD_CHANGE_EMPTY_FIELD, result,
        "Empty new password should fail validation");
}

/**
 * Test: Password validation with exactly 8 characters (boundary test)
 * 
 * Tests that minimum valid password length (8 chars) is accepted.
 * 
 * Reference: ADR-016 - Password Change Validation Rules
 */
void test_password_change_minimum_valid_length(void) {
    // ARRANGE: New password exactly 8 chars (minimum valid)
    const char* current_pwd = "oldpass";
    const char* new_pwd = "12345678";  // Exactly 8 chars
    const char* confirm_pwd = "12345678";
    const char* stored_pwd = "oldpass";
    
    // ACT: Validate password change
    password_change_result_t result = http_validate_password_change(
        current_pwd, new_pwd, confirm_pwd, stored_pwd
    );
    
    // ASSERT: Validation should succeed
    TEST_ASSERT_EQUAL_MESSAGE(PWD_CHANGE_OK, result,
        "8-character password should be accepted (minimum valid length)");
}

/**
 * Test: Password validation with exactly 31 characters (boundary test)
 * 
 * Tests that maximum valid password length (31 chars) is accepted.
 * 
 * Reference: ADR-016 - Password Change Validation Rules
 */
void test_password_change_maximum_valid_length(void) {
    // ARRANGE: New password exactly 31 chars (maximum valid)
    const char* current_pwd = "oldpass";
    const char* new_pwd = "1234567890123456789012345678901";  // Exactly 31 chars
    const char* confirm_pwd = "1234567890123456789012345678901";
    const char* stored_pwd = "oldpass";
    
    // ACT: Validate password change
    password_change_result_t result = http_validate_password_change(
        current_pwd, new_pwd, confirm_pwd, stored_pwd
    );
    
    // ASSERT: Validation should succeed
    TEST_ASSERT_EQUAL_MESSAGE(PWD_CHANGE_OK, result,
        "31-character password should be accepted (maximum valid length)");
}

// Main test runner
int main(void) {
    stdio_init_all();
    sleep_ms(2000);  // Wait for USB serial to stabilize
    
    printf("\n=== HTTP Authentication and Password Management Tests ===\n");
    
    UNITY_BEGIN();
    
    // Base64 decoding tests
    printf("\n--- Base64 Decoding Tests ---\n");
    RUN_TEST(test_base64_decode_simple_string);
    RUN_TEST(test_base64_decode_username_only);
    RUN_TEST(test_base64_decode_empty_string);
    RUN_TEST(test_base64_decode_invalid_input);
    
    // Authentication validation tests
    printf("\n--- Authentication Validation Tests ---\n");
    RUN_TEST(test_authentication_valid_credentials);
    RUN_TEST(test_authentication_wrong_password);
    RUN_TEST(test_authentication_wrong_username);
    RUN_TEST(test_authentication_no_auth_header);
    
    // Password initialization test
    printf("\n--- Password Initialization Tests ---\n");
    RUN_TEST(test_password_initialized_from_factory_defaults);
    
    // Password change validation tests
    printf("\n--- Password Change Validation Tests ---\n");
    RUN_TEST(test_password_change_validation_success);
    RUN_TEST(test_password_change_wrong_current_password);
    RUN_TEST(test_password_change_too_short);
    RUN_TEST(test_password_change_too_long);
    RUN_TEST(test_password_change_confirmation_mismatch);
    RUN_TEST(test_password_change_empty_current_password);
    RUN_TEST(test_password_change_empty_new_password);
    RUN_TEST(test_password_change_minimum_valid_length);
    RUN_TEST(test_password_change_maximum_valid_length);
    
    return UNITY_END();
}
