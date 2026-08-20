/**
 * @file test_device_page_network_status.c
 * @brief Tests for runtime network state display on the device status page
 *
 * The device status page must display the addresses currently active on the
 * lwIP netif (runtime state), not the persisted static configuration
 * (configured state). Under DHCP these differ; the page previously rendered
 * the configured static gateway and netmask unconditionally.
 *
 * The tests run in static IP mode and simulate a differing runtime state by
 * writing sentinel values directly to the netif, which is what the lwIP DHCP
 * client does when a lease binds. This keeps the tests independent of any
 * DHCP server on the test network.
 *
 * Documentation Reference:
 * - arc42 Chapter 8, "Configured vs. Runtime Network State"
 */

#include "unity.h"
#include "network/network_manager.h"
#include "network/enc28j60_driver.h"
#include "network/lwip_netif_enc28j60.h"
#include "network/http_pages/page_device.h"
#include "shared_memory.h"
#include "pico/stdlib.h"
#include "lwip/netif.h"
#include <stdio.h>
#include <string.h>

// Configured (static) values written to shared memory and network config.
#define CONFIGURED_GATEWAY_STR "192.168.1.1"
#define CONFIGURED_NETMASK_STR "255.255.255.0"

// Sentinel runtime values written to the netif to simulate a DHCP lease
// that differs from the configuration. Chosen to never collide with the
// configured values above.
#define RUNTIME_GATEWAY_STR "10.99.88.77"
#define RUNTIME_NETMASK_STR "255.255.0.0"

#define LINK_WAIT_TIMEOUT_MS 10000

static char g_page_buffer[16384];

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
                             "network manager initialization required");

    // Production applies the loaded configuration via reconfigure after init
    // (see core1_main.c); only this path sets the interface administratively
    // up in static mode. Mirror it here.
    TEST_ASSERT_TRUE_MESSAGE(network_manager_reconfigure(&config),
                             "network manager reconfigure required");

    // Mirror the static configuration into shared memory so the device page
    // sees a configured state that differs from the runtime sentinels.
    shared_memory_layout_t* layout = shared_memory_get_layout();
    TEST_ASSERT_NOT_NULL(layout);
    layout->config.network.static_ip = config.static_ip;
    layout->config.network.static_netmask = config.static_netmask;
    layout->config.network.static_gateway = config.static_gateway;
    layout->config.network.use_dhcp = config.use_dhcp;

    uint32_t start = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - start) < LINK_WAIT_TIMEOUT_MS) {
        network_manager_process();
        if (network_manager_is_link_up()) {
            break;
        }
        sleep_ms(100);
    }
    TEST_ASSERT_TRUE_MESSAGE(network_manager_is_link_up(),
                             "physical link required for this test");
}

// Write runtime sentinel values to the netif, as the DHCP client would on bind.
static void set_runtime_sentinels_on_netif(void) {
    struct netif* nif = netif_default;
    TEST_ASSERT_NOT_NULL_MESSAGE(nif, "netif_default not set after init");

    ip4_addr_t gw;
    ip4_addr_t nm;
    IP4_ADDR(&gw, 10, 99, 88, 77);
    IP4_ADDR(&nm, 255, 255, 0, 0);
    netif_set_gw(nif, &gw);
    netif_set_netmask(nif, &nm);
}

void setUp(void) {
    shared_memory_init();
    network_manager_deinit();
    lwip_netif_enc28j60_deinit();
    enc28j60_deinit();
    sleep_ms(100);
}

void tearDown(void) {
    network_manager_deinit();
    lwip_netif_enc28j60_deinit();
    enc28j60_deinit();
    sleep_ms(100);
}

/**
 * @brief Accessors report unavailable while the network is down
 */
void test_accessors_unavailable_when_network_down(void) {
    simple_ip_addr_t addr;
    TEST_ASSERT_FALSE_MESSAGE(network_manager_get_gateway(&addr),
                              "gateway must be unavailable before init");
    TEST_ASSERT_FALSE_MESSAGE(network_manager_get_netmask(&addr),
                              "netmask must be unavailable before init");
    TEST_ASSERT_FALSE_MESSAGE(network_manager_get_gateway(NULL),
                              "NULL output must be rejected");
    TEST_ASSERT_FALSE_MESSAGE(network_manager_get_netmask(NULL),
                              "NULL output must be rejected");
}

/**
 * @brief Accessors return the netif runtime values, not the configuration
 */
void test_accessors_return_runtime_netif_values(void) {
    bring_network_up_static();
    set_runtime_sentinels_on_netif();

    simple_ip_addr_t addr;
    char str[16];

    TEST_ASSERT_TRUE_MESSAGE(network_manager_get_gateway(&addr),
                             "gateway must be available with network up");
    network_manager_ip_to_string(&addr, str);
    TEST_ASSERT_EQUAL_STRING(RUNTIME_GATEWAY_STR, str);

    TEST_ASSERT_TRUE_MESSAGE(network_manager_get_netmask(&addr),
                             "netmask must be available with network up");
    network_manager_ip_to_string(&addr, str);
    TEST_ASSERT_EQUAL_STRING(RUNTIME_NETMASK_STR, str);
}

/**
 * @brief Device page renders runtime values while config holds static ones
 */
void test_device_page_renders_runtime_values(void) {
    bring_network_up_static();
    set_runtime_sentinels_on_netif();

    memset(g_page_buffer, 0, sizeof(g_page_buffer));
    http_generate_device_page(g_page_buffer, sizeof(g_page_buffer));
    TEST_ASSERT_TRUE_MESSAGE(strlen(g_page_buffer) > 0,
                             "device page must not be empty");

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_page_buffer, RUNTIME_GATEWAY_STR),
                                 "page must show the runtime gateway");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_page_buffer, RUNTIME_NETMASK_STR),
                                 "page must show the runtime netmask");
    TEST_ASSERT_NULL_MESSAGE(strstr(g_page_buffer, CONFIGURED_GATEWAY_STR),
                             "page must not show the configured static gateway");
    TEST_ASSERT_NULL_MESSAGE(strstr(g_page_buffer, CONFIGURED_NETMASK_STR),
                             "page must not show the configured static netmask");
}

int main(void) {
    stdio_init_all();
    // Route UART0 to GP16/GP17, same as production main.c, so the output
    // reaches the debug UART logger. SDK default is GP0/GP1.
    stdio_uart_init_full(uart0, 115200, 16, 17);
    sleep_ms(2000);  // Wait for serial to stabilize

    UNITY_BEGIN();

    printf("\n=== DEVICE PAGE NETWORK STATUS TESTS ===\n");
    RUN_TEST(test_accessors_unavailable_when_network_down);
    RUN_TEST(test_accessors_return_runtime_netif_values);
    RUN_TEST(test_device_page_renders_runtime_values);

    return UNITY_END();
}
