# UART2ETH Debug Setup with Pi Pico Debug Probe

## Hardware Setup
- ✅ Pi Pico Debug Probe connected to PC (USB)
- ✅ Debug Probe connected to target board via SWD
- ✅ Target board: RP2350 (Pico 2)

## Status
- ✅ Debug Probe detected: ID 2e8a:000c Raspberry Pi Debug Probe (CMSIS-DAP)
- ✅ OpenOCD connection working
- ✅ Both Cortex-M33 cores detected
- ⚠️ Debug Probe firmware is old (1.0.1) - consider updating

## Build Configuration
- ✅ Project configured for debug mode with `-DDEBUG=ON`
- ✅ Debug symbols enabled with `-g3 -O0 -fno-omit-frame-pointer`
- ✅ Main executable built: `build/uart2eth.elf`

## VS Code Debug Configurations

### 1. "Pico Debug (Debug Probe)" - Recommended
- Starts OpenOCD automatically
- Loads and runs the program
- Stops at main() function
- Full debugging features available

### 2. "Pico Attach (Debug Probe)"
- Attaches to running program without resetting
- Useful for debugging already running code

### 3. "Pico Debug (External OpenOCD)"
- Connects to manually started OpenOCD instance
- For advanced debugging scenarios

## Quick Start Debugging

### Method 1: VS Code (Recommended)
1. Open the project in VS Code
2. Set breakpoints in your code
3. Press F5 or go to Run > Start Debugging
4. Select "Pico Debug (Debug Probe)"

### Method 2: Manual OpenOCD + GDB
1. Start OpenOCD:
   ```bash
   cd /home/shueltenschmidt/projects/UART2ETH
   ${HOME}/.pico-sdk/openocd/0.12.0+dev/openocd \
     -s ${HOME}/.pico-sdk/openocd/0.12.0+dev/scripts \
     -f interface/cmsis-dap.cfg \
     -f target/rp2350.cfg \
     -c "adapter speed 5000"
   ```

2. In another terminal, start GDB:
   ```bash
   ${HOME}/.pico-sdk/toolchain/14_2_Rel1/bin/arm-none-eabi-gdb build/uart2eth.elf
   ```

3. In GDB, connect and load:
   ```
   target remote localhost:3333
   monitor reset init
   load
   break main
   continue
   ```

## Available VS Code Tasks
- **Build Debug**: Builds with debug symbols enabled
- **Start OpenOCD (Debug Probe)**: Starts OpenOCD server in background
- **Flash**: Programs the device via OpenOCD
- **Compile Project**: Standard build

## Debug Features Available
- ✅ Set/clear breakpoints
- ✅ Step through code (step into/over/out)
- ✅ Variable inspection
- ✅ Call stack viewing
- ✅ Memory inspection
- ✅ Register viewing (via SVD file)
- ✅ Multi-core debugging (both Cortex-M33 cores)

## Firmware Update Recommendation
The Debug Probe firmware is version 1.0.1. For better performance, update to the latest version:
1. Download latest firmware from: https://github.com/raspberrypi/debugprobe/releases/latest
2. Hold BOOTSEL button while connecting Debug Probe to PC
3. Copy the .uf2 file to the RPI-RP2 drive

## Connection Diagram
```
PC USB ←→ Debug Probe ←→ Target Board (SWD)
                         ├─ SWDIO (Pin 2)
                         ├─ SWCLK (Pin 3)
                         └─ GND   (Pin 8)
```

## Troubleshooting
- If "Resource busy" error: Kill any running OpenOCD processes
- If connection fails: Check SWD wiring and power to target
- If VS Code debug fails: Try "Start OpenOCD" task first
- For slow debugging: Update Debug Probe firmware
- **If "Undefined Command: enable-pretty-printing"**: ✅ **FIXED** - GDB lacks Python support, using native commands instead

## Project Structure for Debugging
```
UART2ETH/
├── .vscode/
│   ├── launch.json     # Debug configurations
│   ├── tasks.json      # Build/OpenOCD tasks
│   └── settings.json   # VS Code settings
├── build/
│   └── uart2eth.elf    # Debug binary with symbols
└── CMakeLists.txt      # Build configuration with debug support
```
