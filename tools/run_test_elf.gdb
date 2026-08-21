# Load and run an on-target test ELF via an already running openocd server.
# Usage: gdb-multiarch -batch -x tools/run_test_elf.gdb build/tests/<test>.elf
# Afterwards read the test output with ./tools/persistent_uart_logger.sh tail
set confirm off
set pagination off
target extended-remote :3333
monitor reset halt
load
monitor reset halt
monitor resume
detach
quit
