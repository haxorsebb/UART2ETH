/**
 * @file test_config_change_reboot_semantics.c
 * @brief Tests for reboot-based network configuration change semantics
 *
 * All network configuration changes (TCP ports, static IP/netmask/gateway,
 * DHCP mode) are persisted but take effect only at the next boot
 * (ADR-019, proposed). The form handler must therefore:
 *  - store and persist valid new values,
 *  - reject invalid values,
 *  - tell the user that a reboot is required,
 *  - NOT signal a live configuration apply to Core1: config_change_pending
 *    stays false. The former live-apply path tore down all TCP servers and
 *    left the device without a working listener until reboot.
 *
 * Documentation Reference:
 * - ADR-019 (proposed): Network configuration changes take effect at boot
 * - arc42 Chapter 6 - Runtime View, configuration change sequence
 */

// For strcasestr() from newlib's string.h
#define _GNU_SOURCE

#include "unity.h"
#include "network/http_forms.h"
#include "network/http_pages/page_config.h"
#include "shared_memory.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

// Form POST bodies as the handler receives them: HTTP headers are skipped
// up to and including the first double CRLF (see http_parse_post_data).
#define POST_PREFIX "POST /config HTTP/1.1\r\n\r\n"

static char g_error_msg[128];
static char g_success_msg[128];

static shared_memory_layout_t* g_layout;

void setUp(void) {
    TEST_ASSERT_TRUE_MESSAGE(shared_memory_force_reinit(),
                             "Shared memory re-initialization required");
    TEST_ASSERT_TRUE_MESSAGE(flash_persistence_init(),
                             "Flash persistence initialization required");

    g_layout = shared_memory_get_layout();
    TEST_ASSERT_NOT_NULL(g_layout);

    g_layout->config_change_pending = false;
    g_error_msg[0] = '\0';
    g_success_msg[0] = '\0';
}

void tearDown(void) {
}

static bool parse(const char* body) {
    return http_parse_post_data(body, strlen(body),
                                g_error_msg, sizeof(g_error_msg),
                                g_success_msg, sizeof(g_success_msg));
}

/**
 * Test: A valid port change is stored in the shared memory configuration.
 */
void test_valid_port_change_is_stored(void) {
    uint16_t old_port = g_layout->config.channels[1].tcp_port;
    TEST_ASSERT_NOT_EQUAL_MESSAGE(5001, old_port,
                                  "Precondition: test port must differ from default");

    bool changed = parse(POST_PREFIX "ch1_port=5001");

    TEST_ASSERT_TRUE_MESSAGE(changed, "Handler must report a configuration change");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5001, g_layout->config.channels[1].tcp_port,
                                     "New port must be stored in configuration");
}

/**
 * Test: A valid configuration change increments the revision counter so the
 * persistence layer writes the new configuration to flash.
 */
void test_valid_port_change_increments_revision(void) {
    uint32_t revision_before = g_layout->revision_counter;

    parse(POST_PREFIX "ch1_port=5001");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(revision_before + 1,
                                     g_layout->revision_counter,
                                     "Configuration change must increment the revision counter");
}

/**
 * Test: The success message for a port change must state that a reboot is
 * required.
 *
 * The change takes effect at the next boot; without this notice the user
 * assumes the change is live and reports the connection as broken.
 */
void test_port_change_message_states_reboot_required(void) {
    parse(POST_PREFIX "ch1_port=5001");

    TEST_ASSERT_NOT_NULL_MESSAGE(strcasestr(g_success_msg, "reboot"),
        "Success message must state that the change takes effect after reboot");
}

/**
 * Test: A static IP change is stored and its success message states that a
 * reboot is required.
 *
 * Address settings follow the same reboot semantics as ports: they are
 * persisted, the runtime interface is untouched until the next boot.
 */
void test_static_ip_change_is_stored_and_states_reboot_required(void) {
    bool changed = parse(POST_PREFIX "static_ip=192.168.1.99");

    TEST_ASSERT_TRUE_MESSAGE(changed, "Handler must report a configuration change");

    uint32_t stored = g_layout->config.network.static_ip.addr;
    uint8_t octet0 = (uint8_t)(stored & 0xFF);
    uint8_t octet3 = (uint8_t)((stored >> 24) & 0xFF);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(192, octet0,
                                    "First octet of new static IP must be stored");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(99, octet3,
                                    "Last octet of new static IP must be stored");

    TEST_ASSERT_NOT_NULL_MESSAGE(strcasestr(g_success_msg, "reboot"),
        "Success message must state that the change takes effect after reboot");
}

/**
 * Test: No configuration change may signal a live configuration apply.
 *
 * config_change_pending formerly triggered core1_apply_configuration_changes(),
 * which reconfigured the network interface and tore down all TCP servers.
 * All network settings are reboot-based; the flag must stay false for
 * port changes and address changes alike.
 */
void test_config_change_does_not_signal_live_apply(void) {
    parse(POST_PREFIX "ch1_port=5001");
    TEST_ASSERT_FALSE_MESSAGE(g_layout->config_change_pending,
        "Port change must not set config_change_pending (no live apply)");

    parse(POST_PREFIX "static_ip=192.168.1.99");
    TEST_ASSERT_FALSE_MESSAGE(g_layout->config_change_pending,
        "Static IP change must not set config_change_pending (no live apply)");
}

