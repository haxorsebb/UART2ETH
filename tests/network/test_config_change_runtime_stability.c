/**
 * @file test_config_change_runtime_stability.c
 * @brief Regression test: configuration changes must not disturb the running
 *        network
 *
 * Reported defect: after changing a channel's TCP port via the web form, no
 * connection was possible on either the old or the new port until the device
 * was rebooted. Cause: the configuration form triggered a live apply on
 * Core1 that tore down all TCP servers (multi_tcp_server_deinit_all) and
 * immediately re-initialized them; the re-bind path fails and leaves the
 * device without listeners.
 *
 * Per ADR-019 (proposed), ALL network configuration changes take effect at
 * boot. Handling a configuration POST must therefore have no effect on the
 * running network:
 *  - every active TCP listener stays active and bound to its ORIGINAL port,
 *  - the interface keeps its ORIGINAL IP address.
 * The new values become effective only through the ADR-017 reboot path.
 *
 * The tests drive the production entry point (http_parse_post_data) against
 * the real network stack on the target (static IP, ENC28J60 link up),
 * mirroring the bring-up used in test_device_page_network_status.c.
 *
 * Documentation Reference:
 * - ADR-019 (proposed): Network configuration changes take effect at boot
 * - ADR-017: Reboot state handling
 * - arc42 Chapter 5 - Core 1 Network Subsystem
 */

#include "unity.h"
#include "network/http_forms.h"
#include "network/network_manager.h"
#include "network/enc28j60_driver.h"
#include "network/multi_tcp_server.h"
#include "shared_memory.h"
#include "pico/stdlib.h"
#include "lwip/ip4_addr.h"
#include <stdio.h>
#include <string.h>

#define TEST_CHANNEL        CHANNEL_1
#define TEST_ORIGINAL_PORT  4001
#define LINK_WAIT_TIMEOUT_MS 10000

// Form POST bodies as the handler receives them: HTTP headers are skipped
// up to and including the first double CRLF (see http_parse_post_data).
#define POST_PREFIX "POST /config HTTP/1.1\r\n\r\n"

// Static IP the interface runs on during the test is 192.168.1.42; the form
// posts 192.168.1.99, which must NOT become active before reboot.

static shared_memory_layout_t* g_layout;

static char g_error_msg[128];
static char g_success_msg[128];

static bool parse(const char* body) {
    return http_parse_post_data(body, strlen(body),
                                g_error_msg, sizeof(g_error_msg),
                                g_success_msg, sizeof(g_success_msg));
}

/**
 * Bring the network up with a static IP configuration and mirror that
 * configuration into shared memory, so the form handler operates on
 * consistent data (same pattern as test_device_page_network_status.c).
 */
static void bring_network_up_static(void) {
    TEST_ASSERT_TRUE_MESSAGE(enc28j60_init(), "ENC28J60 initialization required");

    network_config_t config;
    network_manager_get_default_config(&config);
    config.use_dhcp = false;

    ip4_addr_t tmp;
    IP4_ADDR(&tmp, 192, 168, 1, 42);
    config.static_ip.addr = tmp.addr;
    IP4_ADDR(&tmp, 255, 255, 255, 0);
    config.static_netmask.addr = tmp.addr;
    IP4_ADDR(&tmp, 192, 168, 1, 1);
    config.static_gateway.addr = tmp.addr;

    TEST_ASSERT_TRUE_MESSAGE(network_manager_init(&config),
                             "Network manager initialization required");
    TEST_ASSERT_TRUE_MESSAGE(network_manager_reconfigure(&config),
                             "Network manager reconfigure required");

    g_layout->config.network.static_ip = config.static_ip;
    g_layout->config.network.static_netmask = config.static_netmask;
    g_layout->config.network.static_gateway = config.static_gateway;
    g_layout->config.network.use_dhcp = config.use_dhcp;

    uint32_t start = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - start) < LINK_WAIT_TIMEOUT_MS) {
        network_manager_process();
        if (network_manager_is_link_up()) {
            return;
        }
        sleep_ms(10);
    }
    TEST_FAIL_MESSAGE("Physical link did not come up (cable connected?)");
}

/**
 * Start the TCP listener under test on its original port and mirror the
 * channel configuration into shared memory.
 */
