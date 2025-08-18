/**
 * @file test_message_framing.c
 * @brief Test message framing function for protocol compliance
 * 
 * Tests the check_message_end function to ensure it only returns true
 * when buffer ends with exactly '!\r\n'.
 */

#include "unity.h"
#include "network/tcp_socket_server.h"
#include <string.h>

// Forward declaration of function to test
bool check_message_end(const char* buffer, size_t length);

void setUp(void) {
    // Test setup
}

void tearDown(void) {
    // Test cleanup  
}

void test_check_message_end_valid_terminator(void) {
    char buffer[] = "#1234test!\r\n";
    size_t length = strlen(buffer);
    
    TEST_ASSERT_TRUE(check_message_end(buffer, length));
}

void test_check_message_end_no_terminator(void) {
    char buffer[] = "#1234test";
    size_t length = strlen(buffer);
    
    TEST_ASSERT_FALSE(check_message_end(buffer, length));
}

void test_check_message_end_partial_terminator_exclamation_only(void) {
    char buffer[] = "#1234test!";
    size_t length = strlen(buffer);
    
    TEST_ASSERT_FALSE(check_message_end(buffer, length));
}

void test_check_message_end_partial_terminator_exclamation_r(void) {
    char buffer[] = "#1234test!\r";
    size_t length = strlen(buffer);
    
    TEST_ASSERT_FALSE(check_message_end(buffer, length));
}

void test_check_message_end_wrong_order(void) {
    char buffer[] = "#1234test\r\n!";
    size_t length = strlen(buffer);
    
    TEST_ASSERT_FALSE(check_message_end(buffer, length));
}

void test_check_message_end_individual_chars_not_terminator(void) {
    char buffer1[] = "#1234test\r";
    char buffer2[] = "#1234test\n"; 
    char buffer3[] = "#1234test!something";
    
    TEST_ASSERT_FALSE(check_message_end(buffer1, strlen(buffer1)));
    TEST_ASSERT_FALSE(check_message_end(buffer2, strlen(buffer2)));
    TEST_ASSERT_FALSE(check_message_end(buffer3, strlen(buffer3)));
}

void test_check_message_end_buffer_too_short(void) {
    char buffer1[] = "!";
    char buffer2[] = "!\r";
    char buffer3[] = "#123!\r\n";  // 7 bytes - less than minimum 8
    
    TEST_ASSERT_FALSE(check_message_end(buffer1, strlen(buffer1)));
    TEST_ASSERT_FALSE(check_message_end(buffer2, strlen(buffer2)));
    TEST_ASSERT_FALSE(check_message_end(buffer3, strlen(buffer3)));
}

void test_check_message_end_empty_buffer(void) {
    TEST_ASSERT_FALSE(check_message_end(NULL, 0));
    TEST_ASSERT_FALSE(check_message_end("", 0));
}

void test_check_message_end_minimal_valid_message(void) {
    char buffer[] = "#0000!\r\n";  // Exactly 8 bytes - minimum valid
    size_t length = strlen(buffer);
    
    TEST_ASSERT_TRUE(check_message_end(buffer, length));
    TEST_ASSERT_EQUAL(8, length);  // Verify minimum length
}

void test_check_message_end_below_minimum_length(void) {
    // Even with correct terminator, reject if below 8 bytes
    char buffer[] = "!\r\n";  // Only 3 bytes
    size_t length = strlen(buffer);
    
    TEST_ASSERT_FALSE(check_message_end(buffer, length));
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_check_message_end_valid_terminator);
    RUN_TEST(test_check_message_end_no_terminator);
    RUN_TEST(test_check_message_end_partial_terminator_exclamation_only);
    RUN_TEST(test_check_message_end_partial_terminator_exclamation_r);
    RUN_TEST(test_check_message_end_wrong_order);
    RUN_TEST(test_check_message_end_individual_chars_not_terminator);
    RUN_TEST(test_check_message_end_buffer_too_short);
    RUN_TEST(test_check_message_end_empty_buffer);
    RUN_TEST(test_check_message_end_minimal_valid_message);
    RUN_TEST(test_check_message_end_below_minimum_length);
    
    return UNITY_END();
}