/**
 * Test: After a stored configuration change, the config page offers a
 * "Reboot now" control next to the success message.
 *
 * The reboot is the required next user action (changes take effect at
 * boot); without a control on the same page the user has to know that the
 * reboot button lives on the update page.
 */
void test_config_page_offers_reboot_after_change(void) {
    static char page[16384];
    const char* msg = "Configuration saved. Changes take effect after reboot.";

    http_generate_config_page(page, sizeof(page),
                              NULL, 0, msg, strlen(msg),
                              true /* offer_reboot */);

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(page, "action=\"/reboot\""),
        "Config page must contain a POST /reboot form after a stored change");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(page, "Reboot now"),
        "Config page must offer a 'Reboot now' control after a stored change");
}

/**
 * Test: Without a stored change the config page has no reboot control.
 *
 * An always-present reboot button next to a "No changes detected" message
 * would invite pointless reboots.
 */
void test_config_page_has_no_reboot_control_without_change(void) {
    static char page[16384];
    const char* msg = "No changes detected";

    http_generate_config_page(page, sizeof(page),
                              NULL, 0, msg, strlen(msg),
                              false /* offer_reboot */);

    TEST_ASSERT_NULL_MESSAGE(strstr(page, "action=\"/reboot\""),
        "Config page must not contain a reboot form without a stored change");
}

/**
 * Test: A stored channel 4 port change survives the persistence round
 * trip (save to flash, wipe RAM, load from flash).
 *
 * This is the boot path in miniature: with reboot-based semantics
 * (ADR-019) the loader is the only apply path, so it must deliver the
 * persisted values unchanged. Regression: the loader unconditionally
 * overwrote channel 4's port with the mode default 4002, discarding every
 * user change at boot. Channel 4 is used deliberately: it is the channel
 * subject to the SHARK device mode enforcement block in
 * flash_persistence_load_configuration().
 */
void test_ch4_port_change_survives_persistence_round_trip(void) {
    // Every test in this suite re-inits shared memory to revision 1 and
    // saves as revision 2, so the flash ring holds several revision-2
    // blocks from earlier tests. Raise the revision so the block written
    // here is unambiguously the newest one for the loader's best-block
    // search.
    g_layout->revision_counter = 1000000 + g_layout->revision_counter;

    TEST_ASSERT_TRUE_MESSAGE(parse(POST_PREFIX "ch4_port=5001"),
                             "Handler must report a configuration change");
    TEST_ASSERT_EQUAL_UINT16(5001, g_layout->config.channels[CHANNEL_4].tcp_port);

    // Simulate the reboot: wipe the RAM configuration, then load from flash
    // like the boot sequence does.
    TEST_ASSERT_TRUE_MESSAGE(shared_memory_force_reinit(),
                             "RAM wipe (reboot simulation) required");
    TEST_ASSERT_NOT_EQUAL_MESSAGE(5001,
        shared_memory_get_layout()->config.channels[CHANNEL_4].tcp_port,
        "Precondition: RAM wipe must discard the in-memory value");

    TEST_ASSERT_TRUE_MESSAGE(flash_persistence_load_configuration(),
                             "Configuration load from flash required");

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5001,
        shared_memory_get_layout()->config.channels[CHANNEL_4].tcp_port,
        "Loader must deliver the persisted channel 4 port, not the mode default");
}

/**
 * Test: Port values below 1024 are rejected and the configuration is
 * left unchanged (privileged port range, matches boot-time validation
 * in flash_persistence.c).
 */
void test_privileged_port_is_rejected(void) {
    uint16_t old_port = g_layout->config.channels[1].tcp_port;

    parse(POST_PREFIX "ch1_port=80");

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(old_port,
                                     g_layout->config.channels[1].tcp_port,
                                     "Port below 1024 must be rejected");
}

/**
 * Test: Non-numeric port values are rejected and the configuration is
 * left unchanged.
 */
void test_non_numeric_port_is_rejected(void) {
    uint16_t old_port = g_layout->config.channels[1].tcp_port;

    parse(POST_PREFIX "ch1_port=abc");

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(old_port,
                                     g_layout->config.channels[1].tcp_port,
                                     "Non-numeric port must be rejected");
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);  // Allow USB/UART enumeration before test output

    printf("\n=== Configuration Change Reboot Semantics Tests (ADR-019 proposed) ===\n");

    UNITY_BEGIN();
    RUN_TEST(test_valid_port_change_is_stored);
    RUN_TEST(test_valid_port_change_increments_revision);
    RUN_TEST(test_port_change_message_states_reboot_required);
    RUN_TEST(test_static_ip_change_is_stored_and_states_reboot_required);
    RUN_TEST(test_config_change_does_not_signal_live_apply);
    RUN_TEST(test_config_page_offers_reboot_after_change);
    RUN_TEST(test_config_page_has_no_reboot_control_without_change);
    RUN_TEST(test_ch4_port_change_survives_persistence_round_trip);
    RUN_TEST(test_privileged_port_is_rejected);
    RUN_TEST(test_non_numeric_port_is_rejected);
    return UNITY_END();
}
