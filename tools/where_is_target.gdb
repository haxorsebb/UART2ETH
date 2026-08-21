# Attach to running openocd, halt, print backtrace of both cores, resume, detach.
# Usage: gdb-multiarch -batch -x tools/where_is_target.gdb <elf>
set confirm off
set pagination off
target extended-remote :3333
monitor halt
info threads
thread 1
bt 8
thread 2
bt 8
monitor resume
detach
quit
