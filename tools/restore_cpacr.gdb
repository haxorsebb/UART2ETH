# Restore the coprocessor access register (CPACR) on core 0 before resuming.
#
# Background (2026-08-26, PR #103): the bundled openocd 0.12.0+dev (2025-07-17)
# runs its RP2350 flash probe on core 0 at the first gdb connection of a
# session. The probe enables only the RCP (CP7) and leaves
# CPACR = 0x0000C000. The pico runtime_init value is 0x00F0C303
# (CP0 GPIO coprocessor, CP4 DCP, CP7 RCP, CP10/CP11 FPU). After resuming,
# the first FPU or GPIO-coprocessor instruction on core 0 raises a NOCP
# UsageFault that escalates to HardFault; in this firmware that is the
# alarm-pool IRQ handler (USB stdio tick), within about 1 ms. The symptom is
# a "lock-up" that the firmware did not cause, and USB-CDC logging stops
# mid-line.
#
# Core 1 is not touched by the probe. The write ORs the bits in, so it is
# harmless when CPACR is already correct.
#
# Usage: source this file, then call restore_cpacr before monitor resume or
# detach (detach resumes the target). Bare attach-and-look scripts in this
# directory do that already.
define restore_cpacr
  thread 1
  set *(unsigned int*)0xE000ED88 = *(unsigned int*)0xE000ED88 | 0x00f0c303
  printf "core 0 CPACR = %08x (restored)\n", *(unsigned int*)0xE000ED88
end