static void start_listener_on_original_port(void) {
    g_layout->config.channels[TEST_CHANNEL].enabled = true;
    g_layout->config.channels[TEST_CHANNEL].tcp_port = TEST_ORIGINAL_PORT;
    TEST_ASSERT_TRUE_MESSAGE(
        multi_tcp_server_init_channel(TEST_CHANNEL, TEST_ORIGINAL_PORT),
        "Precondition: listener on original port required");
    TEST_ASSERT_TRUE(multi_tcp_server_is_channel_active(TEST_CHANNEL));
}

void setUp(void) {
    TEST_ASSERT_TRUE_MESSAGE(shared_memory_force_reinit(),
                             "Shared memory re-initialization required");
    TEST_ASSERT_TRUE_MESSAGE(flash_persistence_init(),
                             "Flash persistence initialization required");
    g_layout = shared_memory_get_layout();
    TEST_ASSERT_NOT_NULL(g_layout);

    g_error_msg[0] = '\0';
    g_success_msg[0] = '\0';
}

void tearDown(void) {
    multi_tcp_server_deinit_all();
}

/**
 * Test: A port change POST leaves the listener active and bound to the
 * ORIGINAL port.
 *
 * This is the direct regression test for the reported defect. With the
 * defective implementation the listener is gone after the change; with the
 * ADR-019 behavior it is untouched and the new port waits for the reboot.
 */
void test_port_change_post_keeps_listener_on_original_port(void) {
    bring_network_up_static();
    start_listener_on_original_port();

    TEST_ASSERT_TRUE_MESSAGE(parse(POST_PREFIX "ch1_port=5001"),
                             "Handler must report a configuration change");

    TEST_ASSERT_TRUE_MESSAGE(
        multi_tcp_server_is_channel_active(TEST_CHANNEL),
        "Listener must still be active after the configuration change");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        TEST_ORIGINAL_PORT,
        multi_tcp_server_get_channel_port(TEST_CHANNEL),
        "Listener must remain bound to the original port until reboot");
    TEST_ASSERT_TRUE_MESSAGE(
        multi_tcp_server_is_any_listening(),
        "Device must never be left without listeners by a configuration change");
}

/**
 * Test: A static IP change POST leaves the interface on the ORIGINAL
 * address.
 *
 * Address settings are reboot-based like ports: the interface must keep
 * running unchanged, otherwise the user loses the web session that just
 * submitted the form.
 */
void test_static_ip_change_post_keeps_runtime_address(void) {
    bring_network_up_static();
    start_listener_on_original_port();

    ip4_addr_t original_ip;
    IP4_ADDR(&original_ip, 192, 168, 1, 42);

    TEST_ASSERT_TRUE_MESSAGE(parse(POST_PREFIX "static_ip=192.168.1.99"),
                             "Handler must report a configuration change");

    simple_ip_addr_t runtime_ip;
    TEST_ASSERT_TRUE_MESSAGE(network_manager_get_ip_address(&runtime_ip),
                             "Interface must still have a valid address");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        original_ip.addr, runtime_ip.addr,
        "Interface must keep the original address until reboot");
    TEST_ASSERT_TRUE_MESSAGE(
        multi_tcp_server_is_channel_active(TEST_CHANNEL),
        "Listener must survive an address configuration change");
}

/**
 * Test: Repeated configuration POSTs remain stable.
 *
 * The defective implementation degraded with each apply cycle (listen PCB
 * re-bind failures accumulate). Several consecutive changes must leave the
 * listener state unchanged.
 */
void test_repeated_config_posts_keep_listener_stable(void) {
    bring_network_up_static();
    start_listener_on_original_port();

    for (int i = 0; i < 5; i++) {
        // Alternate the posted value so every POST is a real change.
        const char* body = (i % 2 == 0) ? POST_PREFIX "ch1_port=5001"
                                        : POST_PREFIX "ch1_port=5002";
        parse(body);

        TEST_ASSERT_TRUE_MESSAGE(
            multi_tcp_server_is_channel_active(TEST_CHANNEL),
            "Listener must survive every configuration change");
        TEST_ASSERT_EQUAL_UINT16(
            TEST_ORIGINAL_PORT,
            multi_tcp_server_get_channel_port(TEST_CHANNEL));
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);  // Allow USB/UART enumeration before test output

    printf("\n=== Configuration Change Runtime Stability Tests (ADR-019 proposed) ===\n");

    UNITY_BEGIN();
    RUN_TEST(test_port_change_post_keeps_listener_on_original_port);
    RUN_TEST(test_static_ip_change_post_keeps_runtime_address);
    RUN_TEST(test_repeated_config_posts_keep_listener_stable);
    return UNITY_END();
}
