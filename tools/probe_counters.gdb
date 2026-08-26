# Sample live counters without stopping the target for long.
# Usage: gdb-multiarch -batch -x tools/probe_counters.gdb build/uart2eth.elf
set confirm off
set pagination off
target extended-remote :3333
source tools/restore_cpacr.gdb
monitor halt
printf "main_state=%d core0_sub=%d core1_sub=%d\n", g_main_state, g_core0_substate, g_core1_substate
printf "enc int_pending=%d int_time=%u\n", g_interrupt_pending, g_interrupt_time
printf "enc rx=%u tx=%u rxerr=%u txerr=%u next_ptr=%u\n", g_enc28j60_state.packets_received, g_enc28j60_state.packets_sent, g_enc28j60_state.rx_errors, g_enc28j60_state.tx_errors, g_enc28j60_state.next_packet_ptr
printf "core0 active timers=%u core1 active timers=%u\n", g_core0_timers.active_timer_count, g_core1_timers.active_timer_count
printf "core1 NET_TIMEOUT active=%d expired=%d DHCP_DISC active=%d expired=%d LINK_POLL active=%d\n", g_core1_timers.timers[1].active, g_core1_timers.timers[1].expired, g_core1_timers.timers[6].active, g_core1_timers.timers[6].expired, g_core1_timers.timers[11].active
thread 2
bt 12
restore_cpacr
monitor resume
detach
quit
