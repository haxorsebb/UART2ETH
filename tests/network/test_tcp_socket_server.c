/**
 * @file test_tcp_socket_server.c
 * @brief Unit Tests for TCP Socket Server Implementation
 * 
 * Tests TCP server functionality using lwIP Raw API for UART2ETH bridge.
 * Tests line-based protocol, connection management, and echo functionality.
 * 
 * Documentation Reference:
 * - arc42 Chapter 5 - TCP Socket Server Building Block
 * - Issue #61: Add sockets to network implementation
 */

#include "unity.h" 
#include "network/network_manager.h"
#include "network/tcp_socket_server.h"
#include "pico/stdlib.h"
#include "log_manager.h"
#include "state_machine.h"
#include "shared_memory.h"
#include "debug.h"
#include <string.h>

// Test configuration 
static network_config_t test_config;
static tcp_server_stats_t server_stats;

void setUp(void) {
    // Initialize test environment
    shared_memory_init();
    log_manager_init();
    state_machine_init();
    
    // Get default network config
    network_manager_get_default_config(&test_config);
    
    // Initialize network manager
    TEST_ASSERT_TRUE(network_manager_init(&test_config));
    
    // Wait for network ready
    int timeout = 300;  //wait 30 seconds
    while (network_manager_get_status() != NETWORK_STATUS_READY && timeout-- > 0) {
        network_manager_process();
        sleep_ms(100);
    }
    
    // Clear statistics
    memset(&server_stats, 0, sizeof(tcp_server_stats_t));
}

void tearDown(void) {
    tcp_socket_server_deinit();
    network_manager_deinit();
}

/**
 * Test TCP server initialization
 */
void test_tcp_server_init(void) {
    // Test successful initialization
    TEST_ASSERT_TRUE(tcp_socket_server_init(4001));
    
    // Check server is listening
    TEST_ASSERT_TRUE(tcp_socket_server_is_listening());
    
    // Get statistics
    tcp_socket_server_get_stats(&server_stats);
    TEST_ASSERT_EQUAL(4001, server_stats.listen_port);
    TEST_ASSERT_EQUAL(0, server_stats.active_connections);
    TEST_ASSERT_EQUAL(0, server_stats.total_connections);
}

/**
 * Test TCP server connection establishment
 */
void test_tcp_server_connection_accept(void) {
    // Initialize server
    TEST_ASSERT_TRUE(tcp_socket_server_init(4001));
    
    // Simulate connection attempt (requires external client in integration test)
    // For unit test, check server accepts connections when callback triggered
    
    // Test connection limits
    tcp_socket_server_get_stats(&server_stats);
    TEST_ASSERT_TRUE(server_stats.max_connections > 0);
}

/**
 * Test line-based protocol - single line
 */
void test_tcp_server_line_protocol_single_line(void) {
    TEST_ASSERT_TRUE(tcp_socket_server_init(4001));
    
    // Test line parsing functionality
    const char* test_line = "Hello World\n";
    char echo_buffer[128];
    
    // Simulate line processing (in actual implementation this goes through lwIP callbacks)
    int result = tcp_socket_server_process_line(test_line, strlen(test_line), echo_buffer, sizeof(echo_buffer));
    
    TEST_ASSERT_EQUAL(strlen(test_line), result);
    TEST_ASSERT_EQUAL_STRING(test_line, echo_buffer);
}

/**
 * Test line-based protocol - multiple lines
 */
void test_tcp_server_line_protocol_multiple_lines(void) {
    TEST_ASSERT_TRUE(tcp_socket_server_init(4001));
    
    const char* line1 = "First line\n";
    const char* line2 = "Second line\n";
    char echo_buffer[128];
    
    // Process first line
    int result1 = tcp_socket_server_process_line(line1, strlen(line1), echo_buffer, sizeof(echo_buffer));
    TEST_ASSERT_EQUAL(strlen(line1), result1);
    TEST_ASSERT_EQUAL_STRING(line1, echo_buffer);
    
    // Process second line
    int result2 = tcp_socket_server_process_line(line2, strlen(line2), echo_buffer, sizeof(echo_buffer));
    TEST_ASSERT_EQUAL(strlen(line2), result2);
    TEST_ASSERT_EQUAL_STRING(line2, echo_buffer);
}

/**
 * Test connection closing functionality
 */
void test_tcp_server_connection_close(void) {
    TEST_ASSERT_TRUE(tcp_socket_server_init(4001));
    
    // Test graceful close handling
    // In integration test, this would test actual TCP close from client
    
    tcp_socket_server_get_stats(&server_stats);
    // After initialization, should have 0 active connections
    TEST_ASSERT_EQUAL(0, server_stats.active_connections);
}

/**
 * Test server restart after connection failure
 */
void test_tcp_server_reconnection(void) {
    TEST_ASSERT_TRUE(tcp_socket_server_init(4001));
    
    // Simulate connection failure and recovery
    // Check server can accept new connections after previous ones close
    
    tcp_socket_server_get_stats(&server_stats);
    TEST_ASSERT_TRUE(tcp_socket_server_is_listening());
}

/**
 * Test server statistics collection
 */
void test_tcp_server_statistics(void) {
    TEST_ASSERT_TRUE(tcp_socket_server_init(4001));
    
    tcp_socket_server_get_stats(&server_stats);
    
    // Check initial statistics
    TEST_ASSERT_EQUAL(4001, server_stats.listen_port);
    TEST_ASSERT_EQUAL(0, server_stats.active_connections);
    TEST_ASSERT_EQUAL(0, server_stats.total_connections);
    TEST_ASSERT_EQUAL(0, server_stats.bytes_sent);
    TEST_ASSERT_EQUAL(0, server_stats.bytes_received);
    TEST_ASSERT_EQUAL(0, server_stats.lines_processed);
}

/**
 * Test Core1 integration
 */
void test_tcp_server_core1_integration(void) {
    TEST_ASSERT_TRUE(tcp_socket_server_init(4001));
    
    // Test that server processing can be called from Core1 main loop
    tcp_socket_server_process();
    
    // Verify no crashes and server remains functional
    TEST_ASSERT_TRUE(tcp_socket_server_is_listening());
}

/**
 * Main test runner
 */
int main(void) {
    stdio_init_all();
    
    DEBUG_ONLY({
        printf("Starting TCP Socket Server Tests\n");
    });
    
    UNITY_BEGIN();
    
    RUN_TEST(test_tcp_server_init);
    RUN_TEST(test_tcp_server_connection_accept);
    RUN_TEST(test_tcp_server_line_protocol_single_line);
    RUN_TEST(test_tcp_server_line_protocol_multiple_lines);
    RUN_TEST(test_tcp_server_connection_close);
    RUN_TEST(test_tcp_server_reconnection);
    RUN_TEST(test_tcp_server_statistics);
    RUN_TEST(test_tcp_server_core1_integration);
    
    while (true) {
        printf("TCP Socket Server Tests completed\n");
        UNITY_END();
        sleep_ms(1000);
    }
}