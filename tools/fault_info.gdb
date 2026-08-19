# Attach, halt, dump core 0 fault context. Usage: gdb-multiarch -batch -x tools/fault_info.gdb <elf>
set confirm off
set pagination off
target extended-remote :3333
monitor halt
thread 1
bt 15
info registers pc sp lr xpsr msp psp
printf "CFSR  %08x\n", *(unsigned int*)0xE000ED28
printf "HFSR  %08x\n", *(unsigned int*)0xE000ED2C
printf "MMFAR %08x\n", *(unsigned int*)0xE000ED34
printf "BFAR  %08x\n", *(unsigned int*)0xE000ED38
printf "SHCSR %08x\n", *(unsigned int*)0xE000ED24
printf "ICSR  %08x\n", *(unsigned int*)0xE000ED04
x/12xw $sp
thread 2
bt 10
detach
quit
