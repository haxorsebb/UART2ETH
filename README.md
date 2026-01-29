# UART2ETH
An RPI RP2350 based hardware and firmware solution to bridge UART to TCP sockets.
Up to 4 UARTS are exposed as TCP Sockets. Secure OTA firmware updates, A/B updates, custom plugable serial protocol filters allow efficient TCP packaging. Package caching and custom transmit timeout allow optimizations for low latency or efficient bulk data transfers.
GPL Licence


FLASHING:
The target is partitioned, so different flashing methods might yield different results.
The target uses TBYB scheme, so different flashing methods might yield different results.
Some data to clarify:
The partition table is within the first 8K of falsh, up to 0x2000.
Partition A starts after that, then Partition B.
After that are 8K of factory defaults, the last 512K are data storage for config and logs.

Using picotool:
After the target has been partitioned by 
"picotool load partition_table.uf2"
the 'ota' variant of the firmware can be flashed with 
"picotool load -x uart2eth_ota.uf2"
The -x starts the new image and the image will 'buy' itself if it reaches main state 'operation'.

Using Web management UI upload:
Upload the 'ota' variant of the firmware.
After the upload, start the new image and the image will 'buy' itself if it reaches main state 'operation'.

Using OpenOCD (pi pico project extension in VS Codium):
A special flash script (flash_dev.tcl) is necessary to flash the target in a compatible manner. This script will:
- flash the partition table to 0x0-0x2000
- flash the binary (uart2eth) to 0x2000-
- invalidate the B partition 
- start the firmware
- keep the data and factory defaults intact

Using the shell:
run flash_dev.sh to run OpenOCD to do its job.


