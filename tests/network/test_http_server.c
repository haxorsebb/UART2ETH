/**
 * @file test_http_server.c
 * @brief Unit tests for HTTP server connection pool handling
 *
 * Tests the connection pool size and the behaviour of the accept callback
 * when the pool is exhausted.
 *
 * Documentation Reference:
 * - ADR-018: HTTP Server Modularization, Bug 4 (pool size) and Bug 5
 *   (double PCB release on pool exhaustion)
 * - arc42 Chapter 11, TD-005 (no idle timeout on pool slots)
 */

// Explicitly enable USB reset interface with all required options
#define PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE 1
#define PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_BOOTSEL 1
#define PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_FLASH_BOOT 1

#include "unity.h"
#include "network/http_server.h"
#include "shared_memory.h"
#include "pico/stdlib.h"
#include "lwip/init.h"
#include "lwip/tcp.h"
#include <stdio.h>
#include <string.h>

// Accept callback is exposed for testing (ADR-018, Bug 5).
// It is otherwise only reachable through lwIP.
extern err_t http_server_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err);

// PCBs handed to the accept callback during a test. Kept so tearDown can
// release the ones the server accepted; the refused one is released by
// the callback itself.
#define TEST_MAX_PCBS 8
static struct tcp_pcb* g_test_pcbs[TEST_MAX_PCBS];
static int g_test_pcb_count = 0;

static struct tcp_pcb* new_test_pcb(void) {
    TEST_ASSERT_LESS_THAN_MESSAGE(TEST_MAX_PCBS, g_test_pcb_count, "test PCB array full");
    struct tcp_pcb* pcb = tcp_new();
    TEST_ASSERT_NOT_NULL_MESSAGE(pcb, "tcp_new() failed - MEMP_NUM_TCP_PCB exhausted?");
    g_test_pcbs[g_test_pcb_count++] = pcb;
    return pcb;
}

void setUp(void) {
    shared_memory_init();
    g_test_pcb_count = 0;
    memset(g_test_pcbs, 0, sizeof(g_test_pcbs));
    TEST_ASSERT_TRUE_MESSAGE(http_server_init(), "http_server_init() failed");
}

void tearDown(void) {
    // http_server_deinit() closes every PCB the server accepted.
    http_server_deinit();
    g_test_pcb_count = 0;
}

/**
 * Test: Connection pool has at least four slots.
 *
 * A page load needs two concurrent connections (HTML + /styles.css).
 * Four slots leave room for one reload in flight and one speculative
 * browser connection. See ADR-018, Bug 4.
 */
void test_connection_pool_has_at_least_four_slots(void) {
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(4, http_server_get_max_connections(),
        "HTTP connection pool must hold at least 4 slots (ADR-018 Bug 4)");
}

/**
 * Test: Accept callback returns ERR_OK for every connection until the pool
 * is full.
 */
void test_accept_returns_ok_until_pool_is_full(void) {
    int max = http_server_get_max_connections();

    for (int i = 0; i < max; i++) {
        struct tcp_pcb* pcb = new_test_pcb();
        err_t err = http_server_accept_callback(NULL, pcb, ERR_OK);
        TEST_ASSERT_EQUAL_MESSAGE(ERR_OK, err,
            "accept must succeed while a pool slot is free");
    }
}

/**
 * Test: Accept callback returns ERR_ABRT, not ERR_MEM, when the pool is full.
 *
 * lwIP calls tcp_abort() on the new PCB itself unless the accept callback
 * returns ERR_ABRT. Returning ERR_MEM after having closed the PCB in the
 * callback releases the PCB twice. See ADR-018, Bug 5.
 */
void test_accept_returns_err_abrt_when_pool_is_full(void) {
    int max = http_server_get_max_connections();

    for (int i = 0; i < max; i++) {
        http_server_accept_callback(NULL, new_test_pcb(), ERR_OK);
    }

    // The refused PCB is aborted (freed) inside the callback, so it must
    // not be tracked for tearDown.
    struct tcp_pcb* refused = tcp_new();
    TEST_ASSERT_NOT_NULL(refused);

    err_t err = http_server_accept_callback(NULL, refused, ERR_OK);
    TEST_ASSERT_EQUAL_MESSAGE(ERR_ABRT, err,
        "accept must return ERR_ABRT when the pool is full (ADR-018 Bug 5)");
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);  // Wait for USB serial to stabilize

    lwip_init();

    printf("\n=== HTTP Server Connection Pool Tests ===\n");

    UNITY_BEGIN();

    RUN_TEST(test_connection_pool_has_at_least_four_slots);
    RUN_TEST(test_accept_returns_ok_until_pool_is_full);
    RUN_TEST(test_accept_returns_err_abrt_when_pool_is_full);

    return UNITY_END();
}
