# Post-mortem snapshot of both cores after a UART/TCP connection drop.
# Does NOT reset the target. Usage (with openocd already running):
#   gdb-multiarch build/uart2eth.elf -batch -x tools/gdb_hang_diag.gdb | tee logs/hang_diag_$(date +%s).txt
set pagination off
set confirm off
target extended-remote :3333

echo \n=== state machine ===\n
print/d 'state_machine.c'::g_main_state
print/d 'state_machine.c'::g_core0_substate
print/d 'state_machine.c'::g_core1_substate

echo \n=== ringbuffer ===\n
print 'ringbuffer.c'::g_ringbuffer

echo \n=== SIO FIFO status (bit0 VLD rx, bit1 RDY tx, bit2 WOF, bit3 ROE) ===\n
print/x *(unsigned int*)0xd0000050

echo \n=== cores ===\n
info threads
thread 1
echo --- core0 ---\n
bt 20
info registers pc lr sp xpsr primask
thread 2
echo --- core1 ---\n
bt 20
info registers pc lr sp xpsr primask
detach
