/**
 * @file main.c
 * @brief Main entry point for UART2ETH firmware with dual-core launch
 *
 * Implements dual-core startup sequence with event-driven state machine
 * coordination as documented in arc42 runtime view.
 *
 * Architecture:
 * - Core0: UART processing with event-driven state machine
 * - Core1: Network, persistence, and log processing
 * - State machine: Three independent event-driven state machines
 *
 * Documentation Reference:
 * - ADR-007: Event-Driven State Machine Architecture
 * - arc42 Chapter 5 - Building Block View
 *
 * 
Changes from 0.9.7 (production version until 2026-06-09) to 0.9.10
Several problems were noticed:

- Link detection problem
  When the network cable is plugged in AFTER the Shark mainboard is powered, it is very unlikely (<20 % chance)
  that the PC/laptop can connect to the mainboard.

  Reason: The ENC28J60 does not signal the link status correctly, or the firmware does not react properly. 
  The link detection by interrupt does not work.

  Fix: RP2354 now polls link status every 500 ms, this works.

- Parameter restore and firmware update problem
  When Sharknet sends many packets quickly, it is observed that 
  a) some packets are truncated at the beginning (first ~80 chars missing)
  b) some packets are completely lost
  c) sometimes the order of the packets is mixed up (typically ABCDE => CDEAB or ABC => BCA)
  As a result, parameter restore and firmware update both do not work with board revision 07 (RP2354 Ethernet interface).
  
  Analysis and fix:
  It is possible that a TCP packet contains more than one Sharknet message. The firmware currently assumes that
  each Sharknet message is a separate TCP packet and looks for the termination character only at the end of the TCP packet.
  This is wrong. The firmware must look for termination characters everywhere in the TCP packet and enqueues each
  Sharknet message separately => fixed.
    
  It is possible that more than one Sharknet message arrives at the same millisecond. So looking for the "oldest" message
  by a millisecond timestamp can fail. Timestamp replaced by sequence index => fixed.
  
  Note that also an update of the Sharknet software is required.
  In the sharknet software we can set socket.nodelay = true and socket.blocking = true. This forces each Sharknet packet
  to be sent immediately and fixes the problem. However, the problem is also fixed in the RP2354 firmware now.

 */

#include "debug.h"
#include "factory_defaults.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "log_manager.h"
#include "pico/multicore.h"
#include "pico/stdio_uart.h"
#include "ringbuffer.h"
#include "shared_memory.h"
#include "state_machine.h"
#include "timestamp.h"
#include "utils/selftest.h"
#include <stdio.h>

// Forward declarations for core main functions

void core0_main(void);
void core1_main(void);

#define FACTORY_RESET_GPIO 1

/**
 * Core1 entry point function
 *
 * This function is called when Core1 is launched. It performs
 * Core1-specific initialization and then calls the main Core1 loop.
 */
// Minimal Core1 entry point for debugging hard fault
void core1_entry() {
  // ABSOLUTE MINIMAL - no printf, no shared memory, no complex operations

  // Simple infinite loop with basic operations to test Core1 viability
  // Launch Core1 main function directly
  core1_main();

  // Should never reach here
  while (true) {
    busy_wait_us(1000000); // 1 second - error state
  }
}

/**
 * Main entry point - runs on Core0
 *
 * Initializes system components and launches both cores with their
 * respective main functions using the event-driven state machine.
 */
int main() {

  // Initialize UART0 for debug output
  stdio_uart_init_full(uart0, 115200, 16, 17);

  #ifdef FACTORY_INTERNAL_VERSION
  // perform a simple selftest, output the results to UART1 so that the
  // BOARDTEST software can see it

  //The next line must be commented out if the factory internal version runs on a board without boardtest software
  selftest();
  #endif

  // Configure GPIO21 to output 25 MHz clock for ENC28J60
  float clk_div = (float)clock_get_hz(clk_sys) / 25000000.0f;
  clock_gpio_init(21, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                  clk_div);

  // Configure Factory Reset Button
  gpio_init(FACTORY_RESET_GPIO);
  gpio_set_function(FACTORY_RESET_GPIO, GPIO_FUNC_SIO);
  gpio_set_dir(FACTORY_RESET_GPIO, GPIO_IN);
  gpio_pull_up(FACTORY_RESET_GPIO);

  // Wait for USB-serial connection for debugging
  sleep_ms(2000);
  printf("UART2ETH COPYRIGHT 2025 CASSEL MESSTECHNIK GMBH\nBUILD: ");
  printf(_TIMEZ_);
#ifdef FACTORY_INTERNAL_VERSION
  printf("\n!!!FACTORY INTERNAL VERSION!!!\n!!!NEVER TO BE "
         "SHIPPED!!!\n!!!TRAINED AND AUTHORIZED PERSONAL ONLY!!!\n");
#endif
  printf("\n--------SOFTWARE START--------\n");

  // Initialize and display factory defaults (early boot)
  factory_defaults_init();
  factory_defaults_print_serial_number();

  // read factory reset pin after wait
  if (!gpio_get(FACTORY_RESET_GPIO)) {
    // reset was requested by user
    do_factory_reset(); // reset will happen druing flash persistence init
  }
  printf("AFTER FACTORY RESET\n");
  factory_defaults_print_serial_number();

  // Initialize shared memory system
  if (!shared_memory_init()) {
    printf("ERROR: Shared memory init failed!\n");
    while (true) {
      sleep_ms(1000); // Halt system on critical error
    }
  }
  printf("AFTER SHARED\n");

  factory_defaults_print_serial_number();

  // Initialize ring buffer for UART-TCP message bridging
  if (!ringbuffer_init()) {
    printf("ERROR: Ringbuffer init failed!\n");
    while (true) {
      sleep_ms(1000); // Halt system on critical error - ringbuffer init failed
    }
  }

  // Initialize event-driven state machine
  if (!state_machine_init()) {
    printf("ERROR: State machine init failed!\n");
    while (true) {
      sleep_ms(1000); // Halt system on critical error
    }
  }

  // Initialize log manager
  if (!log_manager_init()) {
    printf("ERROR: Log manager init failed!\n");
    while (true) {
      sleep_ms(1000); // Halt system on critical error
    }
  }
  // RP2350 specific: Ensure clocks are stable before Core1 launch
  uint32_t sys_clk = clock_get_hz(clk_sys);
  log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_INFO, LOG_EVENT_SYSTEM_CLOCK,
            sys_clk);
  if (sys_clk < 1000000) { // Less than 1MHz indicates clock issues
    printf("ERROR: System clock too low (%u Hz), multicore unsafe\n", sys_clk);
    while (1)
      sleep_ms(1000);
  }

  // Ensure all memory operations are complete before launching Core1
  __dsb(); // Data Synchronization Barrier
  __isb(); // Instruction Synchronization Barrier

  // Launch Core1 with network and maintenance processing
  // Note: On RP2350, this should automatically handle stack allocation
  multicore_launch_core1(core1_entry);

  // flush remaining messages
  fflush(stdout);

  // Small delay to let any immediate hard fault surface
  sleep_ms(10);

  // Ensure all memory operations are complete before launching Core0
  __dsb(); // Data Synchronization Barrier
  __isb(); // Instruction Synchronization Barrier

  core0_main();
  factory_defaults_print_serial_number();

  // Should never reach here
  while (true) {
    log_event(EVENT_SOURCE_SYSTEM, LOG_LEVEL_ERROR, LOG_EVENT_CORE_EXIT_ERROR,
              0);
    sleep_ms(1000);
  }

  return 0; // Never reached
}