# Development flash script for RP2350 with partitions
# Usage: openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg -f flash_dev.tcl

adapter speed 5000

# Initialize and halt target
init
reset halt

# 1. Flash partition table at 0x0 (ensures consistency)
echo "Flashing partition table..."
flash write_image erase partition_table_slots.bin 0x10000000
verify_image partition_table_slots.bin 0x10000000

# 2. Flash current development binary at partition 0 (0x2000)
echo "Flashing firmware to partition 0..."
flash write_image erase build/uart2eth_factory_internal.bin 0x10002000
verify_image build/uart2eth_factory_internal.bin 0x10002000

# 3. Erase first sector of partition 1 (invalidate any old firmware)
# Adjust 0x10100000 if your partition 1 starts elsewhere
echo "Erasing partition 1 first sector..."
flash erase_address 0x100C0000 0x1000

# echo "Erasing data..."
# flash erase_address 0x1017E000 0x82000

# 4. Reset and run
echo "Flashing complete, rebooting..."
reset run
shutdown
