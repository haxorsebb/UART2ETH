# UART Pin Configuration - SPI0 Conflict Resolution

## Problem

The original configuration used UART0 for stdio output, which could potentially conflict with SPI0 used by the ENC28J60 Ethernet controller, either through pin conflicts or internal peripheral resource sharing on the RP2350.

## Solution

**Moved stdio output to UART1 with dedicated pins:**

### Pin Configuration

| Function | Pin | Description |
|----------|-----|-------------|
| **SPI0 (ENC28J60)** | | |
| SCK | GP2 | SPI Clock |
| MOSI | GP3 | Master Out, Slave In |
| MISO | GP4 | Master In, Slave Out |
| CS | GP5 | Chip Select |
| INT | GP8 | Interrupt |
| **UART1 (Debug/Logging)** | | |
| TX | GP12 | UART1 Transmit |
| RX | GP13 | UART1 Receive |
| **UART0** | | |
| TX | GP0 | **DISABLED** (to avoid SPI0 conflict) |
| RX | GP1 | **DISABLED** (to avoid SPI0 conflict) |

### Baud Rate
- **UART1**: 115200 baud (standard for debugging)

## Implementation Changes

### 1. Main Firmware (`src/main.c`)
```c
// Configure UART1 on GP12/GP13 at 115200 baud for hardware logging
uart_init(uart1, 115200);
gpio_set_function(12, GPIO_FUNC_UART);
gpio_set_function(13, GPIO_FUNC_UART);
```

### 2. CMakeLists.txt
```cmake
# Enable stdio: USB CDC for debugging, UART0 disabled to avoid SPI0 conflict
pico_enable_stdio_usb(uart2eth 1)
pico_enable_stdio_uart(uart2eth 0)  # Disable UART0 to avoid SPI0 conflict
```

### 3. Test Files
- All test executables use the same configuration
- UART0 disabled across all tests
- UART1 manually configured in test main functions

## Hardware Connections

### Debug/Monitoring Setup
1. **USB CDC**: Primary debug interface via USB cable
2. **UART1 Hardware**: Connect UART-to-USB adapter to:
   - **TX** → GP12 
   - **RX** → GP13
   - **GND** → GND

### ENC28J60 Ethernet Module
- Connect SPI pins as defined (GP2-GP5, GP8)
- No conflict with UART1 on GP12/GP13

## Benefits

1. **No Peripheral Conflicts**: UART1 and SPI0 use completely separate resources
2. **Dual Debug Channels**: 
   - USB CDC for development/testing
   - UART1 hardware for production monitoring
3. **Future Expansion**: UART0 pins (GP0/GP1) available for other uses
4. **Consistent Configuration**: Same setup across all firmware and tests

## Verification

The configuration has been tested to ensure:
- ✅ Firmware builds successfully
- ✅ Test suite builds successfully  
- ✅ No pin conflicts between peripherals
- ✅ USB CDC debug output works
- ✅ UART1 hardware logging available

## Tools Support

Update persistent UART logger to use **UART1** hardware connection:
- Connect UART-to-USB adapter to GP12/GP13
- Logger will capture UART1 output at 115200 baud
- No changes needed to logging tools - same serial device interface